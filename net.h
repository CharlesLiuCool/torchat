#pragma once

int create_listener(int port);
int accept_peer(int listener_fd);
int connect_to_peer(const char* ip, int port);