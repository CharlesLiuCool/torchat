#ifndef NET_H
#define NET_H

int create_listener(int port);
int accept_peer(int listener_fd);

#endif