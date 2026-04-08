#include "net.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int create_listener(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 10) < 0) {
        perror("listen"); close(fd); return -1;
    }

    set_nonblocking(fd);
    printf("[net] Listening on port %d\n", port);
    return fd;
}

int accept_peer(int listener_fd)
{
    struct sockaddr_in peer_addr;
    socklen_t addr_len = sizeof(peer_addr);

    int fd = accept(listener_fd, (struct sockaddr*)&peer_addr, &addr_len);
    if (fd < 0) return -1;

    set_nonblocking(fd);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof(ip));
    printf("[net] Accepted connection from %s\n", ip);
    return fd;
}

int connect_to_peer(const char* ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton"); close(fd); return -1;
    }
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return -1;
    }

    set_nonblocking(fd);
    printf("[net] Connected to %s:%d\n", ip, port);
    return fd;
}