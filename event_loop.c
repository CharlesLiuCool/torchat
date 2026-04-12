#include "event_loop.h"
#include "peer.h"
#include "net.h"
#include "backend.h"
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static peer_list* g_peers       = NULL;
static int        g_listener_fd = -1;

/* ------------------------------------------------------------------ */
/* Blocking CLI event loop                                             */
/* ------------------------------------------------------------------ */

void run_event_loop(int listener_fd, peer_list* peers, const char* nickname)
{
    (void)nickname; /* unused — backend owns the nickname */

    g_peers       = peers;
    g_listener_fd = listener_fd;

    fd_set read_fds;
    char   buffer[512];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(listener_fd,  &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = listener_fd > STDIN_FILENO ? listener_fd : STDIN_FILENO;

        for (size_t i = 0; i < peers->count; i++) {
            int fd = peers->fds[i];
            FD_SET(fd, &read_fds);
            if (fd > max_fd) max_fd = fd;
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) { perror("select"); continue; }

        /* Accept new incoming peers */
        if (FD_ISSET(listener_fd, &read_fds)) {
            int fd = accept_peer(listener_fd);
            if (fd >= 0) {
                peer_add(peers, fd);
                /* backend tracks its own peer table separately */
            }
        }

        /* User typed something on stdin */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(buffer, sizeof(buffer), stdin)) {
                buffer[strcspn(buffer, "\n")] = '\0';
                backend_send_message(buffer);
            }
        }

        /* Data arriving from connected peers */
        for (size_t i = 0; i < peers->count; ) {
            int fd = peers->fds[i];
            if (!FD_ISSET(fd, &read_fds)) { i++; continue; }

            int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                /* Route through the backend so protocol messages are handled */
                (void)buffer; /* backend_poll() in the UI path handles this */
                i++;
            } else {
                /* n == 0: clean close; n < 0: error */
                peer_remove(peers, (int)i);
                /* don't increment — slot is now occupied by the last peer */
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Non-blocking poll — called every frame from the UI loop            */
/* ------------------------------------------------------------------ */

void poll_event_loop(void)
{
    /*
     * In the UI build the real networking is driven by backend_poll()
     * which is called directly from run_ui() every frame.
     * This function exists as a compatibility shim for any code that
     * still references it; all socket work is delegated to the backend.
     */
    backend_poll();
}
