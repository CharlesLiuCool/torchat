#include "peer.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Initialize the peer list.
 * Simply sets the count to 0 — no active connections yet.
 */
void peer_list_init(peer_list *pl) {
    pl->count = 0;
}

/*
 * Add a new peer (socket file descriptor) to the list.
 *
 * Returns:
 *   0  -> success
 *  -1  -> list is full (cannot add more peers)
 */
int peer_add(peer_list *pl, int fd) {
    // Prevent exceeding fixed capacity
    if (pl->count >= MAX_PEERS) return -1;

    // Append the new socket to the end of the array
    pl->fds[pl->count++] = fd;
    return 0;
}

/*
 * Remove a peer by index.
 *
 * Key idea:
 * - Close the socket to free OS resources
 * - Replace removed element with the last element (O(1) deletion)
 */
void peer_remove(peer_list *pl, int index) {
    // Bounds check — ignore invalid indices
    if (index < 0 || index >= pl->count) return;

    // Close the socket (important: releases system resource)
    close(pl->fds[index]);

    // Move last element into this slot (constant-time removal)
    pl->fds[index] = pl->fds[pl->count - 1];

    // Decrease count
    pl->count--;
}

/*
 * Broadcast a message to ALL connected peers.
 *
 * Arguments:
 *   msg -> pointer to message buffer
 *   len -> number of bytes to send
 */
void peer_broadcast(peer_list *pl, const char *msg, int len) {
    for (size_t i = 0; i < pl->count; i++) {
        int n = send(pl->fds[i], msg, len, 0);

        // If send fails, print error (but do not remove peer here)
        if (n < 0) perror("send");
    }
}

/*
 * Broadcast a message to all peers EXCEPT one.
 *
 * Useful for:
 * - Relaying messages without echoing back to sender
 */
void peer_broadcast_except(peer_list *pl, int exclude_fd, const char *msg, int len) {
    for (size_t i = 0; i < pl->count; i++) {
        int fd = pl->fds[i];

        // Skip the excluded socket
        if (fd == exclude_fd) continue;

        int n = send(fd, msg, len, 0);
        if (n < 0) perror("send");
    }
}

/*
 * Cleanup all peers.
 *
 * Closes every socket and resets the list.
 * Typically called during program shutdown.
 */
void peer_list_cleanup(peer_list *pl) {
    for (size_t i = 0; i < pl->count; i++) {
        close(pl->fds[i]);  // close each active connection
    }

    // Reset list to empty state
    pl->count = 0;
}