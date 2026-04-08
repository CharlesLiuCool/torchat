#include "backend.h"
#include "net.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

#define MAX_PEERS 64

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int  fd;
    char nickname[32];
} peer_info;

static peer_info g_peers[MAX_PEERS];
static size_t    g_peer_count  = 0;
static int       g_listener_fd = -1;
static char      g_nickname[32];

static message_cb g_on_msg        = NULL;
static peer_cb    g_on_connect    = NULL;
static peer_cb    g_on_disconnect = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static peer_info* find_peer(int fd)
{
    for (size_t i = 0; i < g_peer_count; i++)
        if (g_peers[i].fd == fd) return &g_peers[i];
    return NULL;
}

/* Send our nickname to fd */
static void send_nick(int fd)
{
    char msg[40];
    int  n = snprintf(msg, sizeof(msg), "/nick %s", g_nickname);
    send(fd, msg, (size_t)n, 0);
}

/* Remove peer at index i, close socket */
static void remove_peer(size_t i)
{
    int fd = g_peers[i].fd;
    if (g_on_disconnect) g_on_disconnect(fd);
    close(fd);
    g_peers[i] = g_peers[g_peer_count - 1];
    g_peer_count--;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void backend_init(const char*  nickname,
                  message_cb   on_msg,
                  peer_cb      on_connected,
                  peer_cb      on_disconnected)
{
    strncpy(g_nickname, nickname, sizeof(g_nickname) - 1);
    g_nickname[sizeof(g_nickname) - 1] = '\0';

    g_on_msg        = on_msg;
    g_on_connect    = on_connected;
    g_on_disconnect = on_disconnected;

    g_peer_count  = 0;
    g_listener_fd = -1;
}

int backend_start_listening(int port)
{
    g_listener_fd = create_listener(port);
    return g_listener_fd;
}

int backend_connect_to_peer(const char* ip, int port)
{
    if (g_peer_count >= MAX_PEERS) return -1;

    int fd = connect_to_peer(ip, port);
    if (fd < 0) return -1;

    g_peers[g_peer_count].fd = fd;
    strncpy(g_peers[g_peer_count].nickname, "UNKNOWN", 31);
    g_peer_count++;

    /* Handshake: announce ourselves, then ask for their nick */
    send_nick(fd);
    send(fd, "/getnick", 8, 0);

    if (g_on_connect) g_on_connect(fd);
    return fd;
}

/* ------------------------------------------------------------------ */
/* poll() — call every frame from the UI thread                        */
/* ------------------------------------------------------------------ */

void backend_poll(void)
{
    if (g_listener_fd < 0 && g_peer_count == 0) return;

    fd_set read_fds;
    FD_ZERO(&read_fds);

    int max_fd = -1;

    if (g_listener_fd >= 0) {
        FD_SET(g_listener_fd, &read_fds);
        if (g_listener_fd > max_fd) max_fd = g_listener_fd;
    }

    for (size_t i = 0; i < g_peer_count; i++) {
        int fd = g_peers[i].fd;
        FD_SET(fd, &read_fds);
        if (fd > max_fd) max_fd = fd;
    }

    struct timeval tv = {0, 0}; /* non-blocking */
    if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) <= 0) return;

    /* Accept new incoming connections */
    if (g_listener_fd >= 0 && FD_ISSET(g_listener_fd, &read_fds)) {
        if (g_peer_count < MAX_PEERS) {
            int fd = accept_peer(g_listener_fd);
            if (fd >= 0) {
                g_peers[g_peer_count].fd = fd;
                strncpy(g_peers[g_peer_count].nickname, "UNKNOWN", 31);
                g_peer_count++;

                /* Announce ourselves; the connector will reply with /nick */
                send_nick(fd);

                if (g_on_connect) g_on_connect(fd);
            }
        }
    }

    /* Receive data from existing peers */
    char buf[512];
    for (size_t i = 0; i < g_peer_count; /* manual increment */) {
        int fd = g_peers[i].fd;
        if (!FD_ISSET(fd, &read_fds)) { i++; continue; }

        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            /* --- Handle protocol messages --- */
            if (strncmp(buf, "/nick ", 6) == 0) {
                peer_info* p = find_peer(fd);
                if (p) {
                    /* Truncate nick to 31 chars, always NUL-terminate */
                    size_t nlen = strlen(buf + 6);
                    if (nlen > 31) nlen = 31;
                    memcpy(p->nickname, buf + 6, nlen);
                    p->nickname[nlen] = '\0';
                }
            } else if (strcmp(buf, "/getnick") == 0) {
                send_nick(fd);
            } else {
                if (g_on_msg) g_on_msg(fd, buf);
            }
            i++;
        } else {
            /* n == 0 → clean close; n < 0 → error; either way remove peer */
            remove_peer(i);
            /* don't increment i; the slot now holds a different peer */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Send a chat message to all peers                                    */
/* ------------------------------------------------------------------ */

void backend_send_message(const char* msg)
{
    char formatted[544];
    int  n = snprintf(formatted, sizeof(formatted), "[%s]: %s", g_nickname, msg);

    /* Echo locally */
    if (g_on_msg) g_on_msg(-1, formatted);

    /* Broadcast */
    for (size_t i = 0; i < g_peer_count; i++)
        send(g_peers[i].fd, formatted, (size_t)n, 0);
}

/* ------------------------------------------------------------------ */
/* Shutdown                                                            */
/* ------------------------------------------------------------------ */

void backend_shutdown(void)
{
    if (g_listener_fd >= 0) { close(g_listener_fd); g_listener_fd = -1; }
    for (size_t i = 0; i < g_peer_count; i++) close(g_peers[i].fd);
    g_peer_count = 0;
}

/* ------------------------------------------------------------------ */
/* Peer introspection                                                  */
/* ------------------------------------------------------------------ */

size_t      backend_peer_count(void)        { return g_peer_count; }
int         backend_peer_fd(size_t i)       { return i < g_peer_count ? g_peers[i].fd       : -1;   }
const char* backend_peer_name(size_t i)     { return i < g_peer_count ? g_peers[i].nickname : NULL; }