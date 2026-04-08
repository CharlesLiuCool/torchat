#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "raylib.h"
#include "../backend.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

/* ------------------------------------------------------------------ */
/* Chat log                                                            */
/* ------------------------------------------------------------------ */

#define MAX_CHAT_LINES   512
#define MAX_LINE_LENGTH  512

typedef struct {
    char lines[MAX_CHAT_LINES][MAX_LINE_LENGTH];
    int  count;
    int  scroll; /* first visible line index */
} chat_log_t;

static chat_log_t g_chat = {0};

static void chat_push(const char* msg)
{
    if (g_chat.count < MAX_CHAT_LINES) {
        strncpy(g_chat.lines[g_chat.count], msg, MAX_LINE_LENGTH - 1);
        g_chat.lines[g_chat.count][MAX_LINE_LENGTH - 1] = '\0';
        g_chat.count++;
    } else {
        /* ring: shift everything up by one */
        memmove(g_chat.lines[0], g_chat.lines[1],
                sizeof(g_chat.lines[0]) * (MAX_CHAT_LINES - 1));
        strncpy(g_chat.lines[MAX_CHAT_LINES - 1], msg, MAX_LINE_LENGTH - 1);
        g_chat.lines[MAX_CHAT_LINES - 1][MAX_LINE_LENGTH - 1] = '\0';
    }
    /* auto-scroll to bottom */
    g_chat.scroll = g_chat.count;
}

/* ------------------------------------------------------------------ */
/* Backend callbacks (file-scope, not static — called by backend)     */
/* ------------------------------------------------------------------ */

void ui_on_msg(int fd, const char* msg)
{
    (void)fd;
    chat_push(msg);
}

void ui_on_peer_connected(int fd)
{
    char line[MAX_LINE_LENGTH];
    snprintf(line, sizeof(line), "*** Peer %d connected", fd);
    chat_push(line);
}

void ui_on_peer_disconnected(int fd)
{
    char line[MAX_LINE_LENGTH];
    snprintf(line, sizeof(line), "*** Peer %d disconnected", fd);
    chat_push(line);
}

/* ------------------------------------------------------------------ */
/* run_ui — called from main()                                        */
/* ------------------------------------------------------------------ */

void run_ui(void)
{
    const int W = 900, H = 620;
    InitWindow(W, H, "TorChat");
    SetTargetFPS(60);

    /* Input state */
    char msg_buf[256]  = {0};
    char ip_buf[64]    = {"127.0.0.1"};
    char port_buf[12]  = {"9000"};
    bool msg_edit      = false;
    bool ip_edit       = false;
    bool port_edit     = false;

    /* Peer list scroll */
    int peer_scroll = 0;
    int peer_active = -1;

    /* Chat visible area */
    const int CHAT_X     = 220;
    const int CHAT_Y     = 10;
    const int CHAT_W     = 670;
    const int CHAT_H     = 470;
    const int LINE_H     = 18;
    const int VISIBLE    = (CHAT_H - 10) / LINE_H;

    while (!WindowShouldClose()) {
        /* ---- Network tick ---- */
        backend_poll();

        /* ---- Mouse-wheel chat scroll ---- */
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            g_chat.scroll -= (int)wheel * 3;
            int max_scroll = g_chat.count;
            if (g_chat.scroll < 0)          g_chat.scroll = 0;
            if (g_chat.scroll > max_scroll) g_chat.scroll = max_scroll;
        }

        /* ---- Enter to send ---- */
        if (msg_edit && IsKeyPressed(KEY_ENTER) && msg_buf[0] != '\0') {
            backend_send_message(msg_buf);
            msg_buf[0] = '\0';
        }

        BeginDrawing();
        ClearBackground(GetColor(0x1a1a2eff));

        /* ============================================================
         * LEFT PANEL — peers + connect
         * ============================================================ */
        DrawRectangle(5, 5, 205, H - 10, GetColor(0x12122aff));
        DrawRectangleLines(5, 5, 205, H - 10, GetColor(0x3333aaff));

        GuiLabel((Rectangle){15, 12, 185, 20}, "# CONNECTED PEERS");

        /* Build peer list string — 64 peers × (31 chars + ';') + NUL */
        static char peer_str[64 * 33 + 1];
        peer_str[0] = '\0';
        for (size_t i = 0; i < backend_peer_count(); i++) {
            const char* name = backend_peer_name(i);
            strncat(peer_str, name ? name : "?", 31);
            if (i + 1 < backend_peer_count()) strncat(peer_str, ";", 1);
        }
        if (backend_peer_count() == 0) strcpy(peer_str, "(none)");

        GuiListView((Rectangle){10, 35, 195, 200}, peer_str,
                    &peer_scroll, &peer_active);

        /* Connect section */
        DrawLine(10, 242, 205, 242, GetColor(0x3333aaff));
        GuiLabel((Rectangle){15, 248, 185, 18}, "CONNECT TO PEER");

        GuiLabel((Rectangle){15, 270, 30, 18}, "IP:");
        if (GuiTextBox((Rectangle){50, 268, 155, 22}, ip_buf, sizeof(ip_buf), ip_edit))
            ip_edit = !ip_edit;

        GuiLabel((Rectangle){15, 298, 40, 18}, "Port:");
        if (GuiTextBox((Rectangle){60, 296, 60, 22}, port_buf, sizeof(port_buf), port_edit))
            port_edit = !port_edit;

        if (GuiButton((Rectangle){130, 296, 75, 22}, "Connect")) {
            int p = atoi(port_buf);
            if (p > 0 && ip_buf[0] != '\0') {
                int fd = backend_connect_to_peer(ip_buf, p);
                if (fd < 0) chat_push("*** Connection failed");
            }
        }

        /* ============================================================
         * CHAT AREA
         * ============================================================ */
        DrawRectangle(CHAT_X, CHAT_Y, CHAT_W, CHAT_H, GetColor(0x0e0e20ff));
        DrawRectangleLines(CHAT_X, CHAT_Y, CHAT_W, CHAT_H, GetColor(0x3333aaff));

        /* Compute first line to display */
        int first = g_chat.scroll - VISIBLE;
        if (first < 0) first = 0;

        int y = CHAT_Y + 6;
        for (int i = first; i < g_chat.count && y < CHAT_Y + CHAT_H - LINE_H; i++) {
            Color col = WHITE;
            if (strncmp(g_chat.lines[i], "***", 3) == 0)
                col = GetColor(0x888888ff);
            else if (strncmp(g_chat.lines[i], "[", 1) == 0)
                col = GetColor(0x88ccffff);
            DrawText(g_chat.lines[i], CHAT_X + 6, y, 14, col);
            y += LINE_H;
        }

        /* Scroll hint */
        if (g_chat.scroll < g_chat.count) {
            DrawText("(scroll down for more)", CHAT_X + CHAT_W - 200, CHAT_Y + CHAT_H - 18,
                     11, GetColor(0x555577ff));
        }

        /* ============================================================
         * INPUT BAR
         * ============================================================ */
        int INPUT_Y = CHAT_Y + CHAT_H + 8;
        DrawRectangle(CHAT_X, INPUT_Y, CHAT_W, 50, GetColor(0x12122aff));
        DrawRectangleLines(CHAT_X, INPUT_Y, CHAT_W, 50, GetColor(0x3333aaff));

        if (GuiTextBox((Rectangle){CHAT_X + 4, INPUT_Y + 6, CHAT_W - 90, 38},
                       msg_buf, sizeof(msg_buf), msg_edit)) {
            msg_edit = !msg_edit;
        }
        if (GuiButton((Rectangle){CHAT_X + CHAT_W - 82, INPUT_Y + 6, 78, 38}, "SEND")) {
            if (msg_buf[0] != '\0') {
                backend_send_message(msg_buf);
                msg_buf[0] = '\0';
            }
        }

        /* ============================================================
         * Status bar
         * ============================================================ */
        DrawText(TextFormat("Peers: %zu  |  Scroll with mouse wheel",
                            backend_peer_count()),
                 CHAT_X + 4, H - 16, 11, GetColor(0x555577ff));

        EndDrawing();
    }

    backend_shutdown();
    CloseWindow();
}