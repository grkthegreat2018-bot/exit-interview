#include "game.h"
#include "hud.h"
#include <rlgl.h>
#include <cstdlib>
#include <cstring>


// named for menus/prompts - raylib keycodes for letters/digits/punct already
// match ASCII, only the special keys need labels. Returns std::string (not a
// static buffer): several calls appear in one concatenation and a shared
// static char[] would alias to the last key written.
std::string Game::KeyName(int k) {
    switch (k) {
        case 0: return "---";
        case KEY_LEFT_SHIFT: return "LSHIFT";
        case KEY_RIGHT_SHIFT: return "RSHIFT";
        case KEY_LEFT_CONTROL: return "LCTRL";
        case KEY_RIGHT_CONTROL: return "RCTRL";
        case KEY_LEFT_ALT: return "LALT";
        case KEY_RIGHT_ALT: return "RALT";
        case KEY_TAB: return "TAB";
        case KEY_SPACE: return "SPACE";
        case KEY_ENTER: return "ENTER";
        case KEY_BACKSPACE: return "BKSP";
        case KEY_ESCAPE: return "ESC";
        case KEY_UP: return "UP";
        case KEY_DOWN: return "DOWN";
        case KEY_LEFT: return "LEFT";
        case KEY_RIGHT: return "RIGHT";
        case KEY_CAPS_LOCK: return "CAPS";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_PAGE_UP: return "PGUP";
        case KEY_PAGE_DOWN: return "PGDN";
        case KEY_INSERT: return "INS";
        case KEY_DELETE: return "DEL";
    }
    if (k >= KEY_F1 && k <= KEY_F12) {
        char fb[8]; snprintf(fb, 8, "F%d", k - KEY_F1 + 1); return fb;
    }
    char b[8];
    snprintf(b, 8, "%c", (k >= 32 && k < 127) ? k : '?');
    return b;
}


std::string Game::BindLabel(const char* action, int key) {
    return std::string(action) + "  [" + KeyName(key) + "]";
}


// hover-highlighted menu button; registers its tooltip on hover. click = true.
bool Game::MenuBtn(Rectangle r, const char* label, const char* tooltip) {
    bool hov = CheckCollisionPointRec(GetMousePosition(), r);
    if (hov && tooltip) tip = tooltip;
    DrawRectangleRounded(r, 0.14f, 6, hov ? Color{85, 100, 125, 255} : Color{55, 62, 75, 255});
    if (hov) DrawRectangleRoundedLinesEx(r, 0.14f, 6, 1.5f, Color{150, 200, 235, 200});
    Vector2 ts = MeasureTextEx(A.font, label, 23, 1);
    DrawTextEx(A.font, label, {r.x + (r.width - ts.x) / 2, r.y + (r.height - 23) / 2 + 1.f}, 23, 1, WHITE);
    return hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}


// setting row: label left, drag slider right. mutates v live while held.
void Game::SliderRow(Rectangle r, const char* label, float& v, float lo, float hi,
                     const char* tooltip) {
    Vector2 m = GetMousePosition();
    Rectangle bar{r.x + r.width - 260.f, r.y + 12.f, 200.f, 20.f};
    if (CheckCollisionPointRec(m, r) && tooltip) tip = tooltip;
    if (CheckCollisionPointRec(m, bar) && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        v = std::clamp(lo + (m.x - bar.x) / bar.width * (hi - lo), lo, hi);
    DrawRectangleRounded(r, 0.12f, 6, Color{40, 44, 54, 255});
    DrawTextEx(A.font, label, {r.x + 14.f, r.y + 11.f}, 20, 1, Color{190, 200, 210, 255});
    float t = std::clamp((v - lo) / (hi - lo), 0.f, 1.f);
    DrawRectangleRounded(bar, 0.5f, 6, Color{28, 31, 40, 255});
    DrawRectangleRounded({bar.x, bar.y, bar.width * t, bar.height}, 0.5f, 6, Color{95, 115, 145, 255});
    DrawCircleV({bar.x + bar.width * t, bar.y + bar.height / 2}, bar.height * 0.68f, Color{180, 195, 215, 255});
    char vb[24];
    if (lo == 0.f && hi == 1.f) snprintf(vb, 24, "%d%%", (int)(v * 100.f + 0.5f));
    else snprintf(vb, 24, "%.2f", v);
    DrawTextEx(A.fontSmall, vb, {bar.x + bar.width + 10.f, r.y + 13.f}, 18, 1, Color{150, 160, 170, 255});
}


// setting row: label + click-to-edit text value (empty = config default)
void Game::TextRow(Rectangle r, const char* label, std::string& s, int id,
                   const char* tooltip) {
    Vector2 m = GetMousePosition();
    bool hov = CheckCollisionPointRec(m, r);
    if (hov && tooltip) tip = tooltip;
    if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { editId = id; focusApiKey = false; }
    else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !hov && editId == id) editId = 0;
    bool foc = editId == id;
    DrawRectangleRounded(r, 0.12f, 6, Color{40, 44, 54, 255});
    DrawRectangleRoundedLinesEx(r, 0.12f, 6, 2,
        foc ? Color{150, 200, 235, 255} : Color{70, 80, 95, 255});
    if (foc) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (ch >= 32 && ch < 127 && s.size() < 200) s += (char)ch;
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !s.empty())
            s.pop_back();
        TryPaste(s, 200, true);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) { editId = 0; ApplyProvider(); }
    }
    std::string disp = s.empty() ? std::string("(config default)") : s;
    std::string row = std::string(label) + "  " + disp + (foc ? "_" : "");
    // shrink the tail end if the value is longer than the row
    while (MeasureTextEx(A.font, row.c_str(), 17, 1).x > r.width - 16 && disp.size() > 12) {
        disp = ".." + disp.substr(3);
        row = std::string(label) + "  " + disp + (foc ? "_" : "");
    }
    DrawTextEx(A.font, row.c_str(), {r.x + 14.f, r.y + 12.f}, 17, 1,
               s.empty() ? Color{120, 125, 135, 255} : Color{190, 200, 210, 255});
}


void Game::DrawTooltipBar(float x, float y, float w) {
    if (tip.empty()) return;
    float ly = y;
    for (auto& l : WrapText(A.fontSmall, tip, 18, 1, w)) {
        DrawTextEx(A.fontSmall, l.c_str(), {x, ly}, 18, 1, Color{150, 160, 175, 255});
        ly += 20.f;
    }
}


void Game::DrawTitle() {
    int W = GetScreenWidth(), H = GetScreenHeight();
    tip.clear();
    // translucent - the drifting office renders behind this
    DrawRectangleGradientV(0, 0, W, H, Color{8, 10, 14, 195}, Color{24, 16, 24, 210});
    std::string t = JS(Cfg()["game"], "title", "EXIT INTERVIEW");
    Vector2 ts = MeasureTextEx(A.fontBig, t.c_str(), 77, 2);
    DrawTextEx(A.fontBig, t.c_str(), {W / 2.f - ts.x / 2, H * 0.22f}, 77, 2, Color{220, 215, 205, 255});
    std::string sub = JS(Cfg()["game"], "subtitle");
    ts = MeasureTextEx(A.font, sub.c_str(), 24, 1);
    DrawTextEx(A.font, sub.c_str(), {W / 2.f - ts.x / 2, H * 0.22f + 80}, 24, 1, Color{140, 150, 160, 255});
    std::string ntxt = night == 0 ? "ORIENTATION SHIFT - TRAINING  [R to skip]"
                     : night == 1 ? "NIGHT ONE"
                     : ("NIGHT " + std::to_string(night) + "  [R to wipe record]");
    if (weekDone) ntxt += "   - WEEK SURVIVED";
    ts = MeasureTextEx(A.fontSmall, ntxt.c_str(), 20, 1);
    DrawTextEx(A.fontSmall, ntxt.c_str(), {W / 2.f - ts.x / 2, H * 0.22f + 112}, 20, 1, Color{170, 140, 80, 255});
    if (!lastEval.empty()) {
        std::string ev = "LAST SHIFT EVALUATION: " + lastEval;
        ts = MeasureTextEx(A.fontSmall, ev.c_str(), 18, 1);
        DrawTextEx(A.fontSmall, ev.c_str(), {W / 2.f - ts.x / 2, H * 0.22f + 136}, 18, 1, Color{130, 135, 145, 255});
    }

    // now-playing chip, top-left: whatever the menu rotation landed on,
    // with elapsed/total time and a progress strip
    if (audio.MenuPlaying()) {
        float played = audio.MenuTimePlayed(), len = audio.MenuTimeLength();
        std::string name = audio.MenuTrackName();
        Rectangle chip{16.f, 16.f, 296.f, 64.f};
        DrawRectangleRounded(chip, 0.16f, 6, Color{14, 16, 22, 220});
        DrawRectangleRoundedLinesEx(chip, 0.16f, 6, 1.f, Color{70, 80, 95, 170});
        DrawTextEx(A.fontSmall, "NOW PLAYING", {chip.x + 12.f, chip.y + 7.f}, 15, 1,
                   Color{118, 126, 140, 255});
        std::string dn = name;
        while (MeasureTextEx(A.font, dn.c_str(), 19, 1).x > chip.width - 24.f && dn.size() > 10)
            dn = dn.substr(0, dn.size() - 4) + "...";
        DrawTextEx(A.font, dn.c_str(), {chip.x + 12.f, chip.y + 25.f}, 19, 1,
                   Color{205, 210, 218, 255});
        Rectangle bar{chip.x + 12.f, chip.y + 50.f, chip.width - 104.f, 5.f};
        DrawRectangleRounded(bar, 0.5f, 4, Color{38, 42, 52, 255});
        if (len > 0.f)
            DrawRectangleRounded({bar.x, bar.y, bar.width * std::clamp(played / len, 0.f, 1.f), bar.height},
                                 0.5f, 4, Color{150, 128, 88, 255});
        char tb[40];
        snprintf(tb, 40, "%d:%02d / %d:%02d", (int)played / 60, (int)played % 60,
                 (int)len / 60, (int)len % 60);
        DrawTextEx(A.fontSmall, tb, {chip.x + chip.width - 84.f, chip.y + 44.f}, 15, 1,
                   Color{140, 148, 160, 255});
    }

    if (menuPage == 1) { DrawSettings(); return; }

    // main menu - click or keyboard shortcuts (suppressed while the update
    // popup is modal so clicks can't pass through the veil)
    const float bw = 380.f, bh = 52.f, gap = 13.f;
    Rectangle r{W / 2.f - bw / 2, H * 0.50f, bw, bh};
    if (!updOpen) {
        if (MenuBtn(r, night == 0 ? "BEGIN THE ORIENTATION SHIFT" : "BEGIN THE NIGHT SHIFT",
                   night == 0 ? "A guided first shift - two appointments, hands-on lessons."
                              : "Clock in at 6:00 PM. Survive until 6:00 AM.")) { NewShift(false); return; }
        r.y += bh + gap;
        if (saveExists) {
            if (MenuBtn(r, "CONTINUE FROM SAVE", "Resume the shift exactly where you left it.")) {
                if (Load()) { mode = Mode::Roam; DisableCursor(); Notify("Save loaded."); }
                return;
            }
            r.y += bh + gap;
        }
        if (MenuBtn(r, "SETTINGS", "Audio, controls, LLM and general options.")) { menuPage = 1; settingsTab = 0; return; }
        r.y += bh + gap;
        if (MenuBtn(r, "QUIT", "Exit to desktop.")) { quit = true; return; }
    }

    std::string warn = (llm.ProviderName() == "groq" && apiKey.empty())
        ? "no API key set - replies run on the offline stub (SETTINGS > LLM)"
        : "LLM: " + llm.ProviderName() + (hardcodedOnly ? "   |   anomalies: scripted" : "   |   anomalies: scripted + LLM");
    ts = MeasureTextEx(A.fontSmall, warn.c_str(), 18, 1);
    DrawTextEx(A.fontSmall, warn.c_str(), {W / 2.f - ts.x / 2, H - 64.f}, 18, 1,
               apiKey.empty() && llm.ProviderName() == "groq" ? Color{210, 150, 95, 255} : Color{120, 132, 145, 255});
    std::string ctrl = std::string(KeyName(keys.fwd)) + "/" + KeyName(keys.left) + "/" +
        KeyName(keys.back) + "/" + KeyName(keys.right) + " move - " + KeyName(keys.sprint) +
        " sprint - " + KeyName(keys.crouch) + " crouch - " + KeyName(keys.interact) +
        " interact - " + KeyName(keys.swap) + " monitor - ESC pause";
    ts = MeasureTextEx(A.fontSmall, ctrl.c_str(), 18, 1);
    DrawTextEx(A.fontSmall, ctrl.c_str(), {W / 2.f - ts.x / 2, H - 32.f}, 18, 1, Color{135, 142, 155, 255});

    // credits block, bottom-right: makers + AI disclaimer + year/version +
    // repo link
    {
        std::string cr = JS(Cfg()["game"], "credits",
                            "Made by GRKTheGreat & Devin Desktop - 2026 - v1.0.0");
        std::string disc = JS(Cfg()["game"], "ai_disclaimer",
                              "made with AI assistance");
        std::string repo = JS(Cfg()["game"], "repo_url", "github.com/grkthegreat2018-bot/exit-interview");
        ts = MeasureTextEx(A.fontSmall, cr.c_str(), 18, 1);
        DrawTextEx(A.fontSmall, cr.c_str(), {W - ts.x - 16.f, H - 82.f}, 18, 1, Color{145, 152, 165, 255});
        ts = MeasureTextEx(A.fontSmall, disc.c_str(), 18, 1);
        DrawTextEx(A.fontSmall, disc.c_str(), {W - ts.x - 16.f, H - 60.f}, 18, 1, Color{130, 136, 146, 255});
        ts = MeasureTextEx(A.fontSmall, repo.c_str(), 18, 1);
        DrawTextEx(A.fontSmall, repo.c_str(), {W - ts.x - 16.f, H - 38.f}, 18, 1, Color{110, 160, 205, 255});
    }
    // updater: auto-open once when a release appears; a corner pill re-opens
    // it after LATER; the popup itself is modal over the whole menu
    if (updater) {
        auto us = updater->GetState();
        if (!updSeen && (us == GameUpdater::State::Available ||
                         us == GameUpdater::State::Applied)) {
            updOpen = 1; updSeen = true;
        }
        if (updOpen) {
            DrawUpdatePopup();
        } else if (us == GameUpdater::State::Available ||
                   us == GameUpdater::State::Downloading ||
                   us == GameUpdater::State::Ready) {
            std::string pill = "UPDATE " + updater->NewVersion() +
                (us == GameUpdater::State::Ready ? " - READY" :
                 us == GameUpdater::State::Downloading ? " - downloading" : " AVAILABLE");
            Vector2 ps = MeasureTextEx(A.fontSmall, pill.c_str(), 17, 1);
            Rectangle pr{W - ps.x - 40.f, 16.f, ps.x + 24.f, 36.f};
            if (MenuBtn(pr, pill.c_str(), "A new release is out on github - view the changelog."))
                updOpen = 1;
        }
    }
    if (!tip.empty()) DrawTooltipBar(W / 2.f - 300.f, r.y + bh + 18.f, 600.f);
}


// modal update card on the title screen: version delta, scrollable
// changelog, and per-state actions (download -> restart & apply)
void Game::DrawUpdatePopup() {
    int W = GetScreenWidth(), H = GetScreenHeight();
    auto us = updater->GetState();
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 170});
    Rectangle card{W / 2.f - 380.f, H / 2.f - 250.f, 760.f, 500.f};
    DrawRectangleRounded({card.x + 6.f, card.y + 8.f, card.width, card.height},
                         0.045f, 8, Color{0, 0, 0, 130});
    DrawRectangleRounded(card, 0.045f, 8, Color{22, 26, 34, 248});
    DrawRectangleRoundedLinesEx(card, 0.045f, 8, 1.5f, Color{96, 120, 150, 220});
    const char* head =
        us == GameUpdater::State::Available   ? "UPDATE AVAILABLE" :
        us == GameUpdater::State::Downloading ? "DOWNLOADING UPDATE" :
        us == GameUpdater::State::Ready       ? "UPDATE READY TO INSTALL" :
        us == GameUpdater::State::Applying    ? "APPLYING UPDATE" :
        us == GameUpdater::State::Applied     ? "UPDATE APPLIED" :
        us == GameUpdater::State::Error       ? "UPDATE FAILED" :
                                              "CHECKING FOR UPDATES";
    DrawTextEx(A.font, head, {card.x + 28.f, card.y + 22.f}, 30, 1, Color{220, 215, 205, 255});
    std::string vline;
    if (us == GameUpdater::State::Applied)
        vline = "now running " + updater->NewVersion();
    else if (us != GameUpdater::State::Error)
        vline = "v" + updater->CurrentVersion() + "   ->   " + updater->NewVersion();
    DrawTextEx(A.fontSmall, vline.c_str(), {card.x + 30.f, card.y + 58.f}, 19, 1,
               Color{190, 160, 100, 255});
    std::string note = "save files, settings and your key are never touched - " \
                       "only files that changed are replaced";
    DrawTextEx(A.fontSmall, note.c_str(), {card.x + 30.f, card.y + 80.f}, 16, 1,
               Color{110, 118, 130, 255});

    // scrollable changelog
    Rectangle log{card.x + 28.f, card.y + 104.f, card.width - 56.f, card.height - 104.f - 96.f};
    DrawRectangleRounded(log, 0.06f, 6, Color{12, 14, 18, 255});
    std::string body = updater->Changelog();
    if (body.empty()) body = us == GameUpdater::State::Error ? updater->Error()
                             : "(no changelog in the release notes)";
    // WrapText collapses \n - wrap each changelog line separately so bullets
    // keep their shape
    std::vector<std::string> lines;
    {
        std::string raw = body; size_t lp = 0;
        while (lp <= raw.size()) {
            size_t nl = raw.find('\n', lp);
            std::string one = raw.substr(lp, nl == std::string::npos ? nl : nl - lp);
            lp = (nl == std::string::npos) ? raw.size() + 1 : nl + 1;
            while (!one.empty() && (one.back() == '\r' || one.back() == ' ')) one.pop_back();
            if (one.empty()) { lines.push_back(""); continue; }
            auto wl = WrapText(A.fontSmall, one, 17, 1, log.width - 28.f);
            lines.insert(lines.end(), wl.begin(), wl.end());
        }
    }
    float logH = lines.size() * 22.f + 20.f;
    float maxScroll = std::max(0.f, logH - log.height);
    if (CheckCollisionPointRec(GetMousePosition(), log))
        updScroll = std::clamp(updScroll - GetMouseWheelMove() * 30.f, 0.f, maxScroll);
    BeginScissorMode((int)log.x, (int)log.y, (int)log.width, (int)log.height);
    float ly = log.y + 10.f - updScroll;
    for (auto& s : lines) {
        DrawTextEx(A.fontSmall, s.c_str(), {log.x + 14.f, ly}, 17, 1, Color{168, 176, 188, 255});
        ly += 22.f;
    }
    EndScissorMode();
    if (maxScroll > 0.f) {   // scrollbar thumb
        float th = std::max(24.f, log.height * log.height / logH);
        float ty = log.y + (log.height - th) * (updScroll / maxScroll);
        DrawRectangleRounded({log.x + log.width - 8.f, ty, 5.f, th}, 0.5f, 4,
                             Color{90, 100, 118, 200});
    }

    // footer actions per state
    float by = card.y + card.height - 68.f;
    auto Btn = [&](float bx, float bw, const char* label, const char* tt) {
        return MenuBtn({bx, by, bw, 44.f}, label, tt);
    };
    if (us == GameUpdater::State::Available) {
        if (Btn(card.x + 28.f, 300.f, "DOWNLOAD UPDATE", "Fetch the new build from github now."))
            updater->BeginDownload();
        if (Btn(card.x + card.width - 148.f, 120.f, "LATER", "Back to the menu - the badge stays in the corner."))
            updOpen = 0;
    } else if (us == GameUpdater::State::Downloading) {
        float p = updater->Progress();
        Rectangle pb{card.x + 28.f, by + 8.f, card.width - 56.f, 26.f};
        DrawRectangleRounded(pb, 0.4f, 6, Color{34, 38, 48, 255});
        DrawRectangleRounded({pb.x, pb.y, pb.width * p, pb.height}, 0.4f, 6,
                             Color{120, 140, 170, 255});
        char pb2[32]; snprintf(pb2, 32, "%d%%", (int)(p * 100.f + 0.5f));
        Vector2 ts2 = MeasureTextEx(A.fontSmall, pb2, 17, 1);
        DrawTextEx(A.fontSmall, pb2, {pb.x + pb.width / 2 - ts2.x / 2, pb.y + 4.f}, 17, 1, WHITE);
        if (Btn(card.x + card.width - 148.f, 120.f, "LATER", "Keep downloading in the background."))
            updOpen = 0;
    } else if (us == GameUpdater::State::Ready) {
        if (Btn(card.x + 28.f, 300.f, "RESTART & APPLY", "Close, patch the install, and relaunch automatically.")) {
            if (updater->ApplyAndRelaunch()) { quit = true; return; }
        }
        if (Btn(card.x + card.width - 148.f, 120.f, "LATER", "Apply on a later launch instead."))
            updOpen = 0;
    } else if (us == GameUpdater::State::Applying) {
        DrawTextEx(A.font, "closing to patch - the game will relaunch itself",
                   {card.x + 28.f, by + 8.f}, 21, 1, Color{160, 170, 180, 255});
    } else if (us == GameUpdater::State::Applied) {
        if (Btn(card.x + card.width - 148.f, 120.f, "OK", "Back to the menu."))
            updOpen = 0;
    } else if (us == GameUpdater::State::Error) {
        if (Btn(card.x + card.width - 148.f, 120.f, "CLOSE", "Back to the menu."))
            updOpen = 0;
    }
}


// API-key text field + PASTE button, shared by title settings and pause menu.
// Typing masks the key but shows length + last 4 chars so entry is verifiable.
void Game::ApiKeyField(Rectangle field, Rectangle pasteBtn) {
    Vector2 m = GetMousePosition();
    bool hov = CheckCollisionPointRec(m, field);
    if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) focusApiKey = true;
    else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !hov &&
             !CheckCollisionPointRec(m, pasteBtn)) focusApiKey = false;
    DrawRectangleRounded(field, 0.12f, 6, Color{40, 44, 54, 255});
    DrawRectangleRoundedLinesEx(field, 0.12f, 6, 2,
        focusApiKey ? Color{150, 200, 235, 255} : Color{70, 80, 95, 255});
    if (focusApiKey) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (ch >= 32 && ch < 127 && apiKey.size() < 512) apiKey += (char)ch;
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !apiKey.empty())
            apiKey.pop_back();
        TryPaste(apiKey, 512, true);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
            focusApiKey = false;
            ApplyProvider();
        }
    }
    // paste button: fills the field from the clipboard, with visible feedback
    bool phov = CheckCollisionPointRec(m, pasteBtn);
    DrawRectangleRounded(pasteBtn, 0.12f, 6, phov ? Color{85, 100, 125, 255} : Color{55, 62, 75, 255});
    Vector2 pts = MeasureTextEx(A.font, "PASTE", 22, 1);
    DrawTextEx(A.font, "PASTE", {pasteBtn.x + (pasteBtn.width - pts.x) / 2, pasteBtn.y + 12.f}, 22, 1, WHITE);
    if (phov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const char* c = GetClipboardText();
        apiKey.clear();
        if (c) for (const char* p = c; *p && apiKey.size() < 512; p++)
            if (*p >= 33 && *p < 127) apiKey += *p;
        Notify(apiKey.empty() ? "Clipboard empty or unreadable - type the key instead."
                              : "API key pasted (" + std::to_string(apiKey.size()) + " chars).");
        ApplyProvider();
    }
    std::string disp;
    if (apiKey.empty()) disp = "GROQ API KEY";
    else {
        disp = std::string(std::min<size_t>(apiKey.size(), 18), '*');
        if (apiKey.size() > 4) disp += apiKey.substr(apiKey.size() - 4);   // last-4 confirms the right key
        disp += " (" + std::to_string(apiKey.size()) + ")";
    }
    DrawTextEx(A.font, ("KEY: " + disp + (focusApiKey ? "_" : "")).c_str(),
               {field.x + 12.f, field.y + 12.f}, 18, 1,
               apiKey.empty() ? Color{130, 135, 145, 255} : Color{190, 200, 210, 255});
}


void Game::DrawSettings() {
    int W = GetScreenWidth(), H = GetScreenHeight();
    float pw = std::min(940.f, W - 60.f), ph = std::min(600.f, H - 80.f);
    Rectangle panel{W / 2.f - pw / 2, H / 2.f - ph / 2, pw, ph};
    DrawRectangleRounded(panel, 0.03f, 8, Color{14, 16, 20, 238});
    DrawRectangleRoundedLinesEx(panel, 0.03f, 8, 2, Color{60, 70, 90, 255});
    DrawTextEx(A.fontBig, "SETTINGS", {panel.x + 28.f, panel.y + 16.f}, 34, 1, Color{210, 205, 195, 255});

    // ---- tabs ----
    const char* tabs[4] = {"GENERAL", "AUDIO", "CONTROLS", "LLM"};
    const char* tabTips[4] = {
        "Camera feel and your shift record. Hover any setting for an explanation.",
        "Volume mix. Master scales everything else; keep ambience audible to hear instability cues.",
        "Rebind movement and interaction keys. Click a bind, then press the new key. ESC cancels.",
        "The model that writes visitor dialogue, phone calls, and judges your supervisor report."
    };
    float tw = (pw - 56.f - 3 * 10.f) / 4.f;
    for (int i = 0; i < 4; i++) {
        Rectangle tr{panel.x + 28.f + i * (tw + 10.f), panel.y + 62.f, tw, 38.f};
        bool hov = CheckCollisionPointRec(GetMousePosition(), tr);
        if (hov) tip = tabTips[i];
        bool on = settingsTab == i;
        DrawRectangleRounded(tr, 0.14f, 6,
            on ? Color{95, 115, 145, 255} : hov ? Color{75, 85, 105, 255} : Color{45, 50, 62, 255});
        Vector2 ts = MeasureTextEx(A.font, tabs[i], 20, 1);
        DrawTextEx(A.font, tabs[i], {tr.x + (tr.width - ts.x) / 2, tr.y + 9.f}, 20, 1, WHITE);
        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { settingsTab = i; rebinding = -1; }
    }

    // ---- rows ----
    Rectangle r{panel.x + 28.f, panel.y + 118.f, pw - 56.f, 44.f};
    auto next = [&] { r.y += 52.f; };
    auto backBtn = [&] {
        Rectangle br{panel.x + 28.f, panel.y + ph - 52.f, 180.f, 38.f};
        if (MenuBtn(br, "< BACK", "Return to the previous screen.")) menuPage = 0;
    };

    if (settingsTab == 0) {
        SliderRow(r, "MOUSE SENSITIVITY", sens, 0.02f, 0.40f,
            "How fast the view turns. 0.12 is the default feel; lower is steadier, higher is twitchier.");
        next();
        std::string nl = "SHIFT RECORD: NIGHT " + std::to_string(night) + "  (click to reset)";
        if (MenuBtn(r, nl.c_str(), "Reset the night counter and your warning record - back to the training shift.")) {
            night = 0; nightBeforeEnv = -1; seenNames.clear(); lastEval.clear(); SaveSettings();
        }
        next();
        if (MenuBtn(r, "RESET ALL SETTINGS", "Restore sensitivity, volumes, keybinds and LLM options to defaults.")) {
            sens = JF(Cfg()["game"], "mouse_sensitivity", 0.12f);
            volMaster = volSfx = volAmb = volMusic = volDrone = 1.f;
            musicEnabled = true;
            keys = KeyMap{};
            llmEndpoint.clear(); llmModel.clear(); apiKey.clear();
            hardcodedOnly = true; ad.freeform = false;
            ApplyAudio(); ApplyProvider();
        }
        next();
        if (MenuBtn(r, "EXPORT PROFILE",
            "Write settings + save (including LLM provider/API key) into ei_profile.json next to the exe. Copy it into a new build's folder to migrate everything at once. Contains your API key - keep it private.")) {
            ExportProfile();
        }
        next();
        if (MenuBtn(r, "IMPORT PROFILE",
            "Load ei_profile.json from next to the exe - restores settings, API key and a saved shift. (A new build does this automatically on first launch if it finds one.)")) {
            if (ImportProfile())
                Notify("Profile imported - settings, API key and save restored.");
            else
                Notify("No ei_profile.json found next to the exe, or it was empty.");
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) SaveSettings();
    }
    else if (settingsTab == 1) {
        SliderRow(r, "MASTER VOLUME", volMaster, 0.f, 2.f,
            "Overall volume for everything the game plays. Up to 200%."); next();
        SliderRow(r, "EFFECTS VOLUME", volSfx, 0.f, 2.f,
            "Knocks, the phone ring, stingers, UI ticks and footsteps. The danger cues live here."); next();
        SliderRow(r, "AMBIENCE VOLUME", volAmb, 0.f, 2.f,
            "The fluorescent room tone that loops under everything."); next();
        SliderRow(r, "DRONE VOLUME", volDrone, 0.f, 2.f,
            "The instability drone that swells as anomalies gather. The dread meter."); next();
        SliderRow(r, "MUSIC VOLUME", volMusic, 0.f, 2.f,
            "The menu themes - music on the title and ending screens."); next();
        if (MenuBtn(r, musicEnabled ? "MUSIC: ON" : "MUSIC: OFF",
            "Kill or restore the title/dead-screen music entirely - the now-playing chip goes with it.")) {
            musicEnabled = !musicEnabled;
            ApplyAudio();   // stops the stream (and chip) instantly when toggling off
            SaveSettings();
        }
        next();
        // sliders apply live while dragging - audible feedback beats "release to hear"
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) ApplyAudio();
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) { ApplyAudio(); SaveSettings(); }
    }
    else if (settingsTab == 2) {
        const char* bl[8] = {"MOVE FORWARD", "MOVE BACK", "STRAFE LEFT", "STRAFE RIGHT",
                             "SPRINT (hold)", "CROUCH / HIDE (hold)", "INTERACT", "MONITOR / VISITOR"};
        const char* bt[8] = {
            "Walk forward while roaming.",
            "Walk backward while roaming.",
            "Sidestep left - while seated this leans left to peek past the monitor.",
            "Sidestep right - while seated this leans right to peek.",
            "Hold to run. Louder footsteps.",
            "Hold to crouch. At your desk this hides you under it during a breach.",
            "Use doors, blinds, the phone, chairs - whatever the prompt shows.",
            "While seated, swap between the computer and facing your visitor."
        };
        int* bk[8] = {&keys.fwd, &keys.back, &keys.left, &keys.right,
                      &keys.sprint, &keys.crouch, &keys.interact, &keys.swap};
        for (int i = 0; i < 8; i++) {
            Rectangle kb{r.x + r.width - 170.f, r.y + 6.f, 150.f, 32.f};
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            if (hov) tip = bt[i];
            DrawRectangleRounded(r, 0.12f, 6, Color{40, 44, 54, 255});
            DrawTextEx(A.font, bl[i], {r.x + 14.f, r.y + 11.f}, 20, 1, Color{190, 200, 210, 255});
            bool on = rebinding == i;
            DrawRectangleRounded(kb, 0.16f, 6, on ? Color{150, 90, 60, 255} : Color{60, 68, 82, 255});
            DrawRectangleRoundedLinesEx(kb, 0.16f, 6, 1.5f,
                on ? Color{255, 170, 90, 255} : Color{90, 100, 120, 255});
            std::string kn = on ? "PRESS KEY" : KeyName(*bk[i]);
            Vector2 ts = MeasureTextEx(A.font, kn.c_str(), 18, 1);
            DrawTextEx(A.font, kn.c_str(), {kb.x + (kb.width - ts.x) / 2, kb.y + 8.f}, 18, 1, WHITE);
            if (CheckCollisionPointRec(GetMousePosition(), kb) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                rebinding = i;
            next();
        }
        // capture the next keypress for the armed bind
        if (rebinding >= 0) {
            int kc = -1;
            for (int k = 32; k < 400; k++) if (IsKeyPressed(k)) { kc = k; break; }
            if (kc == KEY_ESCAPE) rebinding = -1;                       // cancel
            else if (kc > 0) {
                int* bk2[8] = {&keys.fwd, &keys.back, &keys.left, &keys.right,
                               &keys.sprint, &keys.crouch, &keys.interact, &keys.swap};
                int old = *bk2[rebinding];
                for (int j = 0; j < 8; j++)
                    if (j != rebinding && *bk2[j] == kc) *bk2[j] = old;  // swap conflicts
                *bk2[rebinding] = kc;
                rebinding = -1;
                SaveSettings();
            }
        }
    }
    else {
        std::string prov = "PROVIDER: " + llm.ProviderName();
        if (MenuBtn(r, prov.c_str(),
            "groq = cloud API (needs an API key). local = your own endpoint such as Ollama. stub = offline keyword replies, no network.")) {
            providerIdx = (providerIdx + 1) % 3;
            ApplyProvider();
        }
        next();
        ApiKeyField(Rectangle{r.x, r.y, r.width - 120.f, r.height},
                    Rectangle{r.x + r.width - 110.f, r.y, 110.f, r.height});
        if (CheckCollisionPointRec(GetMousePosition(), r))
            tip = "API key for the groq provider. Click and type, or PASTE. Stored locally in settings.json.";
        next();
        TextRow(r, "ENDPOINT", llmEndpoint, 2,
            "Chat-completions URL. Empty uses the config default (api.groq.com or localhost:11434).");
        next();
        TextRow(r, "MODEL", llmModel, 3,
            "Model name sent to the endpoint. Empty uses the config default for the chosen provider.");
        next();
        std::string anL = std::string("ANOMALIES: ") + (hardcodedOnly ? "SCRIPTED ONLY" : "SCRIPTED + LLM");
        if (MenuBtn(r, anL.c_str(),
            "Scripted uses only the hand-authored anomaly table. +LLM also lets the director remix furniture and ring you with generated callers.")) {
            hardcodedOnly = !hardcodedOnly;
            ad.freeform = !hardcodedOnly;
            SaveSettings();
        }
        next();
        if (MenuBtn(r, "PING PROVIDER", "Send a one-line request to verify the provider, endpoint, model and key actually work.")) {
            LLMClient::Request q;
            q.user = "Reply with exactly one word: ONLINE.";
            q.maxTokens = 24;
            testReq = llm.Submit(q);
            testResult = "pinging...";
        }
        if (!testResult.empty())
            DrawTextEx(A.fontSmall, testResult.c_str(), {r.x + r.width - 340.f, r.y + 14.f},
                       17, 1, Color{160, 170, 180, 255});
        next();
    }
    backBtn();
    // controls tab carries a second bottom button on the right
    if (settingsTab == 2) {
        Rectangle rr{panel.x + pw - 208.f, panel.y + ph - 52.f, 180.f, 38.f};
        if (MenuBtn(rr, "RESET BINDS", "Restore the default control layout (WASD, SHIFT, Q, E, TAB).")) {
            keys = KeyMap{}; SaveSettings();
        }
        DrawTooltipBar(panel.x + 230.f, panel.y + ph - 44.f, pw - 460.f);
    } else
        DrawTooltipBar(panel.x + 230.f, panel.y + ph - 44.f, pw - 260.f);
}


void Game::DrawPause() {
    int W = GetScreenWidth(), H = GetScreenHeight();
    tip.clear();
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 170});
    std::string t = pauseReason.empty() ? "PAUSED" : pauseReason;
    Vector2 ts = MeasureTextEx(A.fontBig, t.c_str(), 54, 1);
    DrawTextEx(A.fontBig, t.c_str(), {W / 2.f - ts.x / 2, H * 0.16f}, 54, 1, Color{215, 210, 200, 255});
    if (menuPage == 1) { DrawSettings(); return; }
    if (!pauseReason.empty()) {
        float ry = H * 0.26f;
        for (auto& l : WrapText(A.font, pauseReason, 24, 1, W * 0.7f)) {
            DrawTextEx(A.font, l.c_str(), {W / 2.f - MeasureTextEx(A.font, l.c_str(), 24, 1).x / 2, ry}, 24, 1, Color{235, 190, 110, 255});
            ry += 28;
        }
    }
    const float bw = 380.f, bh = 52.f, gap = 13.f;
    Rectangle r{W / 2.f - bw / 2, H * 0.36f, bw, bh};
    bool seated = pauseReturn == Mode::SitVisitor || pauseReturn == Mode::SitComputer ||
                  pauseReturn == Mode::Unseating;
    if (MenuBtn(r, "RESUME", "Back to the shift.")) {
        menuPage = 0;
        mode = pauseReturn;
        if (pauseReturn == Mode::Roam) DisableCursor();
        return;
    }
    r.y += bh + gap;
    if (seated) {   // paused mid-unseat or via handset - offer the way out of the chair
        if (MenuBtn(r, "STAND UP", "Leave the desk and get back on your feet.")) {
            menuPage = 0;
            if (pauseReturn != Mode::Unseating) StandUp();   // already blending? let it finish
            else mode = Mode::Unseating;
            return;
        }
        r.y += bh + gap;
    }
    if (MenuBtn(r, "SETTINGS", "Audio, controls, LLM and general options.")) { menuPage = 1; settingsTab = 0; return; }
    r.y += bh + gap;
    if (MenuBtn(r, "SAVE & TITLE", "Save the shift and return to the main menu.")) {
        menuPage = 0; Save(); saveExists = true; mode = Mode::Title; return;
    }
    r.y += bh + gap;
    if (MenuBtn(r, "RESTART SHIFT", "Abandon this shift and start a fresh one.")) { menuPage = 0; NewShift(false); return; }
    r.y += bh + gap;
    if (MenuBtn(r, "QUIT GAME", "Exit to desktop without saving.")) { quit = true; return; }
    if (!tip.empty()) DrawTooltipBar(W / 2.f - 300.f, r.y + bh + 18.f, 600.f);
}
