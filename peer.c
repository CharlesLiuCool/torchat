/*
peer.c
This is responsible for: 
- tracking connected peers
- adding/removing peers
- broadcasting messages to peers
*/
#include "peer.h"

void peer_list_init(peer_list *pl) {
    pl->count = 0;
}

int peer_add(peer_list *pl, int fd) {
    if (pl->count >= MAX_PEERS) {
        return -1;  // full
    }

    pl->fds[pl->count] = fd;
    pl->count++;
    return 0;
}

void peer_remove(peer_list *pl, int index) {
    close(pl->fds[index]);

    // move last peer into this slot
    pl->fds[index] = pl->fds[pl->count - 1];
    pl->count--;
}

/*
peer_broadcast()
Sends a message to all connected peers in the network
- pl: pointer to peer list
- msg: buffer containing the message to send
- len: number of bytes to send

The function attempts to send the message to every peer socket.
Errors from send() are printed but do not stop the broadcast.
*/
void peer_broadcast(peer_list *pl, const char *msg, int len) {
    for (int i = 0; i < pl->count; i++) {
        int fd = pl->fds[i];

        int n = send(fd, msg, len, 0);
        if (n < 0) {
            perror("send");
        }
    }
}

/*
peer_broadcast_except()
Sends a message to all peers except one specified socket.
- pl: pointer to the peer list
- exclude_fd: socket descriptor to skip
- msg: buffer containing the message
- len: number of bytes to send

Used for relaying messages without echoing them back to the original sender.
Errors from send() are logged.
*/
void peer_broadcast_except(peer_list *pl, int exclude_fd, const char *msg, int len) {
    for (int i = 0; i < pl->count; i++) {
        int fd = pl->fds[i];

        if (fd == exclude_fd) {
            continue;
        }

        int n = send(fd, msg, len, 0);
        if (n < 0) {
            perror("send");
        }
    }
}