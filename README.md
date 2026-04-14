# TorChat

A peer-to-peer chat application written in C with a graphical interface. TorChat lets users chat directly with each other over a local network or the internet — no central server required. Peers connect by exchanging IP addresses and port numbers, and all messages flow directly between clients.

---

## Features

- **Serverless P2P messaging** — peers connect directly via TCP; no relay or sign-in server needed
- **Graphical UI** — built with [Raylib](https://www.raylib.com/) and [raygui](https://github.com/raysan5/raygui)
- **Multi-peer support** — up to 64 simultaneous peer connections
- **Nickname handshake** — peers automatically exchange display names on connect via a lightweight `/nick` / `/getnick` protocol
- **Broadcast messaging** — every message sent is delivered to all connected peers
- **Local echo** — senders see their own messages in the chat log immediately
- **Persistent chat history** — all messages are saved to a per-user SQLite database and replayed on next launch
- **Scrollable chat history** — ring buffer holds up to 512 lines; auto-scrolls to the latest message
- **Live connection panel** — sidebar shows connected peers; system messages announce connects and disconnects
- **Non-blocking I/O** — `select()`-based networking with a zero-timeout poll keeps the UI responsive at all times
- **Both listen and dial** — a node can accept incoming connections, dial out to peers, or do both simultaneously

---
## Dependencies

| Dependency | Purpose | How to install |
|---|---|---|
| **GCC** (C11) | Compiler | Ships with most Linux distros; `brew install gcc` on macOS |
| **Raylib** | Window, rendering, input | Built automatically from the bundled submodule — no install needed |
| **raygui** | Immediate-mode GUI widgets | Bundled as `UI/raygui.h` — no install needed |
| **SQLite3** | Message persistence | `sudo apt install libsqlite3-dev` · `brew install sqlite3` |
| **POSIX sockets** | Networking | Standard on Linux & macOS |
>
> **Linux system deps** (required before `make`):
> ```bash
> sudo apt install libsqlite3-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
> ```

---

## How to Build & Run

### Linux / macOS (full application)

```bash
# 1. Install system dependencies
# Linux:
sudo apt install libsqlite3-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
# macOS:
brew install sqlite3

# 2. Clone with submodules (raylib is bundled as a git submodule)
git clone --recurse-submodules https://github.com/CharlesLiuCool/torchat.git
cd torchat

# 3. Build (compiles raylib from source on first run, then links everything)
make

# 4. Run
./torchat
```

On startup you will be prompted for:
- **Nickname** — your display name shown to other peers; also names your history file (`<nickname>.db`)
- **Listen port** — the TCP port to accept incoming connections on (enter `0` to skip listening and dial-out only)

### Connecting two peers

**Peer A** (listening):
```
Enter nickname: Alice
Enter listen port (0 to skip): 9000
```

**Peer B** (dialling out) — use the IP/Port fields in the UI sidebar and click **Connect**:
```
IP:   192.168.1.42
Port: 9000
```

Once connected, both peers exchange nicknames automatically and can start chatting.

---

## Architecture Overview

### Design philosophy

TorChat is built around three hard constraints: **single process, no threads, minimal dependencies**. Every architectural decision flows from these.

Because there are no threads, the UI render loop doubles as the application's heartbeat. Raylib calls `BeginDrawing` / `EndDrawing` at 60 FPS, and the network and storage layers are driven inline — they never block, so the window stays responsive regardless of what's happening on the wire.

### Layer diagram

```
┌─────────────────────────────────────────────┐
│                   UI Layer                  │
│   UI/main_ui.c  —  Raylib + raygui          │
│   Renders frames, handles input, fires      │
│   backend_* calls on user actions           │
└────────────────┬────────────────────────────┘
                 │  backend_* API  (backend.h)
┌────────────────▼────────────────────────────┐
│                Backend Layer                │
│   backend.c  —  owns all application state  │
│   • peer table (up to 64 peers)             │
│   • nickname handshake protocol             │
│   • message formatting & broadcast         │
│   • fires UI callbacks on events           │
│   • drives storage on every message        │
└──────┬─────────────────────┬────────────────┘
       │                     │
┌──────▼───────┐   ┌─────────▼──────────────┐
│ Network Layer│   │    Storage Layer        │
│  net.c       │   │    storage.c            │
│  • socket()  │   │    • SQLite WAL DB      │
│  • bind()    │   │    • one file per user  │
│  • listen()  │   │    • persists every msg │
│  • accept()  │   │    • replays on startup │
│  • connect() │   └─────────────────────────┘
│  • select()  │
└──────────────┘
```

### Main loop

The entire application ticks through a single loop in `run_ui()`. Network I/O and storage are driven as side effects of the frame:

```
while window open:
    backend_poll()         ← zero-timeout select() on all open sockets
        ├─ accept new connections
        ├─ recv() from each ready peer
        │    ├─ /nick or /getnick → handshake, no UI event
        │    └─ chat message → fire on_message cb → storage_save_message()
        └─ (returns immediately if nothing is ready)

    render UI frame        ← Raylib BeginDrawing / EndDrawing
    handle user input      ← text box, send button, connect form
```

### Backend API

The backend is the only layer that owns sockets. The UI and storage layers never call `send()` or `recv()` directly — everything goes through this interface:

```c
// Lifecycle
backend_init(nickname, on_message, on_connected, on_disconnected);
backend_shutdown();

// Networking
backend_start_listening(port);      // bind + listen — become a server
backend_connect_to_peer(ip, port);  // dial out and handshake

// Messaging
backend_send_message(text);         // format, echo locally, broadcast, persist

// Per-frame driver — call every frame from the UI
backend_poll();

// Peer introspection — used by the UI sidebar
backend_peer_count();
backend_peer_name(index);
backend_peer_fd(index);
```

### Nickname handshake protocol

When two peers connect, they immediately negotiate nicknames over a tiny text protocol before any chat messages flow:

```
Connector → Acceptor:   /nick Alice
Connector → Acceptor:   /getnick
Acceptor  → Connector:  /nick Bob

(both sides now have each other's display name)
```

Any message that doesn't start with `/nick` or `/getnick` is treated as a chat message and dispatched to the UI and storage.

### Callback flow

The backend fires three callbacks into the UI layer. These are wired up in `main.c` and implemented in `UI/main_ui.c`:

```
on_message(fd, text)      → chat_push(text)              — renders the message
on_connected(fd)          → chat_push("*** Peer N connected")
on_disconnected(fd)       → chat_push("*** Peer N disconnected")
```

`fd == -1` is a special sentinel meaning the message came from the local user (self-echo after sending).

### Storage integration

Storage is owned by the backend — the UI layer only interacts with it at startup to replay history:

```
backend_init()            → storage_open("<nickname>.db")
backend_send_message()    → storage_save_message()        — every outgoing message
backend_poll() recv path  → storage_save_message()        — every incoming message
backend_shutdown()        → storage_close()

run_ui() startup          → storage_load_history(200)     — replay into chat log
```

SQLite is configured with WAL journal mode and `NORMAL` fsync so writes are fast without risking data loss on crash.

---

## Message Storage

TorChat persists all chat messages to a local SQLite database using `storage.c`. Each user gets their own database file named after their nickname — so logging in as `Alice` creates `Alice.db` in the working directory, keeping histories fully separated on shared machines.

### Schema

```sql
CREATE TABLE messages (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,   -- Unix epoch seconds (UTC)
    sender    TEXT    NOT NULL,   -- peer nickname, or local user for sent messages
    peer_addr TEXT,               -- NULL for messages sent by the local user
    body      TEXT    NOT NULL
);
```

### Storage API

| Function | Purpose |
|---|---|
| `storage_open(path)` | Open/create the DB file |
| `storage_close()` | Flush and close |
| `storage_save_message(sender, addr, body)` | Insert one message |
| `storage_load_history(limit, cb, data)` | Replay last N messages oldest-first |
| `storage_load_peer_history(addr, limit, cb, data)` | Same, filtered to one peer |
| `storage_search(keyword, cb, data)` | Case-insensitive full-text search |
| `storage_message_count()` | Total rows stored |
| `storage_delete_before(cutoff)` | Prune messages older than a timestamp |

---

## Data Flow

```
App starts
  └─ backend_init("Alice")  →  storage_open("Alice.db")
  └─ run_ui()               →  storage_load_history(200) → chat_push() × N
                                                         → "--- history ---"
User sends message
  └─ backend_send_message() →  g_on_msg(-1) → chat_push()      [UI]
                            →  storage_save_message()           [DB]
                            →  send() to peers                  [Network]
Peer sends message
  └─ backend_poll()         →  g_on_msg(fd) → chat_push()      [UI]
                            →  storage_save_message()           [DB]
App closes
  └─ backend_shutdown()     →  storage_close()                  [DB flushes]
                            →  close() all sockets
```

---

## Roadmap

- [ ] Wire the Send button to `backend_send_message()` in the merged build
- [ ] Wire the Connect form to `backend_connect_to_peer()`
- [ ] Scrollable chat history in the UI
- [ ] Visual theming and polish
- [ ] Graceful nickname display in peer sidebar (currently shows fd number)
- [ ] Message framing / length-prefix to handle split TCP reads
- [ ] Track `peer_addr` (ip:port) per connection so storage can filter history per peer
- [ ] In-UI history search using `storage_search()`

---

## How to Contribute

1. **Fork** the repository and create a branch for your feature or fix:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Follow the coding style** — C11, `-Wall -Wextra`, zero warnings. Run `make` before committing to confirm a clean build.

3. **Keep layers separate.** Networking logic lives in `net.c` / `backend.c`; UI logic lives in `UI/main_ui.c`; persistence lives in `storage.c`. No layer should reach directly into another's internals — use the `backend_*` API and the callback interface.

4. **Test with two terminal windows** (or two machines) to verify any networking change end-to-end.

5. **Open a pull request** with a clear description of what changed and why.

### Key files to know before contributing

| File | What to know |
|---|---|
| `backend.h` | The public API surface — all UI↔backend interaction goes through here |
| `net.c` | Socket primitives only; no protocol logic here |
| `storage.h` | Full storage API — read this before touching persistence logic |
| `UI/main_ui.c` | UI callbacks (`ui_on_msg`, `ui_on_peer_connected`, etc.) are the integration seam |
| `event_loop.h` | Contains both blocking (CLI) and non-blocking (UI) loop variants |

---
