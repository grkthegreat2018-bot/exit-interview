#include "common.h"
#include "game.h"

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "EXIT INTERVIEW");
    // borderless fullscreen: undecorated window sized to the desktop monitor
    SetWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    int mon = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
    SetWindowPosition(0, 0);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    Game game;
    if (!game.Init()) {
        // minimal error frame
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("Failed to load assets/ - run next to assets/ or the exe folder.", 40, 40, 20, RED);
            EndDrawing();
        }
        CloseWindow();
        return 1;
    }
    game.Run();
    game.Shutdown();
    CloseWindow();
    return 0;
}
