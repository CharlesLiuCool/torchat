# TorChat

A peer-to-peer chat application written in C with a graphical interface. TorChat lets users chat directly with each other over a local network or the internet — no central server required. Peers connect by exchanging IP addresses and port numbers, and all messages flow directly between clients.

> **Course project** — CPTS 360, Spring 2026

---

## Features

- **Serverless P2P messaging** — peers connect directly via TCP; no relay or sign-in server needed
- **Graphical UI** — 900×620 window built with [Raylib](https://www.raylib.com/) and [raygui](https://github.com/raysan5/raygui), running at 60 FPS
- **Multi-peer support** — up to 64 simultaneous peer connections
- **Nickname handshake** — peers automatically exchange display names on connect via a lightweight `/nick` / `/getnick` protocol
- **Broadcast messaging** — every message sent is delivered to all connected peers
- **Local echo** — senders see their own messages in the chat log immediately
- **Scrollable chat history** — ring buffer holds up to 512 lines; auto-scrolls to the latest message
- **Live connection panel** — sidebar shows connected peers; system messages announce connects and disconnects
- **Non-blocking I/O** — `select()`-based networking with a zero-timeout poll keeps the UI responsive at all times
- **Both listen and dial** — a node can accept incoming connections, dial out to peers, or do both simultaneously

---

## Project Structure

```
torchat/
├── main.c            Entry point — reads nickname & port, initialises backend, launches UI
├── backend.c / .h    Core state machine: peer table, protocol logic, callback dispatch
├── net.c / .h        Raw socket layer: create listener, accept, connect, set non-blocking
├── peer.c / .h       Peer-list primitives and broadcast helpers
├── event_loop.c / .h Both a blocking CLI loop and a non-blocking UI-compatible poll loop
├── Makefile          Cross-platform build (macOS & Linux)
└── UI/
    ├── main_ui.c     Raylib/raygui rendering, input handling, backend callback hooks
    └── raygui.h      Single-header immediate-mode GUI library (bundled)
```

---

## Dependencies

| Dependency | Purpose | How to install |
|---|---|---|
| **GCC** (C11) | Compiler | Ships with most Linux distros; `brew install gcc` on macOS |
| **Raylib** | Window, rendering, input | `sudo apt install libraylib-dev` · `brew install raylib` |
| **raygui** | Immediate-mode GUI widgets | Bundled as `UI/raygui.h` — no install needed |
| **POSIX sockets** | Networking | Standard on Linux & macOS |

> **Windows:** The UI sandbox under `UI/` can be built with MinGW (see below). The main Makefile targets Linux and macOS only.

---

## How to Build & Run

### Linux / macOS (full application)

```bash
# 1. Install Raylib (pick your platform)
sudo apt install libraylib-dev      # Debian / Ubuntu
brew install raylib                  # macOS

# 2. Clone and build
git clone https://github.com/CharlesLiuCool/torchat.git
cd torchat
make

# 3. Run
./torchat
```

On startup you will be prompted for:
- **Nickname** — your display name shown to other peers
- **Listen port** — the TCP port to accept incoming connections on (enter `0` to skip listening and dial-out only)

### Windows (UI sandbox only)

The `UI/` directory contains a standalone prototype that can be built with MinGW without the backend:

```bash
cd UI
gcc main_ui.c -o chat_ui.exe -O1 -Wall -std=c99 -I include/ -L lib/ -lraylib -lopengl32 -lgdi32 -lwinmm
./chat_ui.exe
```

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

TorChat runs in a **single process** with no threads. The UI's render loop drives everything:

```
while window open:
    backend_poll()        ← non-blocking select() on all sockets
    render UI frame       ← Raylib BeginDrawing / EndDrawing
    handle user input     ← text box, send button, connect form
```

The backend exposes a clean callback interface so the UI layer never touches sockets directly:

```c
backend_init(nickname, on_message, on_connected, on_disconnected);
backend_start_listening(port);       // optional — become a server
backend_connect_to_peer(ip, port);   // dial out to a peer
backend_send_message(text);          // broadcast to all peers
backend_poll();                      // call every frame
```

---

## Roadmap

- [ ] Wire the Send button to `backend_send_message()` in the merged build
- [ ] Wire the Connect form to `backend_connect_to_peer()`
- [ ] Scrollable chat history in the UI
- [ ] Visual theming and polish
- [ ] Graceful nickname display in peer sidebar (currently shows fd number)
- [ ] Message framing / length-prefix to handle split TCP reads

---

## How to Contribute

1. **Fork** the repository and create a branch for your feature or fix:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Follow the coding style** — C11, `-Wall -Wextra`, zero warnings. Run `make` before committing to confirm a clean build.

3. **Keep layers separate.** Networking logic lives in `net.c` / `backend.c`; UI logic lives in `UI/main_ui.c`. Neither layer should reach directly into the other's internals — use the `backend_*` API or the callback interface.

4. **Test with two terminal windows** (or two machines) to verify any networking change end-to-end.

5. **Open a pull request** with a clear description of what changed and why.

### Key files to know before contributing

| File | What to know |
|---|---|
| `backend.h` | The public API surface — all UI↔backend interaction goes through here |
| `net.c` | Socket primitives only; no protocol logic here |
| `UI/main_ui.c` | UI callbacks (`ui_on_msg`, `ui_on_peer_connected`, etc.) are the integration seam |
| `event_loop.h` | Contains both blocking (CLI) and non-blocking (UI) loop variants |
