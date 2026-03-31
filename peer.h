#ifndef PEER_H
#define PEER_H

#define MAX_PEERS 32

typedef struct {
    int fds[MAX_PEERS];
    int count;
} peer_list;

void peer_list_init(peer_list *pl);
int peer_add(peer_list *pl, int fd);
void peer_remove(peer_list *pl, int index);
void peer_broadcast(peer_list *pl, const char *msg, int len);
void peer_broadcast_except(peer_list *pl, int exclude_fd, const char *msg, int len);


#endif
