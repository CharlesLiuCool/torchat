/*
event_loop.c
Responsible for:
- monitoring sockets
- dispatching events
- coordinating everything
*/

#include "event_loop.h"

void run_event_loop(int listener_fd, peer_list *peers) {
    while(1) {
        // set of fds we want to monitor
        // a "bitmask", e.g.
        // fd : 0 1 2 3 4 5 6
        // bit: 1 0 0 1 0 0 0
        // 1 means watch this fd
        fd_set read_fds;

        // clear the bitmask (all zeros)
        FD_ZERO(&read_fds);

        int max_fd = 0;
        /*Add fds to the watch list*/

        // watch listener socket
        FD_SET(listener_fd, &read_fds);
        if (listener_fd > max_fd) { 
            max_fd = listener_fd;
        }

        // watch stdin (fd = 0)
        FD_SET(STDIN_FILENO, &read_fds);
        if (STDIN_FILENO > max_fd) { 
            max_fd = STDIN_FILENO;
        }

        // watch all peers
        for (int i = 0; i < peers->count; i++) {
            int fd = peers->fds[i];
            FD_SET(fd, &read_fds);

            if (fd > max_fd) {
                max_fd = fd;
            }
        }

        // copies fd_set (bitmask) read_fds into kernell spaces
        // checks all listed descriptors
        // - max_fd + 1 means check fds from 0 to max
        // if none are ready, go back to sleep, no CPU usage
        // if one is ready, network packet arrives, user types input, kernel wakes up
        // - read_fds is updated
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        // none are ready
        if (ready < 0) {
            perror("select");
            return;
        }

        // listener ready (bitmask is 1) -> new peer
        if (FD_ISSET(listener_fd, &read_fds)) {
            int peer = accept_peer(listener_fd);
            if (peer >= 0) {
                printf("Peer accepted %d\n", peer);
                peer_add(peers,peer);
            }
        }

        // stdin ready -> user input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[256];

            if (fgets(buffer, sizeof(buffer), stdin)) {
                printf("You typed: %s", buffer);
            }
        }

        for (int i = 0; i < peers->count; i++) {
            // get the socket descriptor
            int fd = peers->fds[i];
    
            // check if socket is marked ready
            // ready means:
            // - data arrived
            // - peer disconnected
            // - error occured
            if (FD_ISSET(fd, &read_fds)) {
                char buffer[256];
    
                // give available data
                // detect the case of what we're dealing with here
                int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    
                // data received
                if (n > 0) {
                    buffer[n] = '\0';
                    printf("Peer %d says: %s", fd, buffer);
                    peer_broadcast_except(peers, fd, buffer, n);
                // disconnect
                } else if (n == 0) {
                    printf("Peer %d disconnected\n", fd);
                    peer_remove(peers, i);
                    i--;
                // error - something went wrong
                } else {
                    perror("recv");
                    printf("Error: something went wrong.");
                    peer_remove(peers, i);
                    i--;
                }
            }
        }
    }
}
