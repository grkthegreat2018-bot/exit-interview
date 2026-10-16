#include "common.h"
#include "game.h"

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "EXIT INTERVIEW");
    // window icon: a lit doorway, an EXIT bar, and somebody standing in it -
    // generated at startup so no asset file is needed
    {
        Image icon = GenImageColor(64, 64, Color{8, 9, 12, 255});
        ImageDrawRectangle(&icon, 20, 10, 24, 5, Color{215, 245, 220, 255});   // EXIT sign
        ImageDrawRectangle(&icon, 22, 18, 20, 34, Color{150, 228, 170, 255});  // lit opening
        ImageDrawCircle(&icon, 32, 30, 4, Color{6, 7, 9, 255});                // head
        ImageDrawRectangle(&icon, 27, 34, 10, 18, Color{6, 7, 9, 255});        // silhouette body
        ImageDrawRectangle(&icon, 20, 52, 24, 3, Color{90, 150, 110, 255});    // threshold glow
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
    // borderless fullscreen: undecorated window sized to the desktop monitor
    SetWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    int mon = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
    SetWindowPosition(0, 0);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    // kill the unpainted white window: one black frame before heavy init
    BeginDrawing();
    ClearBackground(Color{6, 7, 9, 255});
    EndDrawing();

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
