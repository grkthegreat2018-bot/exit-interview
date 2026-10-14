#pragma once
#include "common.h"
#include "assets.h"

namespace HUD {
    inline void DrawClock(const Assets& a, int W, float gameHour, bool debug, float instab, int night = 0) {
        std::string t = FmtClock(gameHour);
        Vector2 ts = MeasureTextEx(a.font, t.c_str(), 36, 1);
        DrawTextEx(a.font, t.c_str(), {W / 2.f - ts.x / 2, 16.f}, 36, 1, Color{200, 210, 215, 255});
        DrawTextEx(a.fontSmall, "MERIDIAN LOGISTICS - HR", {W / 2.f - MeasureTextEx(a.fontSmall, "MERIDIAN LOGISTICS - HR", 16, 1).x / 2, 50.f}, 16, 1, Color{120, 130, 140, 255});
        if (night > 0) {
            std::string n = "NIGHT " + std::to_string(night);
            DrawTextEx(a.fontSmall, n.c_str(), {24.f, 16.f}, 16, 1, Color{150, 130, 90, 255});
        }
        if (debug) {
            std::string d = "instability " + std::to_string((int)instab);
            DrawTextEx(a.fontSmall, d.c_str(), {W / 2.f - MeasureTextEx(a.fontSmall, d.c_str(), 16, 1).x / 2, 66.f}, 16, 1, Color{220, 120, 120, 255});
        }
    }
    inline void DrawPrompt(const Assets& a, int W, int H, const std::string& s) {
        if (s.empty()) return;
        Vector2 ts = MeasureTextEx(a.font, s.c_str(), 24, 1);
        Rectangle r{W / 2.f - ts.x / 2 - 16.f, H - 120.f, ts.x + 32.f, 36.f};
        DrawRectangleRounded(r, 0.2f, 6, Color{10, 12, 16, 190});
        DrawTextEx(a.font, s.c_str(), {r.x + 16.f, r.y + 7.f}, 24, 1, Color{220, 225, 230, 255});
    }
    inline void DrawNotes(const Assets& a, int W, const std::vector<std::pair<std::string, float>>& notes) {
        float y = 90;
        for (auto& n : notes) {
            float al = std::clamp(n.second, 0.f, 1.f);
            for (auto& l : WrapText(a.fontSmall, n.first, 19, 1, 380)) {
                Vector2 ts = MeasureTextEx(a.fontSmall, l.c_str(), 19, 1);
                DrawTextEx(a.fontSmall, l.c_str(), {W - ts.x - 24.f, y}, 19, 1,
                           Color{235, 200, 120, (unsigned char)(230 * al)});
                y += 22;
            }
            y += 6;
        }
    }
    inline void DrawCrosshair(int W, int H) {
        DrawCircle(W / 2, H / 2, 2.5f, Color{230, 230, 230, 160});
    }
    inline void DrawVignette(int W, int H, float instab) {
        // subtle darkening edges that grow with instability (meter stays hidden)
        float t = instab / 100.f;
        if (t < 0.25f) return;
        int bands = 6;
        for (int i = 0; i < bands; i++) {
            unsigned char al = (unsigned char)((t - 0.25f) * 120.f * (float)i / bands);
            float pad = (float)i * 30.f;
            DrawRectangle(0, 0, W, (int)pad + 30, Color{10, 4, 8, (unsigned char)(al / 3)});
            DrawRectangle(0, (int)(H - pad - 30), W, (int)pad + 30, Color{10, 4, 8, (unsigned char)(al / 3)});
            DrawRectangle(0, 0, (int)pad + 30, H, Color{10, 4, 8, (unsigned char)(al / 3)});
            DrawRectangle((int)(W - pad - 30), 0, (int)pad + 30, H, Color{10, 4, 8, (unsigned char)(al / 3)});
        }
    }
}
