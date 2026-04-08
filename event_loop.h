#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include "peer.h"

// =======================
// Blocking CLI event loop
// =======================
void run_event_loop(int listener_fd, peer_list* peers, const char* nickname);

// =======================
// Non-blocking poll loop
// Call repeatedly from UI loop
// =======================
void poll_event_loop(void);

// =======================
// Peer connection / user input handlers
// Should be implemented in backend/app
// =======================
void handle_peer_message(int fd, const char* msg, size_t len);
void handle_peer_connected(int fd);
void handle_peer_disconnected(int fd);
void handle_user_input(const char* input);

#endif