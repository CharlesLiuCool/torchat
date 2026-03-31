/*
net.c
This is a listener, a network entry point. Its responsible for
- opening TCP ports
- accepting new peers.
*/

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

int create_listener(int port) {
    int fd; // socket descriptor
    struct sockaddr_in addr; // ip address + port

    // create socket
    // - AF_INET: address family -> IPv4 networking
    // - SOCK_STREAM: socket type -> TCP
    // - 0: protocol selection, default is 0 for TCP
    fd = socket(AF_INET, SOCK_STREAM, 0);

    /*
    At create socket, the kernel:
    - allocates a socket object
    - reserves buffers
    - returns a descriptor
    If this fails, fd < 0
    */

    // create socket fails
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    // option value
    int opt = 1;

    // a socket is a configurable kernel object (like a device with settings)
    // - fd: the socket we are configuring
    // - SOL_SOCKET: we are configuring the socket layer itself
    //      - with generic socket settings
    // - SO_REUSEADDR: whether a recently used port can be reused
    //      - this fixes the annoying "address already in use" issue on quick reruns
    // - &opt: a pointer to the option value (not the value itself)
    // - sizeof(opt): how many bytes to read (4 for int usually) 
    //      - we pass the address and size here since setsocketopt takes generic types, not just integers
    //      - this sort of structure works for not just integers, but structs and stuff
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // type has to match up with IPv4 address
    addr.sin_family = AF_INET;

    // sin_addr: IP address
    // INADDR_ANY: listen on all interfaces (WiFi IP, Ethernet IP, localhost, ...)
    addr.sin_addr.s_addr = INADDR_ANY;
    // local only option: inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // different CPUs store bytes differently (endian)
    // convert your CPU format to network format
    // - htons: host to network
    addr.sin_port = htons(port);

    // bind the socket with the IP/port
    if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // transform the socket into a listening socket
    // 10 is max pending connections (waits for accept() and queues incoming connections)
    if (listen(fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    // Non-blocking socket
    // - default sockets are blocking
    // - this means accept() freezes the program
    // - nonblocking means operations return immediately
    // - this enables select()/poll() event loop
    fcntl(fd, F_SETFL, O_NONBLOCK);

    printf("Listening on port %d\n", port);

    return fd;
}

int accept_peer(int listener_fd) {
    struct sockaddr_in peer_addr; // peer IP and port
    socklen_t addr_len = sizeof(peer_addr);

    // Ask the OS: do you have any waiting connections?
    // Case 1: Yes
    // - kernel removes it from queue and creates a new socket, then returns a descriptor
    // Case 2: No
    // - Nothing to do right now, accept() returns -1
    int peer_fd = accept(listener_fd, (struct sockaddr*) &peer_addr, &addr_len);

    // Nothing to do
    if (peer_fd < 0) {
        return -1;
    }

    // make the peer socket nonblocking
    fcntl(peer_fd, F_SETFL, O_NONBLOCK);

    printf("New peer connected!\n");

    return peer_fd;
}