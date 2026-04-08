#include "peer.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

void peer_list_init(peer_list *pl) {
    pl->count = 0;
}

int peer_add(peer_list *pl, int fd) {
    if (pl->count >= MAX_PEERS) return -1;
    pl->fds[pl->count++] = fd;
    return 0;
}

void peer_remove(peer_list *pl, int index) {
    if (index < 0 || index >= pl->count) return;

    close(pl->fds[index]);                     // close socket
    pl->fds[index] = pl->fds[pl->count - 1];  // move last to current
    pl->count--;
}

void peer_broadcast(peer_list *pl, const char *msg, int len) {
    for (size_t i = 0; i < pl->count; i++) {
        int n = send(pl->fds[i], msg, len, 0);
        if (n < 0) perror("send");
    }
}

void peer_broadcast_except(peer_list *pl, int exclude_fd, const char *msg, int len) {
    for (size_t i = 0; i < pl->count; i++) {
        int fd = pl->fds[i];
        if (fd == exclude_fd) continue;
        int n = send(fd, msg, len, 0);
        if (n < 0) perror("send");
    }
}

// ---------------------
// Cleanup
// ---------------------
void peer_list_cleanup(peer_list *pl) {
    for (size_t i = 0; i < pl->count; i++) {
        close(pl->fds[i]);
    }
    pl->count = 0;
}