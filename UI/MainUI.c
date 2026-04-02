#include <stdio.h>
#include <stdbool.h>

// Raylib + Raygui
#include "include/raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "TorChat - UI Prototype");
    SetTargetFPS(60); // 限制在 60 帧，避免吃满 CPU

    char inputText[256] = { 0 }; // 输入框的文本缓存
    bool editMode = false;       // 输入框是否处于激活状态
    
    const char *peerList = "Charles (192.168.1.2);Khoa (192.168.1.3)";
    int activePeerScrollIndex = 0;
    int activePeerActive = -1;

    while (!WindowShouldClose()) {
        
        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

        // Peer List
        GuiDrawRectangle((Rectangle){ 10, 10, 200, 580 }, 1, LIGHTGRAY, Fade(LIGHTGRAY, 0.3f));
        GuiLabel((Rectangle){ 20, 15, 180, 20 }, "Active Peers");
        GuiListView((Rectangle){ 20, 40, 180, 540 }, peerList, &activePeerScrollIndex, &activePeerActive);

        // Chat History
        GuiDrawRectangle((Rectangle){ 220, 10, 570, 480 }, 1, LIGHTGRAY, BLANK);
        GuiLabel((Rectangle){ 230, 20, 200, 20 }, "Chat history will appear here...");
        // 提示：未来这里会写一个循环，把 struct MessageLog 里的文字一行行打印出来

        // Input Area
        if (GuiTextBox((Rectangle){ 220, 510, 450, 80 }, inputText, 256, editMode)) {
            editMode = !editMode; // 点击框体切换输入状态
        }

        // Send Icon
        if (GuiButton((Rectangle){ 690, 510, 100, 80 }, "SEND")) {
            printf("Ready to send: %s\n", inputText);
            inputText[0] = '\0'; // 发送后清空输入框
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}