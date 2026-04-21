#pragma once
#include <stddef.h>

/*
 * Callbacks
 *   on_message      : called when a chat message arrives (fd == -1 means local/self)
 *   on_connected    : called when a new peer is added
 *   on_disconnected : called when a peer is removed
 */
typedef void (*message_cb)(int fd, const char* msg);
typedef void (*peer_cb)(int fd);

/* Startup / teardown */
void backend_init(const char* nickname,
                  message_cb  on_message,
                  peer_cb     on_connected,
                  peer_cb     on_disconnected);

static int       g_listening_port = 0;

int  backend_start_listening(int port);   /* returns listener fd, or -1 */
int  backend_connect_to_peer(const char* ip, int port); /* returns peer fd, or -1 */
void backend_shutdown(void);

void backend_set_nickname(const char* nickname);
int  backend_get_port(void);

/* Called every frame from the UI to drive networking */
void backend_poll(void);

/* Send a chat message to all connected peers */
void backend_send_message(const char* msg);

/* Peer introspection */
size_t      backend_peer_count(void);
int         backend_peer_fd(size_t index);
const char* backend_peer_name(size_t index);
