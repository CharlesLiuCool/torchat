#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "raylib.h"
#include "../backend.h"
#include "../storage.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

/* ------------------------------------------------------------------ */
/* 结构定义与配置                                                       */
/* ------------------------------------------------------------------ */

#define MAX_PEERS        64
#define MAX_CHAT_LINES   512
#define MAX_LINE_LENGTH  512

typedef struct {
    char text[MAX_LINE_LENGTH];
    bool is_me;
    char timestamp[16];
} chat_msg_t;

typedef struct {
    int  fd;
    char addr[64];                                /* 提取出来的纯净昵称 */
    chat_msg_t messages[MAX_CHAT_LINES];
    int  count;
    int  scroll;
    int  unread;
} peer_chat_t;

typedef struct {
    char nickname[32];
    char ip[16];
    int  port;
} profile_t;

static peer_chat_t g_chats[MAX_PEERS + 1];
static int         g_chat_count = 1;
static int         g_active     = 0;

static profile_t g_me = { "MyName", "127.0.0.1", 0 };
static char nickname_buf[32] = "MyName"; 
static bool nickname_edit = false;

static int bg_theme_idx = 0; // 0:CyberGrid, 1:Matrix, 2:Starfield, 3:Minimal
static float global_time = 0.0f; // 用于动画

/* ------------------------------------------------------------------ */
/* 字符串清洗器：解决 TCP 粘包导致的 /getnick 尾巴                       */
/* ------------------------------------------------------------------ */
static void clean_nickname(char* name) {
    if (!name) return;
    // 斩断 TCP 粘包传来的指令尾巴
    char* p = strstr(name, "/getnick");
    if (p) *p = '\0';
    p = strstr(name, "/nick");
    if (p) *p = '\0';
    
    // 移除尾部换行或空格
    int len = strlen(name);
    while (len > 0 && (name[len-1] == '\n' || name[len-1] == '\r' || name[len-1] == ' ')) {
        name[len-1] = '\0';
        len--;
    }
}

/* ------------------------------------------------------------------ */
/* 极客风像素头像生成器 (8-bit Identicon)                              */
/* ------------------------------------------------------------------ */
/* 类似 GitHub 的对称像素格生成器，基于名字哈希，独一无二且充满科技感 */
static void DrawIdenticon(int cx, int cy, int radius, const char* seedStr) {
    unsigned int hash = 5381;
    const char* p = seedStr;
    while (*p) { hash = ((hash << 5) + hash) + *p++; }
    
    // 颜色池
    Color colors[] = { 
        (Color){50, 205, 50, 255},  // 霓虹绿
        (Color){0, 191, 255, 255},  // 深空蓝
        (Color){255, 105, 180, 255},// 赛博粉
        (Color){255, 165, 0, 255},  // 警戒橙
        (Color){147, 112, 219, 255},// 迷幻紫
        (Color){255, 215, 0, 255}   // 黄金
    };
    Color fg = colors[hash % 6];
    Color bg = GetColor(0x1a1a2eff); // 深色底

    // 绘制头像底框
    int size = radius * 2;
    int startX = cx - radius;
    int startY = cy - radius;
    DrawRectangleRounded((Rectangle){(float)startX, (float)startY, (float)size, (float)size}, 0.2f, 4, bg);
    DrawRectangleRoundedLinesEx((Rectangle){(float)startX, (float)startY, (float)size, (float)size}, 0.2f, 4, 1.5f, fg);

    // 5x5 对称像素阵列
    int gridSize = 5;
    int cellSize = (size - 6) / gridSize;
    int padX = startX + 3 + (size - 6 - cellSize * 5) / 2;
    int padY = startY + 3 + (size - 6 - cellSize * 5) / 2;

    for (int x = 0; x < 3; x++) { // 画左半边和中轴
        for (int y = 0; y < 5; y++) {
            // 用哈希的二进制位决定是否填充
            int bitIndex = (x * 5 + y);
            if ((hash >> bitIndex) & 1) {
                // 左侧方块
                DrawRectangle(padX + x * cellSize, padY + y * cellSize, cellSize, cellSize, fg);
                // 对称镜像右侧方块
                if (x < 2) {
                    DrawRectangle(padX + (4 - x) * cellSize, padY + y * cellSize, cellSize, cellSize, fg);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 动态背景渲染引擎 (Animated Background Themes)                         */
/* ------------------------------------------------------------------ */
static void DrawCoolBackground(int x, int y, int w, int h) {
    global_time += GetFrameTime();
    
    // 限制绘制区域，避免背景溢出到侧边栏
    BeginScissorMode(x, y, w, h);

    if (bg_theme_idx == 0) {
        // Theme 0: 赛博网格扫描 (Cyber Grid)
        ClearBackground(GetColor(0x0a0a15ff));
        float offset = (global_time * 20.0f);
        int gridSize = 40;
        int shiftY = (int)offset % gridSize;
        int shiftX = (int)(offset * 0.5f) % gridSize;

        for (int i = -gridSize; i < w + gridSize; i += gridSize) {
            DrawLine(x + i + shiftX, y, x + i + shiftX, y + h, Fade(DARKBLUE, 0.4f));
        }
        for (int i = -gridSize; i < h + gridSize; i += gridSize) {
            DrawLine(x, y + i + shiftY, x + w, y + i + shiftY, Fade(DARKBLUE, 0.4f));
        }
    } 
    else if (bg_theme_idx == 1) {
        // Theme 1: 黑客帝国数字雨 (Matrix Rain)
        ClearBackground(GetColor(0x051005ff));
        for (int i = 0; i < w; i += 30) {
            float speed = 30.0f + (i % 50);
            int dropY = (int)(global_time * speed + i * 7) % (h + 100) - 50;
            DrawText(TextFormat("%d", (i % 2)), x + i, y + dropY, 14, Fade(GREEN, 0.7f));
            DrawText(TextFormat("%d", ((i+1) % 2)), x + i, y + dropY - 20, 14, Fade(DARKGREEN, 0.4f));
        }
    }
    else if (bg_theme_idx == 2) {
        // Theme 2: 深空星流 (Starfield)
        ClearBackground(GetColor(0x050510ff));
        for (int i = 0; i < 50; i++) {
            float speed = 10.0f + (i % 30);
            int starX = w - (int)(global_time * speed + i * 40) % w;
            int starY = (i * i * 17) % h;
            DrawRectangle(x + starX, y + starY, (i%3)+1, (i%3)+1, Fade(WHITE, (i%5)*0.2f + 0.2f));
        }
    }
    else {
        // Theme 3: 极简纯净 (Minimal)
        ClearBackground(GetColor(0x111116ff));
    }

    // 叠加 CRT 扫描线质感滤镜
    for (int i = 0; i < h; i += 4) {
        DrawLine(x, y + i, x + w, y + i, Fade(BLACK, 0.2f));
    }

    EndScissorMode();
}

/* ------------------------------------------------------------------ */
/* 文本表情替换器 (Emoji Replacer)                                     */
/* ------------------------------------------------------------------ */
static void replace_emojis(char* str) {
    char temp[MAX_LINE_LENGTH] = {0};
    char* src = str;
    char* dst = temp;
    
    while (*src && (dst - temp) < (MAX_LINE_LENGTH - 10)) {
        if (strncmp(src, ":smile:", 7) == 0) { strcpy(dst, "(^_^)"); dst += 5; src += 7; }
        else if (strncmp(src, ":sad:", 5) == 0) { strcpy(dst, "(T_T)"); dst += 5; src += 5; }
        else if (strncmp(src, ":heart:", 7) == 0) { strcpy(dst, "<3"); dst += 2; src += 7; }
        else if (strncmp(src, ":lol:", 5) == 0) { strcpy(dst, "xD"); dst += 2; src += 5; }
        else if (strncmp(src, ":angry:", 7) == 0) { strcpy(dst, "(>_<)"); dst += 5; src += 7; }
        else { *dst++ = *src++; }
    }
    *dst = '\0';
    strncpy(str, temp, MAX_LINE_LENGTH - 1);
    str[MAX_LINE_LENGTH - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* 辅助函数                                                            */
/* ------------------------------------------------------------------ */
static const char* get_timestamp(void) {
    static char buf[10];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, sizeof(buf), "%H:%M", t);
    return buf;
}

static peer_chat_t* chat_for_fd(int fd) {
    for (int i = 0; i < g_chat_count; i++)
        if (g_chats[i].fd == fd) return &g_chats[i];
    return NULL;
}

static peer_chat_t* chat_get_or_create(int fd) {
    peer_chat_t* c = chat_for_fd(fd);
    if (c) return c;
    if (g_chat_count > MAX_PEERS) return &g_chats[0];
    c = &g_chats[g_chat_count++];
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    strncpy(c->addr, "UNKNOWN", sizeof(c->addr) - 1);
    return c;
}

static peer_chat_t* chat_get_or_create_by_addr(const char* addr) {
    for (int i = 1; i < g_chat_count; i++)
        if (strcmp(g_chats[i].addr, addr) == 0) return &g_chats[i];
    if (g_chat_count > MAX_PEERS) return &g_chats[0];
    peer_chat_t* c = &g_chats[g_chat_count++];
    memset(c, 0, sizeof(*c));
    c->fd = -2;
    strncpy(c->addr, addr, sizeof(c->addr) - 1);
    return c;
}

static void chat_push_to(peer_chat_t* c, const char* clean_msg, bool is_me) {
    if (!c || clean_msg[0] == '\0') return;
    chat_msg_t* m;
    
    if (c->count < MAX_CHAT_LINES) {
        m = &c->messages[c->count++];
    } else {
        memmove(&c->messages[0], &c->messages[1], sizeof(c->messages[0]) * (MAX_CHAT_LINES - 1));
        m = &c->messages[MAX_CHAT_LINES - 1];
    }
    
    strncpy(m->text, clean_msg, MAX_LINE_LENGTH - 1);
    m->text[MAX_LINE_LENGTH - 1] = '\0';
    m->is_me = is_me;
    strncpy(m->timestamp, get_timestamp(), sizeof(m->timestamp) - 1);

    c->scroll = c->count;
    if (c != &g_chats[g_active]) c->unread++;
}

static void chat_push_system(const char* msg) {
    chat_push_to(&g_chats[0], msg, false);
}

static const char* peer_name_for_fd(int fd) {
    for (size_t i = 0; i < backend_peer_count(); i++)
        if (backend_peer_fd(i) == fd) return backend_peer_name(i);
    return NULL;
}

static void parse_and_push_incoming(peer_chat_t* c, const char* raw_msg) {
    if (c->fd == -1 && raw_msg[0] == '[') {
    chat_push_to(c, raw_msg, false);
    return;
    }
    char sender_name[64] = {0};
    const char* actual_msg = raw_msg;
    bool is_me = false;

    if (raw_msg[0] == '[') {
        const char* end_bracket = strchr(raw_msg, ']');
        if (end_bracket) {
            int name_len = end_bracket - raw_msg - 1;
            if (name_len > 0 && name_len < 63) {
                strncpy(sender_name, raw_msg + 1, name_len);
                sender_name[name_len] = '\0';
                
                clean_nickname(sender_name); // 斩断粘包的脏数据
                
                actual_msg = end_bracket + 1;
                if (*actual_msg == ':') actual_msg++;
                if (*actual_msg == ' ') actual_msg++;

                if (strcmp(sender_name, g_me.nickname) == 0) {
                    is_me = true;
                }
            }
        }
    }
    
    chat_push_to(c, actual_msg, is_me);
}

/* ------------------------------------------------------------------ */
/* 后端回调函数                                                         */
/* ------------------------------------------------------------------ */

void ui_on_msg(int fd, const char* msg) {
    if (fd == -1) return; 
    peer_chat_t* c = chat_get_or_create(fd);
    parse_and_push_incoming(c, msg);
}


void ui_on_peer_connected(int fd) {
    chat_push_system(TextFormat("*** Peer (FD:%d) joined the network successfully", fd));
    peer_chat_t* c = chat_get_or_create(fd); 
    chat_push_to(c, "*** Tip: Type :smile:, :sad:, :heart:, :lol:, :angry: for emojis!", false);
}

void ui_on_peer_disconnected(int fd) {
    peer_chat_t* c = chat_for_fd(fd);
    
    // 如果已经知道对方的名字，就显示名字，否则显示 FD 编号
    const char* name = (c && strcmp(c->addr, "UNKNOWN") != 0) ? c->addr : TextFormat("FD:%d", fd);
    chat_push_system(TextFormat("*** Peer [%s] disconnected", name));
    
    if (c) chat_push_to(c, "*** Disconnected", false);
}

static int replay_cb(const chat_message_t* msg, void* userdata) {
    (void)userdata;
    peer_chat_t* c = (msg->peer_addr && msg->peer_addr[0] != '\0')
                     ? chat_get_or_create_by_addr(msg->peer_addr)
                     : &g_chats[0];
    parse_and_push_incoming(c, msg->body);
    return 0;
}

/* ------------------------------------------------------------------ */
/* UI 渲染组件                                                         */
/* ------------------------------------------------------------------ */

void DrawChatBubble(chat_msg_t* m, const char* peerName, int x, int y, int maxWidth) {
    int fontSize = 14;
    int padding = 10;
    int avatarRadius = 18;
    
    int textWidth = MeasureText(m->text, fontSize);
    int bubbleWidth = textWidth + padding * 2;
    if (bubbleWidth > maxWidth) bubbleWidth = maxWidth;
    
    // 简易处理多行气泡高度
    int bubbleHeight = 36 + (textWidth / maxWidth) * 16; 

    const char* senderName = m->is_me ? g_me.nickname : peerName;
    int nameWidth = MeasureText(senderName, 11);
    int timeWidth = MeasureText(m->timestamp, 10);

    int avatarX, bubbleX;

    if (m->is_me) {
        avatarX = x - avatarRadius;
        bubbleX = avatarX - avatarRadius - 12 - bubbleWidth;
        
        DrawText(senderName, bubbleX + bubbleWidth - nameWidth, y, 11, SKYBLUE);
        DrawText(m->timestamp, bubbleX + bubbleWidth - nameWidth - timeWidth - 8, y + 1, 10, DARKGRAY);
    } else {
        avatarX = x + avatarRadius;
        bubbleX = avatarX + avatarRadius + 12;
        
        DrawText(senderName, bubbleX, y, 11, LIGHTGRAY);
        DrawText(m->timestamp, bubbleX + nameWidth + 8, y + 1, 10, DARKGRAY);
    }

    DrawIdenticon(avatarX, y + 22, avatarRadius, senderName);

    y += 18; 
    Color bubbleColor = m->is_me ? GetColor(0x1c4a8aff) : GetColor(0x2d2d44ff);
    DrawRectangleRounded((Rectangle){(float)bubbleX, (float)y, (float)bubbleWidth, (float)bubbleHeight}, 0.3f, 8, bubbleColor);
    DrawText(m->text, bubbleX + padding, y + 10, fontSize, WHITE);
}

/* ------------------------------------------------------------------ */
/* 运行 UI                                                            */
/* ------------------------------------------------------------------ */

void run_ui(void) {
    const int W = 950, H = 650;
    InitWindow(W, H, "TorChat");
    SetTargetFPS(60);

    g_chats[0].fd = -1;
    chat_push_system("--- TorChat system log ---");
    chat_push_system("*** Tip: Type :smile:, :sad:, :heart:, :lol:, :angry: for emojis!");

    storage_load_history(200, replay_cb, NULL);

    char msg_buf[256] = {0};
    char ip_buf[64]   = {"127.0.0.1"};
    char port_buf[12] = {"9000"};
    bool msg_edit = false, ip_edit = false, port_edit = false;

    const int SIDEBAR_W = 240;
    const int CHAT_X = SIDEBAR_W + 15;
    const int CHAT_W = W - CHAT_X - 10;
    const int CHAT_H = 480;

    // 从后端获取命令行传入的昵称并覆盖默认的 "MyName"
    const char* cmd_nick = backend_get_nickname();
    if (cmd_nick && cmd_nick[0] != '\0') {
        strncpy(g_me.nickname, cmd_nick, 31);
        strncpy(nickname_buf, cmd_nick, 31);
    }
    // 通知后端最新状态
    backend_set_nickname(g_me.nickname);

    while (!WindowShouldClose()) {
        backend_poll();

        g_me.port = backend_get_port();
        for (int i = 1; i < g_chat_count; i++) {
            if (g_chats[i].fd >= 0) {
                const char* real_name = peer_name_for_fd(g_chats[i].fd);
                if (real_name && real_name[0] != '\0') {
                    char temp_name[64];
                    strncpy(temp_name, real_name, sizeof(temp_name) - 1);
                    temp_name[63] = '\0';
                    clean_nickname(temp_name); // 更新好友名字前先洗干净粘包残余
                    if (strcmp(temp_name, "UNKNOWN") != 0) {
                        strncpy(g_chats[i].addr, temp_name, sizeof(g_chats[i].addr) - 1);
                    }
                }
            }
        }

        peer_chat_t* active_chat = &g_chats[g_active];

        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            active_chat->scroll -= (int)wheel * 2;
            if (active_chat->scroll < 0) active_chat->scroll = 0;
        }

        /* 修复1：发送消息时不再进行二次打包 */
        if (msg_edit && IsKeyPressed(KEY_ENTER) && msg_buf[0] != '\0') {
            if (g_active > 0) {
                replace_emojis(msg_buf);
                
                // 直接发 msg_buf，backend_send_message 会自动包上 [MyName]:
                backend_send_message(msg_buf); 
                
                // 本地回显直接记录纯消息
                chat_push_to(active_chat, msg_buf, true);
                msg_buf[0] = '\0';
            }
        }

        BeginDrawing();
        ClearBackground(GetColor(0x0a0a12ff));

        /* ================= 左侧侧边栏 ================= */
        DrawRectangle(5, 5, SIDEBAR_W, H - 10, GetColor(0x12121fff));
        DrawRectangleLines(5, 5, SIDEBAR_W, H - 10, GetColor(0x2a2a4aff));

        /* 1. 个人资料区域 (Profile) */
        DrawIdenticon(35, 45, 20, g_me.nickname); // 用像素风头像替换

        if (GuiTextBox((Rectangle){65, 25, 115, 24}, nickname_buf, 32, nickname_edit))
            nickname_edit = !nickname_edit;
        
        if (GuiButton((Rectangle){185, 25, 45, 24}, "Save")) {
            strncpy(g_me.nickname, nickname_buf, 31);
            g_me.nickname[31] = '\0';
            nickname_edit = false;
            backend_set_nickname(g_me.nickname);
        }

        DrawText(TextFormat("IP: %s:%d", g_me.ip, g_me.port), 65, 58, 11, GRAY);

        if (GuiButton((Rectangle){185, 55, 45, 20}, "Theme")) {
            bg_theme_idx = (bg_theme_idx + 1) % 4; // 切换炫酷背景
        }

        DrawLine(15, 90, SIDEBAR_W - 10, 90, GetColor(0x2a2a4aff));
        GuiLabel((Rectangle){15, 100, SIDEBAR_W - 20, 20}, "# CONVERSATIONS");

        /* 2. 聊天列表 */
        int row_y = 130;
        for (int i = 0; i < g_chat_count; i++) {
            Rectangle row = {12, (float)row_y, (float)SIDEBAR_W - 15, 40};
            bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
            bool selected = (i == g_active);

            if (selected) DrawRectangleRounded(row, 0.2f, 6, GetColor(0x252545ff));
            else if (hovered) DrawRectangleRounded(row, 0.2f, 6, GetColor(0x1a1a2eff));

            Color statusColor = (g_chats[i].fd == -1) ? BLUE : (g_chats[i].fd >= 0 ? GREEN : RED);
            DrawCircle(25, row_y + 20, 4, statusColor);

            const char* name = (g_chats[i].fd == -1) ? "System Log" : g_chats[i].addr;
            
            if (g_chats[i].fd != -1) {
                DrawIdenticon(45, row_y + 20, 10, name); // 列表里也用像素头像
                DrawText(name, 65, row_y + 14, 13, selected ? WHITE : LIGHTGRAY);
            } else {
                DrawText(name, 45, row_y + 14, 13, selected ? WHITE : LIGHTGRAY);
            }

            if (g_chats[i].unread > 0) {
                DrawRectangle(SIDEBAR_W - 35, row_y + 12, 20, 16, RED);
                DrawText(TextFormat("%d", g_chats[i].unread), SIDEBAR_W - 30, row_y + 15, 10, WHITE);
            }

            if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                g_active = i;
                g_chats[i].unread = 0;
            }
            row_y += 45;
            if (row_y > H - 170) break;
        }

        /* 3. 连接表单 */
        int conn_y = H - 160;
        DrawLine(15, conn_y, SIDEBAR_W - 10, conn_y, GetColor(0x2a2a4aff));
        GuiLabel((Rectangle){15, conn_y + 5, 150, 20}, "CONNECT TO PEER");
        if (GuiTextBox((Rectangle){15, conn_y + 30, 215, 24}, ip_buf, 64, ip_edit)) ip_edit = !ip_edit;
        if (GuiTextBox((Rectangle){15, conn_y + 60, 80, 24}, port_buf, 12, port_edit)) port_edit = !port_edit;

	if (GuiButton((Rectangle){105, conn_y + 60, 125, 24}, "Connect")) {
            int port = atoi(port_buf);
            if (port > 0 && ip_buf[0] != '\0') {
                // 修改这里：严格拦截自己连自己
                if (port == g_me.port && (strcmp(ip_buf, "127.0.0.1") == 0 || strcmp(ip_buf, "localhost") == 0)) {
                    chat_push_system("*** Error: Cannot connect to yourself.");
                } else {
                    chat_push_system(TextFormat("*** Attempting to connect to %s:%d...", ip_buf, port));
                    int fd = backend_connect_to_peer(ip_buf, port);
                    if (fd < 0) {
                        chat_push_system("*** Connection failed: Port may be closed or unreachable.");
                    }
                }
            } else {
                chat_push_system("*** Connection failed: Invalid IP or Port.");
            }
        }
        
        /* ================= 右侧聊天区域 ================= */
        // 渲染动态背景
        DrawCoolBackground(CHAT_X, 10, CHAT_W, CHAT_H);
        DrawRectangleLines(CHAT_X, 10, CHAT_W, CHAT_H, GetColor(0x2a2a4aff));
        
        DrawRectangle(CHAT_X + 1, 11, CHAT_W - 2, 25, GetColor(0x1a1a2eff));
        const char* chatTitle = (active_chat->fd == -1) ? "System Console" : TextFormat("Peer: %s", active_chat->addr);
        DrawText(chatTitle, CHAT_X + 10, 17, 13, SKYBLUE);

        /* 气泡渲染 */
        int curr_y = 50;
        int start_idx = active_chat->scroll - 6; 
        if (start_idx < 0) start_idx = 0;

        for (int i = start_idx; i < active_chat->count && curr_y < CHAT_H - 20; i++) {
            chat_msg_t* m = &active_chat->messages[i];
            
            // 只有以 "***" 开头的才是真正的系统提示
            bool isSys = (strstr(m->text, "***") == m->text);

            if (active_chat->fd == -1) {
                // 情况A：在 System Log 界面，全部靠左按纯文本显示（像终端一样）
                Color textColor = isSys ? GetColor(0xaaaaaaff) : LIGHTGRAY;
                DrawText(TextFormat("[%s] %s", m->timestamp, m->text), CHAT_X + 15, curr_y, 14, textColor);
                curr_y += 25;
            } 
            else if (isSys) {
                // 情况B：在具体的聊天窗口里，遇到 *** 提示，居中显示
                int txtW = MeasureText(m->text, 12);
                DrawText(m->text, CHAT_X + CHAT_W/2 - txtW/2, curr_y, 12, GetColor(0xaaaaaaff));
                curr_y += 25;
            } 
            else {
                // 情况C：正常的聊天气泡
                int boundsX = m->is_me ? (CHAT_X + CHAT_W - 15) : (CHAT_X + 15);
                DrawChatBubble(m, active_chat->addr, boundsX, curr_y, CHAT_W - 120);
                curr_y += 65; 
            }
        }

        /* 底部输入栏 */
        int input_y = CHAT_H + 25;
        if (g_active > 0) {
            if (GuiTextBox((Rectangle){(float)CHAT_X, (float)input_y, (float)CHAT_W - 90, 45}, msg_buf, 256, msg_edit))
                msg_edit = !msg_edit;
            
            if (GuiButton((Rectangle){(float)CHAT_X + CHAT_W - 80, (float)input_y, 80, 45}, "SEND")) {
                if (msg_buf[0] != '\0') {
                    replace_emojis(msg_buf); 
                    backend_send_message(msg_buf); // 修复2: 按钮发送也去掉多余拼接
                    chat_push_to(active_chat, msg_buf, true);
                    msg_buf[0] = '\0';
                }
            }
        } else {
            DrawText("System log is read-only.", CHAT_X + 10, input_y + 15, 15, DARKGRAY);
        }

        DrawText(TextFormat("Peers: %zu  |  Profile: %s  |  Status: Online", backend_peer_count(), g_me.nickname), CHAT_X, H - 20, 11, GRAY);

        EndDrawing();
    }

    backend_shutdown();
    CloseWindow();
}
