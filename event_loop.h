#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>

#include "peer.h"
#include "net.h"

void run_event_loop(int listener_fd, peer_list *peers);

#endif