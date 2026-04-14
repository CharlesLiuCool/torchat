#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "raylib.h"
#include "../backend.h"
#include "../storage.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

/* ------------------------------------------------------------------ */
/* Per-peer chat log                                                   */
/* ------------------------------------------------------------------ */

#define MAX_PEERS        64
#define MAX_CHAT_LINES   512
#define MAX_LINE_LENGTH  512

typedef struct {
    int  fd;                                      /* -1 = system/global log  */
    char addr[64];                                /* "ip:port" for history   */
    char lines[MAX_CHAT_LINES][MAX_LINE_LENGTH];
    int  count;
    int  scroll;
    int  unread;                                  /* unread badge count      */
} peer_chat_t;

/* Slot 0 is always the system/global log (fd == -1).
   Slots 1..MAX_PEERS are per-peer conversations.            */
static peer_chat_t g_chats[MAX_PEERS + 1];
static int         g_chat_count = 1;   /* slot 0 always exists */
static int         g_active     = 0;   /* currently viewed slot */

static const char* peer_name_for_fd(int fd);

/* ------------------------------------------------------------------ */
/* Chat helpers                                                        */
/* ------------------------------------------------------------------ */

static peer_chat_t* chat_for_fd(int fd)
{
    for (int i = 0; i < g_chat_count; i++)
        if (g_chats[i].fd == fd)
            return &g_chats[i];
    return NULL;
}

static peer_chat_t* chat_get_or_create(int fd)
{
    peer_chat_t* c = chat_for_fd(fd);
    if (c) return c;
    if (g_chat_count > MAX_PEERS) return &g_chats[0]; /* fallback to system */
    c = &g_chats[g_chat_count++];
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    return c;
}

/* Look up by addr string (used when replaying history — fds don't persist) */
static peer_chat_t* chat_get_or_create_by_addr(const char* addr)
{
    for (int i = 1; i < g_chat_count; i++)
        if (strcmp(g_chats[i].addr, addr) == 0)
            return &g_chats[i];
    if (g_chat_count > MAX_PEERS) return &g_chats[0];
    peer_chat_t* c = &g_chats[g_chat_count++];
    memset(c, 0, sizeof(*c));
    c->fd = -2;   /* placeholder: no live fd yet */
    strncpy(c->addr, addr, sizeof(c->addr) - 1);
    return c;
}

static void chat_push_to(peer_chat_t* c, const char* msg)
{
    if (!c) return;
    if (c->count < MAX_CHAT_LINES) {
        strncpy(c->lines[c->count], msg, MAX_LINE_LENGTH - 1);
        c->lines[c->count][MAX_LINE_LENGTH - 1] = '\0';
        c->count++;
    } else {
        memmove(c->lines[0], c->lines[1],
                sizeof(c->lines[0]) * (MAX_CHAT_LINES - 1));
        strncpy(c->lines[MAX_CHAT_LINES - 1], msg, MAX_LINE_LENGTH - 1);
        c->lines[MAX_CHAT_LINES - 1][MAX_LINE_LENGTH - 1] = '\0';
    }
    /* auto-scroll to bottom */
    c->scroll = c->count;

    /* increment unread badge if this is not the active chat */
    if (c != &g_chats[g_active])
        c->unread++;
}

static void chat_push_system(const char* msg)
{
    /* Push to system log AND to the active peer log so context isn't lost */
    chat_push_to(&g_chats[0], msg);
}

/* ------------------------------------------------------------------ */
/* Backend callbacks                                                   */
/* ------------------------------------------------------------------ */

void ui_on_msg(int fd, const char* msg)
{
    peer_chat_t* c = chat_get_or_create(fd);
    chat_push_to(c, msg);
}

void ui_on_peer_connected(int fd)
{
    char line[MAX_LINE_LENGTH];
    snprintf(line, sizeof(line), "*** Peer %d connected", fd);
    chat_push_system(line);
    /* Create the peer's chat slot now so it appears in the sidebar */
    peer_chat_t* c = chat_get_or_create(fd);
    /* Stamp the addr from the backend name (usually "ip:port") */
    const char* name = peer_name_for_fd(fd);
    if (name) strncpy(c->addr, name, sizeof(c->addr) - 1);
}

void ui_on_peer_disconnected(int fd)
{
    char line[MAX_LINE_LENGTH];
    snprintf(line, sizeof(line), "*** Peer %d disconnected", fd);
    chat_push_system(line);
    /* Optionally push a note into the peer's own log too */
    peer_chat_t* c = chat_for_fd(fd);
    if (c) chat_push_to(c, "*** Disconnected");
}

/* ------------------------------------------------------------------ */
/* Peer name lookup by fd (backend only exposes by index)             */
/* ------------------------------------------------------------------ */

static const char* peer_name_for_fd(int fd)
{
    for (size_t i = 0; i < backend_peer_count(); i++)
        if (backend_peer_fd(i) == fd)
            return backend_peer_name(i);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Storage replay                                                      */
/* ------------------------------------------------------------------ */

static int replay_cb(const chat_message_t* msg, void* userdata)
{
    (void)userdata;
    /* Route by peer_addr if present (fd won't survive restarts anyway) */
    peer_chat_t* c = (msg->peer_addr && msg->peer_addr[0] != '\0')
                     ? chat_get_or_create_by_addr(msg->peer_addr)
                     : &g_chats[0];
    chat_push_to(c, msg->body);
    return 0;
}

/* ------------------------------------------------------------------ */
/* run_ui                                                              */
/* ------------------------------------------------------------------ */

void run_ui(void)
{
    const int W = 900, H = 620;
    InitWindow(W, H, "TorChat");
    SetTargetFPS(60);

    /* Initialise system log slot */
    g_chats[0].fd = -1;
    chat_push_to(&g_chats[0], "--- TorChat system log ---");

    /* Replay history */
    storage_load_history(200, replay_cb, NULL);

    /* Input state */
    char msg_buf[256] = {0};
    char ip_buf[64]   = {"127.0.0.1"};
    char port_buf[12] = {"9000"};
    bool msg_edit     = false;
    bool ip_edit      = false;
    bool port_edit    = false;

    /* Layout constants */
    const int SIDEBAR_W  = 210;
    const int CHAT_X     = SIDEBAR_W + 10;
    const int CHAT_Y     = 10;
    const int CHAT_W     = W - CHAT_X - 5;
    const int CHAT_H     = 470;
    const int LINE_H     = 18;
    const int VISIBLE    = (CHAT_H - 10) / LINE_H;

    while (!WindowShouldClose()) {
        /* ---- Network tick ---- */
        backend_poll();

        /* ---- Active chat pointer ---- */
        peer_chat_t* active_chat = &g_chats[g_active];

        /* ---- Mouse-wheel scroll ---- */
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            active_chat->scroll -= (int)wheel * 3;
            if (active_chat->scroll < 0)
                active_chat->scroll = 0;
            if (active_chat->scroll > active_chat->count)
                active_chat->scroll = active_chat->count;
        }

        /* ---- Enter to send (to the selected peer) ---- */
        if (msg_edit && IsKeyPressed(KEY_ENTER) && msg_buf[0] != '\0') {
            if (g_active > 0) {
                /* NOTE: backend only supports broadcast. If you add
                 * backend_send_message_to(int fd, const char*) later,
                 * replace this call. For now all peers receive the message. */
                backend_send_message(msg_buf);
                char echo[MAX_LINE_LENGTH];
                snprintf(echo, sizeof(echo), "[you] %s", msg_buf);
                chat_push_to(active_chat, echo);
            } else {
                chat_push_to(&g_chats[0], "*** Select a peer before sending.");
            }
            msg_buf[0] = '\0';
        }

        BeginDrawing();
        ClearBackground(GetColor(0x1a1a2eff));

        /* ============================================================
         * LEFT SIDEBAR — peer conversation list + connect form
         * ============================================================ */
        DrawRectangle(5, 5, SIDEBAR_W, H - 10, GetColor(0x12122aff));
        DrawRectangleLines(5, 5, SIDEBAR_W, H - 10, GetColor(0x3333aaff));

        GuiLabel((Rectangle){15, 12, SIDEBAR_W - 20, 20}, "# CONVERSATIONS");

        /* Draw each chat slot as a clickable row */
        int row_y = 36;
        for (int i = 0; i < g_chat_count; i++) {
            Rectangle row = {10, row_y, SIDEBAR_W - 10, 26};
            bool hovered  = CheckCollisionPointRec(GetMousePosition(), row);
            bool selected = (i == g_active);

            Color bg = selected  ? GetColor(0x2a2a6aff) :
                       hovered   ? GetColor(0x1e1e4aff) :
                                   GetColor(0x12122aff);
            DrawRectangleRec(row, bg);
            if (selected)
                DrawRectangleLinesEx(row, 1, GetColor(0x5555ccff));

            /* Label */
            char label[48];
            if (g_chats[i].fd == -1) {
                snprintf(label, sizeof(label), "System log");
            } else {
                /* Prefer live backend name, fall back to stored addr, then fd */
                const char* name = (g_chats[i].fd >= 0)
                                   ? peer_name_for_fd(g_chats[i].fd)
                                   : NULL;
                if (!name || name[0] == '\0') name = g_chats[i].addr;
                snprintf(label, sizeof(label), "Peer %s",
                         (name && name[0]) ? name : TextFormat("%d", g_chats[i].fd));
            }
            DrawText(label, row.x + 6, row.y + 6, 13,
                     selected ? WHITE : GetColor(0xaaaadbff));

            /* Unread badge */
            if (g_chats[i].unread > 0) {
                char badge[8];
                snprintf(badge, sizeof(badge), "%d", g_chats[i].unread);
                int bw = MeasureText(badge, 11) + 8;
                DrawRectangle((int)(row.x + row.width - bw - 4),
                              (int)(row.y + 5), bw, 16,
                              GetColor(0xcc3333ff));
                DrawText(badge, (int)(row.x + row.width - bw),
                         (int)(row.y + 7), 11, WHITE);
            }

            if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                g_active = i;
                g_chats[i].unread = 0;    /* clear badge on open */
            }

            row_y += 30;
            if (row_y > H - 180) break;   /* don't overflow into connect form */
        }

        /* ---- Connect section ---- */
        int conn_y = H - 165;
        DrawLine(10, conn_y, SIDEBAR_W, conn_y, GetColor(0x3333aaff));
        GuiLabel((Rectangle){15, conn_y + 6, SIDEBAR_W - 20, 18}, "CONNECT TO PEER");

        GuiLabel((Rectangle){15, conn_y + 28, 30, 18}, "IP:");
        if (GuiTextBox((Rectangle){50, conn_y + 26, SIDEBAR_W - 55, 22},
                       ip_buf, sizeof(ip_buf), ip_edit))
            ip_edit = !ip_edit;

        GuiLabel((Rectangle){15, conn_y + 56, 40, 18}, "Port:");
        if (GuiTextBox((Rectangle){60, conn_y + 54, 60, 22},
                       port_buf, sizeof(port_buf), port_edit))
            port_edit = !port_edit;

        if (GuiButton((Rectangle){125, conn_y + 54, 75, 22}, "Connect")) {
            int p = atoi(port_buf);
            if (p > 0 && ip_buf[0] != '\0') {
                int fd = backend_connect_to_peer(ip_buf, p);
                if (fd < 0) {
                    chat_push_system("*** Connection failed");
                } else {
                    /* Switch to the new peer's chat immediately */
                    peer_chat_t* nc = chat_get_or_create(fd);
                    for (int i = 0; i < g_chat_count; i++) {
                        if (&g_chats[i] == nc) { g_active = i; break; }
                    }
                }
            }
        }

        /* ============================================================
         * CHAT AREA — shows the active conversation
         * ============================================================ */
        DrawRectangle(CHAT_X, CHAT_Y, CHAT_W, CHAT_H, GetColor(0x0e0e20ff));
        DrawRectangleLines(CHAT_X, CHAT_Y, CHAT_W, CHAT_H, GetColor(0x3333aaff));

        /* Header bar showing who we're talking to */
        {
            const char* title = (active_chat->fd == -1)
                                ? "System Log"
                                : TextFormat("Chat with Peer %d", active_chat->fd);
            DrawRectangle(CHAT_X + 1, CHAT_Y + 1, CHAT_W - 2, 20,
                          GetColor(0x1e1e4aff));
            DrawText(title, CHAT_X + 8, CHAT_Y + 4, 13,
                     GetColor(0x88ccffff));
        }

        /* Message lines */
        int first = active_chat->scroll - VISIBLE;
        if (first < 0) first = 0;

        int y = CHAT_Y + 26;
        for (int i = first;
             i < active_chat->count && y < CHAT_Y + CHAT_H - LINE_H;
             i++) {
            Color col = WHITE;
            if (strncmp(active_chat->lines[i], "***", 3) == 0)
                col = GetColor(0x888888ff);
            else if (strncmp(active_chat->lines[i], "[you]", 5) == 0)
                col = GetColor(0xaaffaaff);
            else if (strncmp(active_chat->lines[i], "[", 1) == 0)
                col = GetColor(0x88ccffff);
            DrawText(active_chat->lines[i], CHAT_X + 6, y, 14, col);
            y += LINE_H;
        }

        if (active_chat->scroll < active_chat->count) {
            DrawText("(scroll down for more)",
                     CHAT_X + CHAT_W - 200, CHAT_Y + CHAT_H - 18,
                     11, GetColor(0x555577ff));
        }

        /* ============================================================
         * INPUT BAR
         * ============================================================ */
        int INPUT_Y = CHAT_Y + CHAT_H + 8;
        DrawRectangle(CHAT_X, INPUT_Y, CHAT_W, 50, GetColor(0x12122aff));
        DrawRectangleLines(CHAT_X, INPUT_Y, CHAT_W, 50, GetColor(0x3333aaff));

        bool can_send = (g_active > 0);   /* can't send from system log */

        if (can_send) {
            if (GuiTextBox((Rectangle){CHAT_X + 4, INPUT_Y + 6, CHAT_W - 90, 38},
                           msg_buf, sizeof(msg_buf), msg_edit))
                msg_edit = !msg_edit;

            if (GuiButton((Rectangle){CHAT_X + CHAT_W - 82, INPUT_Y + 6, 78, 38}, "SEND")) {
                if (msg_buf[0] != '\0') {
                    backend_send_message(msg_buf); /* broadcast; see note above */
                    char echo[MAX_LINE_LENGTH];
                    snprintf(echo, sizeof(echo), "[you] %s", msg_buf);
                    chat_push_to(active_chat, echo);
                    msg_buf[0] = '\0';
                }
            }
        } else {
            DrawText("Select a peer conversation to send a message.",
                     CHAT_X + 10, INPUT_Y + 16, 13, GetColor(0x555577ff));
        }

        /* ============================================================
         * Status bar
         * ============================================================ */
        DrawText(TextFormat("Peers: %zu  |  Scroll: mouse wheel  |  Conversations: %d",
                            backend_peer_count(), g_chat_count - 1),
                 CHAT_X + 4, H - 16, 11, GetColor(0x555577ff));

        EndDrawing();
    }

    backend_shutdown();
    CloseWindow();
}