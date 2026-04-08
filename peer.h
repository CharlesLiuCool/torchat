#ifndef PEER_H
#define PEER_H

#include <stddef.h>

#define MAX_PEERS 32

typedef struct {
    int fds[MAX_PEERS];
    size_t count;
} peer_list;

// ---------------------
// Peer list management
// ---------------------
void peer_list_init(peer_list *pl);
int peer_add(peer_list *pl, int fd);
void peer_remove(peer_list *pl, int index);

// ---------------------
// Broadcast
// ---------------------
void peer_broadcast(peer_list *pl, const char *msg, int len);
void peer_broadcast_except(peer_list *pl, int exclude_fd, const char *msg, int len);

// ---------------------
// Cleanup
// ---------------------
void peer_list_cleanup(peer_list *pl); // closes all fds and resets count

#endif