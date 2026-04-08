#include "event_loop.h"
#include "peer.h"
#include "net.h"
#include "backend.h"    // <-- use backend handlers, no more handle_* functions
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static peer_list* g_peers = NULL;
static int g_listener_fd = -1;

static void make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// --------------------
// Blocking run
// --------------------
void run_event_loop(int listener_fd, peer_list* peers, const char* nickname) {
    (void)nickname; // unused

    g_peers = peers;
    g_listener_fd = listener_fd;

    fd_set read_fds;
    char buffer[512];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(listener_fd, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = listener_fd > STDIN_FILENO ? listener_fd : STDIN_FILENO;

        for (size_t i = 0; i < peers->count; i++) {
            int fd = peers->fds[i];
            FD_SET(fd, &read_fds);
            if (fd > max_fd) max_fd = fd;
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) { perror("select"); continue; }

        // Accept new peers
        if (FD_ISSET(listener_fd, &read_fds)) {
            int fd = accept_peer(listener_fd);
            if (fd >= 0) backend_notify_peer_connected(fd);
        }

        // Handle user input from stdin
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(buffer, sizeof(buffer), stdin)) {
                buffer[strcspn(buffer, "\n")] = '\0';
                backend_send_message(buffer);
            }
        }

        // Handle peer messages
        for (size_t i = 0; i < peers->count; i++) {
            int fd = peers->fds[i];
            if (FD_ISSET(fd, &read_fds)) {
                int n = recv(fd, buffer, sizeof(buffer)-1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    backend_notify_peer_message(fd, buffer, n);
                } else {
                    backend_notify_peer_disconnected(fd);
                }
            }
        }
    }
}

// --------------------
// Non-blocking poll
// --------------------
void poll_event_loop(void) {
    if (!g_peers || g_listener_fd < 0) return;

    fd_set read_fds;
    char buffer[512];

    FD_ZERO(&read_fds);
    FD_SET(g_listener_fd, &read_fds);
    int max_fd = g_listener_fd;

    for (size_t i = 0; i < g_peers->count; i++) {
        int fd = g_peers->fds[i];
        FD_SET(fd, &read_fds);
        if (fd > max_fd) max_fd = fd;
    }

    struct timeval tv = {0, 0}; // non-blocking select
    int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
    if (ready < 0) { perror("select"); return; }

    // Accept new peers
    if (FD_ISSET(g_listener_fd, &read_fds)) {
        int fd = accept_peer(g_listener_fd);
        if (fd >= 0) backend_notify_peer_connected(fd);
    }

    // Handle peer messages
    for (size_t i = 0; i < g_peers->count; i++) {
        int fd = g_peers->fds[i];
        if (FD_ISSET(fd, &read_fds)) {
            int n = recv(fd, buffer, sizeof(buffer)-1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                backend_notify_peer_message(fd, buffer, n);
            } else {
                backend_notify_peer_disconnected(fd);
            }
        }
    }
}