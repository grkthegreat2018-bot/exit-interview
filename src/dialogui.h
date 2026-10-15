#pragma once
#include "common.h"
#include "assets.h"
#include <functional>

// Interview dialog panel - drawn when seated and focused on the visitor.
class DialogPanel {
public:
    std::string input;
    bool pendingReply = false;
    bool focused = false;   // input field focus - A/D lean only when unfocused
    std::function<void(const std::string&)> onSay;
    std::function<void()> onEnd;
    std::string title = "INTERVIEW";       // panel header; "PHONE CALL" for the desk phone
    std::string endLabel = "END INTERVIEW";
    float focusGrace = 0;                  // eat keystrokes briefly after focus
    bool prevFocused = false;              // so the interact key can't type "e"

    void Reset() { input.clear(); pendingReply = false; focused = false;
                   focusGrace = 0; prevFocused = false;
                   title = "INTERVIEW"; endLabel = "END INTERVIEW"; }

    void Draw(const Assets& a, int W, int H,
              const std::string& visitorName,
              const std::vector<std::pair<std::string, std::string>>& transcript) {
        float ph = H * 0.40f;
        Rectangle p{30.f, H - ph - 16.f, (float)W - 60.f, ph};
        DrawRectangleRounded(p, 0.03f, 8, Color{16, 18, 24, 235});
        DrawRectangleRoundedLinesEx(p, 0.03f, 8, 2, Color{60, 70, 88, 255});

        DrawTextEx(a.fontSmall, (title + " - " + visitorName).c_str(), {p.x + 16.f, p.y + 10.f}, 20, 1, Color{160, 190, 212, 255});
        Rectangle endB{p.x + p.width - 150.f, p.y + 8.f, 134.f, 26.f};
        Vector2 m = GetMousePosition();
        bool hov = CheckCollisionPointRec(m, endB);
        DrawRectangleRounded(endB, 0.15f, 6, hov ? Color{120, 60, 60, 255} : Color{90, 50, 52, 255});
        DrawTextEx(a.fontSmall, endLabel.c_str(), {endB.x + 10.f, endB.y + 4.f}, 18, 1, WHITE);
        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && onEnd) onEnd();

        // transcript (last lines that fit)
        float tx = p.x + 16, tw = p.width - 32;
        float ty = p.y + 42;
        float inputH = 40;
        float avail = p.height - 42 - inputH - 12;
        std::vector<std::pair<Color, std::string>> lines;
        for (auto& [spk, txt] : transcript) {
            Color c = (spk == "You") ? Color{175, 218, 245, 255} : (spk == "-") ? Color{135, 140, 150, 255} : Color{225, 215, 198, 255};
            std::string prefix = (spk == "-" || spk.empty()) ? "" : spk + ": ";
            for (auto& l : WrapText(a.fontSmall, prefix + txt, 20, 1, tw)) lines.push_back({c, l});
        }
        int maxLines = (int)(avail / 24);
        int start = std::max(0, (int)lines.size() - maxLines);
        for (int i = start; i < (int)lines.size(); i++) {
            DrawTextEx(a.fontSmall, lines[i].second.c_str(), {tx, ty}, 20, 1, lines[i].first);
            ty += 24;
        }
        if (pendingReply)
            DrawTextEx(a.fontSmall, (visitorName + " is typing...").c_str(), {tx, ty}, 20, 1, Color{135, 140, 150, 255});

        // input - click to focus; only captures keys while focused
        Rectangle inp{tx, p.y + p.height - inputH - 8.f, p.width - 150.f, inputH};
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) focused = CheckCollisionPointRec(m, inp);
        // fresh focus (sit-down, answer, click): swallow keys for a beat so the
        // interact keypress that opened this panel doesn't land as text
        if (focused && !prevFocused) { focusGrace = 0.22f; while (GetCharPressed() > 0) {} }
        prevFocused = focused;
        if (focusGrace > 0) focusGrace -= GetFrameTime();
        DrawRectangleRounded(inp, 0.1f, 6, Color{10, 12, 16, 255});
        DrawRectangleRoundedLinesEx(inp, 0.1f, 6, 2, focused ? Color{150, 200, 235, 255} : Color{90, 120, 150, 255});
        int ch;  // drain queue even unfocused so held keys don't flush in later
        while ((ch = GetCharPressed()) > 0)
            if (focused && focusGrace <= 0 && ch >= 32 && ch <= 255 && input.size() < 400)
                input += CodepointToUtf8(ch);
        if (focused && focusGrace <= 0 && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !input.empty())
            input.pop_back();
        if (focused && focusGrace <= 0) TryPaste(input, 400);
        bool send = focused && focusGrace <= 0 && IsKeyPressed(KEY_ENTER);
        Rectangle sendB{inp.x + inp.width + 10.f, inp.y, 130.f, inputH};
        hov = CheckCollisionPointRec(m, sendB);
        DrawRectangleRounded(sendB, 0.1f, 6, hov ? Color{80, 110, 80, 255} : Color{55, 80, 58, 255});
        DrawTextEx(a.fontSmall, "SAY", {sendB.x + 48.f, sendB.y + 10.f}, 20, 1, WHITE);
        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) send = true;
        if (send && !input.empty() && onSay) { onSay(input); input.clear(); }
        bool showHint = input.empty() && !focused;
        std::string disp = showHint ? "Ask why they're leaving - check RECORDS + APPOINTMENTS"
                                  : input + ((focused && (int)(GetTime() * 2) % 2 == 0) ? "_" : "");
        BeginScissorMode((int)inp.x + 6, (int)inp.y, (int)inp.width - 12, (int)inp.height);
        DrawTextEx(a.fontSmall, disp.c_str(), {inp.x + 10.f, inp.y + 10.f}, 20, 1,
                   showHint ? Color{105, 115, 125, 255} : Color{215, 230, 240, 255});
        EndScissorMode();
    }
};
