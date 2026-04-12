#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include "peer.h"

/*
 * Blocking CLI event loop.
 * Drives select() on the listener, stdin, and all peer sockets.
 * Call this instead of run_ui() for a headless / terminal build.
 */
void run_event_loop(int listener_fd, peer_list* peers, const char* nickname);

/*
 * Non-blocking poll shim.
 * In the graphical build, backend_poll() is called directly from run_ui()
 * every frame. poll_event_loop() simply forwards to backend_poll() so any
 * code that references it still links cleanly.
 */
void poll_event_loop(void);

#endif /* EVENT_LOOP_H */
