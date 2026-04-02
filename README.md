# Tutorial for Cooperation

## 🎨 Frontend & UI Development Guide (C99)

This directory contains the standalone **UI Sandbox** for the TorChat project. To maintain modularity and avoid breaking the backend networking logic, the UI is currently developed in this isolated environment using **Raylib** and **Raygui**, strictly following the **C99** standard.

### 1. How to Build & Run (UI Sandbox)
To preview the current UI prototype without affecting the backend build, follow these steps:

1. **Navigate to the UI directory:**
   ```bash
   cd UI
   ```
2. **Compile using GCC (MinGW/Windows):**
   ```bash
   gcc main_ui.c -o chat_ui.exe -O1 -Wall -std=c99 -I include/ -L lib/ -lraylib -lopengl32 -lgdi32 -lwinmm
   ```
3. **Run the executable:**
   ```bash
   ./chat_ui.exe
   ```

### 2. UI Layout Components
The current `main_ui.c` implements a **Single-process non-blocking loop**:
* **Sidebar (Peer List):** Displays active nodes in the P2P network.
* **Central Panel (Chat History):** Reserved for rendering the message stream.
* **Bottom Bar (Input Area):** Features a functional text box and a "SEND" button for user interaction.

---

## 🔗 Integration Points (Action Items for Discussion)

To successfully merge the Frontend with the Backend (`net.c`, `peer.c`, `event_loop.c`), we need to align on the following technical interfaces during our next meeting:

### A. Shared Data Structures
The UI needs to read data from the backend. We should define a thread-safe (or globally accessible) way to access:
* **`active_peers` List:** A structure containing the display names and IP addresses of connected peers so the UI can update the sidebar.
* **`message_buffer`:** A ring buffer or linked list where `net.c` stores incoming messages. The UI will poll this buffer every frame to update the chat history.

### B. Event Loop Unification
Since the project is restricted to a **single-process**, we cannot run the UI and the Network on separate threads. 
* **Proposal:** We must modify the `poll()` timeout in `event_loop.c` to `0` (non-blocking). 
* **Goal:** The main loop should look like: `Run UI Frame` -> `Poll Network Events` -> `Update UI State` -> `Repeat`.

### C. Action Callbacks
We need to finalize the function signatures for the following UI-triggered actions:
* `void ui_send_message(const char* text);` // Triggered when the "SEND" button is clicked.
* `void ui_connect_to_peer(const char* ip);` // Triggered when adding a new peer manually.

---

### 📝 Next Steps for UI Task
- [ ] Implement a scrolling mechanism for long chat histories.
- [ ] Connect the `inputText` buffer to the actual `protocol_send` logic once the backend API is ready.
- [ ] Style the UI to match the "TorChat" theme.