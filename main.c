/*
main.c
This is the main program.
*/

#include <stdio.h>
#include "net.h"
#include "event_loop.h"
#include "peer.h"

int main() {
    printf("Hello, World!\n");

    // 1. Create listener socket
    int listener = create_listener(5000);
    if (listener < 0) {
        fprintf(stderr, "Failed to create listener\n");
        return 1;
    }

    // 2. Create and initialize peer manager
    peer_list peers;
    peer_list_init(&peers);

    // 3. Run event loop
    run_event_loop(listener, &peers);

    return 0;
}
