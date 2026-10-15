#include "fakeos.h"
#include "assets.h"

void FakeOS::Panel(Rectangle r, Color c) const {
    DrawRectangleRounded(r, 0.04f, 6, c);
    DrawRectangleRoundedLinesEx(r, 0.04f, 6, 2, Color{70, 80, 95, 255});
}

bool FakeOS::Button(const Assets& a, Rectangle r, const std::string& label, bool disabled) {
    Vector2 m = mouse;
    bool hov = interactive && !disabled && CheckCollisionPointRec(m, r);
    Color bg = disabled ? Color{50, 52, 60, 255} : hov ? Color{95, 120, 150, 255} : Color{70, 84, 105, 255};
    DrawRectangleRounded(r, 0.12f, 6, bg);
    float fs = 22.f;
    Vector2 ts = MeasureTextEx(a.font, label.c_str(), fs, 1);
    DrawTextEx(a.font, label.c_str(), Vector2{r.x + (r.width - ts.x) / 2, r.y + (r.height - fs) / 2 - 1}, fs, 1,
               disabled ? Color{130, 130, 135, 255} : WHITE);
    return hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void FakeOS::TextField(const Assets& a, Rectangle r, std::string& text, bool& focus, int maxLen, bool& reqFocus) {
    Vector2 m = mouse;
    bool ownsKeys = acceptsText;   // typing is keyboard input - it must not
                                   // depend on where the mouse happens to hover
    if (ownsKeys && interactive && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        bool nf = CheckCollisionPointRec(m, r);
        if (nf && !focus) focusGrace = 0.22f;
        focus = nf;
    }
    if (reqFocus) {
        focus = true; reqFocus = false;
        focusGrace = 0.22f;
        while (GetCharPressed() > 0) {}   // drop the interact keypress itself
    }
    DrawRectangleRounded(r, 0.08f, 6, Color{18, 20, 26, 255});
    DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, focus ? Color{150, 200, 235, 255} : Color{70, 80, 95, 255});
    // [<] delete button inside the field - click removes one char (for mistakes)
    Rectangle del{r.x + r.width - 34.f, r.y + 5.f, 28.f, r.height - 10.f};
    bool delHov = ownsKeys && CheckCollisionPointRec(m, del);
    DrawRectangleRounded(del, 0.18f, 4, delHov ? Color{110, 62, 62, 255} : Color{52, 46, 50, 255});
    DrawTextEx(a.fontMono, "<", Vector2{del.x + 10.f, del.y + (del.height - 18.f) / 2}, 22, 1, Color{230, 200, 200, 255});
    if (delHov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !text.empty()) { text.pop_back(); focus = true; }
    if (focusGrace > 0) focusGrace -= GetFrameTime();
    if (ownsKeys) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (focus && focusGrace <= 0 && ch >= 32 && ch <= 255 && (int)text.size() < maxLen)
                text += CodepointToUtf8((uint32_t)ch);
    }
    if (focus && ownsKeys && focusGrace <= 0) {
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty())
            text.pop_back();
        TryPaste(text, maxLen);
    }
    std::string disp = text + (focus && ((int)(GetTime() * 2) % 2 == 0) ? "_" : "");
    BeginScissorMode((int)r.x + 6, (int)r.y, (int)r.width - 46, (int)r.height);
    DrawTextEx(a.fontMono, disp.c_str(), Vector2{r.x + 8, r.y + (r.height - 18) / 2}, 22, 1, Color{200, 220, 230, 255});
    EndScissorMode();
}

void FakeOS::Draw(const Assets& a, int W, int H) {
    typing = acceptsText && (focusReport || focusCall);
    // monitor bezel + wallpaper (kept bright enough to read as "on" in-world)
    DrawRectangle(0, 0, W, H, Color{10, 12, 16, 255});
    Rectangle scr(24.f, 24.f, (float)W - 48.f, (float)H - 48.f);
    DrawRectangleGradientV((int)scr.x, (int)scr.y, (int)scr.width, (int)scr.height,
                         Color{46, 66, 92, 255}, Color{30, 42, 60, 255});
    for (int i = 0; i < 24; i++) {
        int y = 24 + i * (int)(scr.height / 24.f);
        DrawLine(24, y, 24 + (int)scr.width, y, Color{40, 55, 76, 255});
    }

    // desktop icons
    struct Icon { App ap; const char* label; };
    Icon icons[] = {{App::Appointments, "APPOINTMENTS"}, {App::Records, "PERSONNEL"},
                    {App::Report, "REPORT"}, {App::Phone, "PHONE"}, {App::Paperwork, "PAPERWORK"}};
    float iy = 60.f;
    for (auto& ic : icons) {
        Rectangle r(50.f, iy, 150.f, 58.f);
        Color c = (app == ic.ap) ? Color{85, 110, 145, 255} : Color{52, 68, 92, 255};
        DrawRectangleRounded(r, 0.1f, 6, c);
        Vector2 ts = MeasureTextEx(a.fontSmall, ic.label, 19, 1);
        DrawTextEx(a.fontSmall, ic.label, Vector2{r.x + (r.width - ts.x) / 2, r.y + (r.height - 15.f) / 2}, 19, 1,
                   Color{215, 230, 240, 255});
        Vector2 m = mouse;
        if (interactive && CheckCollisionPointRec(m, r) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            app = ic.ap; phoneActive.clear(); callReply.clear(); callWaiting = false;
            focusReport = focusCall = focusPaper = false;   // stale focus would block A/D lean
            if (ic.ap == App::Report) reqFocusReport = true;   // auto-focus so typing just works
        }
        iy += 74.f;
    }

    // sticky note - wrap each line so nothing spills off the card
    Rectangle sn((float)W - 262.f, 50.f, 212.f, 0.f);
    std::vector<std::string> nlines;
    for (auto& line : a.dialogs["sticky_note"]) {
        auto wl = WrapText(a.fontMono, line.get<std::string>(), 17, 1, sn.width - 20.f);
        nlines.insert(nlines.end(), wl.begin(), wl.end());
        nlines.push_back("");
    }
    if (!nlines.empty()) nlines.pop_back();
    sn.height = 16.f + (float)nlines.size() * 19.f;
    DrawRectangleRec(sn, Color{220, 200, 120, 255});
    float ny = sn.y + 8.f;
    for (auto& s : nlines) {
        DrawTextEx(a.fontMono, s.c_str(), Vector2{sn.x + 10.f, ny}, 17, 1, Color{60, 55, 40, 255});
        ny += 19.f;
    }

    // app window
    Rectangle win(240.f, 60.f, (float)W - 540.f, (float)H - 160.f);
    if (app != App::None) {
        Panel(win, Color{30, 38, 50, 245});
        std::string title;
        switch (app) {
            case App::Appointments: title = "Appointment Book - tonight"; break;
            case App::Records: title = "Personnel Record"; break;
            case App::Report: title = "Anomaly Report - OFFICE channel"; break;
            case App::Phone: title = "Phone"; break;
            case App::Paperwork: title = "Overnight Paperwork"; break;
            default: break;
        }
        DrawTextEx(a.font, title.c_str(), Vector2{win.x + 18.f, win.y + 14.f}, 26, 1, Color{170, 200, 220, 255});
        if (Button(a, Rectangle(win.x + win.width - 44.f, win.y + 12.f, 32.f, 28.f), "X")) {
            app = App::None; phoneActive.clear(); focusReport = focusCall = focusPaper = false;
        }
        DrawLine((int)win.x + 14, (int)win.y + 48, (int)(win.x + win.width) - 14, (int)win.y + 48, Color{70, 80, 95, 255});
    }

    if (app == App::Appointments && getAppointments) {
        float y = win.y + 62.f;
        // ~d~ struck (visit done), ~a~ amber ON-GOING, ~r~ repeat booking flag
        for (std::string line : getAppointments()) {
            Color col{200, 215, 225, 255};
            bool strike = false;
            if (line.rfind("~d~", 0) == 0)      { col = {118, 148, 130, 255}; strike = true; line.erase(0, 3); }
            else if (line.rfind("~a~", 0) == 0) { col = {235, 205, 120, 255}; line.erase(0, 3); }
            else if (line.rfind("~r~", 0) == 0) { col = {215, 160, 110, 255}; line.erase(0, 3); }
            DrawTextEx(a.fontMono, line.c_str(), Vector2{win.x + 22.f, y}, 20, 1, col);
            if (strike) {
                float tw = MeasureTextEx(a.fontMono, line.c_str(), 19, 1).x;
                DrawLineEx({win.x + 22.f, y + 10.f}, {win.x + 22.f + tw, y + 10.f}, 1.6f, col);
            }
            y += 24.f;
        }
    } else if (app == App::Records && getRecord) {
        float y = win.y + 62.f;
        for (auto& line : getRecord()) {
            for (auto& l : WrapText(a.fontMono, line, 20, 1, win.width - 44.f)) {
                DrawTextEx(a.fontMono, l.c_str(), Vector2{win.x + 22.f, y}, 20, 1, Color{200, 215, 225, 255});
                y += 22.f;
            }
            y += 4.f;
        }
    } else if (app == App::Report) {
        DrawTextEx(a.fontMono, "Describe what is wrong IN THIS OFFICE (not the visitor, not outside):",
                   Vector2{win.x + 22.f, win.y + 62.f}, 17, 1, Color{180, 190, 200, 255});
        TextField(a, Rectangle(win.x + 22.f, win.y + 92.f, win.width - 44.f, 40.f), reportText, focusReport, 240, reqFocusReport);
        bool submit = Button(a, Rectangle(win.x + 22.f, win.y + 146.f, 160.f, 36.f), "SUBMIT REPORT", reportText.empty());
        if (focusReport && !reportText.empty() && IsKeyPressed(KEY_ENTER)) submit = true;   // enter = submit
        if (submit) {
            if (onSubmitReport) onSubmitReport(reportText);
            reportText.clear(); focusReport = false;
        }
        if (statusT > 0)
            DrawTextEx(a.fontMono, statusLine.c_str(), Vector2{win.x + 22.f, win.y + 196.f}, 20, 1, Color{235, 200, 120, 255});
    } else if (app == App::Paperwork && getPaperwork) {
        // checkbox checklist + a signature - filing one holds instability
        json form = getPaperwork();
        if (form.is_null()) {
            DrawTextEx(a.fontMono, "All overnight paperwork filed. Good clerk.",
                       Vector2{win.x + 22.f, win.y + 62.f}, 20, 1, Color{170, 200, 175, 255});
        } else {
            auto& items = form["items"];
            if (paperChecks.size() != items.size()) { paperChecks.assign(items.size(), 0); paperSign.clear(); }
            if (paperDeniedT > 0) paperDeniedT -= GetFrameTime(); else paperDenied = -1;
            float y = win.y + 66.f;
            for (int i = 0; i < (int)items.size(); i++) {
                // items are {t, verify} objects; bare strings = clerical, always ok
                std::string txt = items[i].is_string() ? items[i].get<std::string>() : JS(items[i], "t");
                std::string ver = items[i].is_string() ? "" : JS(items[i], "verify");
                std::string need = (!ver.empty() && ver != "none" && canCertify) ? canCertify(ver) : "";
                Rectangle cr{win.x + 24.f, y, 24.f, 24.f};
                bool hov = interactive && CheckCollisionPointRec(mouse, cr);
                Color cb = hov ? Color{70, 90, 80, 255} : Color{40, 46, 44, 255};
                if (i == paperDenied) cb = Color{110, 55, 50, 255};   // refused flash
                DrawRectangleRounded(cr, 0.15f, 4, cb);
                DrawRectangleRoundedLinesEx(cr, 0.15f, 4, 2,
                    i == paperDenied ? Color{220, 120, 100, 255} : Color{110, 130, 115, 255});
                if (paperChecks[i])
                    DrawTextEx(a.font, "x", {cr.x + 6.f, cr.y + 1.f}, 22, 1, Color{150, 230, 160, 255});
                DrawTextEx(a.fontMono, txt.c_str(), {cr.x + 36.f, cr.y + 2.f}, 18, 1,
                           need.empty() ? Color{195, 205, 200, 255} : Color{150, 148, 138, 255});
                if (!need.empty())
                    DrawTextEx(a.fontMono, ("needs: " + need).c_str(),
                               {cr.x + 36.f + MeasureTextEx(a.fontMono, txt.c_str(), 18, 1).x + 14.f, cr.y + 4.f},
                               15, 1, Color{220, 180, 105, 255});
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    if (need.empty()) paperChecks[i] = !paperChecks[i];
                    else {                          // can't certify what you didn't do
                        paperDenied = i; paperDeniedT = 1.2f;
                        SetStatus("CANNOT CERTIFY - " + need);
                    }
                }
                y += 34.f;
            }
            y += 10.f;
            DrawTextEx(a.fontMono, (JS(form, "sign", "SIGNATURE") + ":").c_str(),
                       {win.x + 24.f, y}, 17, 1, Color{150, 160, 155, 255});
            TextField(a, Rectangle(win.x + 24.f, y + 24.f, win.width - 200.f, 36.f),
                      paperSign, focusPaper, 40, reqFocusPaper);
            bool all = (int)paperSign.size() >= 3;
            for (auto c : paperChecks) all = all && c;
            bool file = Button(a, Rectangle(win.x + win.width - 160.f, y + 22.f, 130.f, 40.f), "FILE", !all);
            if (focusPaper && all && IsKeyPressed(KEY_ENTER)) file = true;   // enter = file
            if (file) {
                paperChecks.clear(); paperSign.clear(); focusPaper = false;
                if (onPaperworkFiled) onPaperworkFiled();
            }
        }
    } else if (app == App::Phone) {
        if (phoneActive.empty()) {
            DrawTextEx(a.fontMono, "Select a contact:", Vector2{win.x + 22.f, win.y + 62.f}, 20, 1, Color{180, 190, 200, 255});
            if (Button(a, Rectangle(win.x + 22.f, win.y + 92.f, 220.f, 44.f), "SECURITY")) {
                phoneActive = "security"; secKind.clear(); callReply.clear();
            }
            if (Button(a, Rectangle(win.x + 22.f, win.y + 146.f, 220.f, 44.f), "FACILITIES")) {
                phoneActive = "facilities"; callReply = "Facilities. What are you seeing?"; reqFocusCall = true;
            }
            if (Button(a, Rectangle(win.x + 22.f, win.y + 200.f, 220.f, 44.f), "FRONT DESK")) {
                phoneActive = "frontdesk"; callReply = "Front desk. Go ahead."; reqFocusCall = true;
            }
        } else {
            std::string who = phoneActive == "security" ? "SECURITY" : phoneActive == "facilities" ? "FACILITIES" : "FRONT DESK";
            DrawTextEx(a.fontMono, ("LINE: " + who).c_str(), Vector2{win.x + 22.f, win.y + 62.f}, 20, 1, Color{150, 220, 170, 255});
            float y = win.y + 90.f;
            if (!callReply.empty()) {
                for (auto& l : WrapText(a.fontMono, "\"" + callReply + "\"", 20, 1, win.width - 60.f)) {
                    DrawTextEx(a.fontMono, l.c_str(), Vector2{win.x + 22.f, y}, 20, 1, Color{215, 220, 200, 255});
                    y += 22.f;
                }
            }
            if (callWaiting)
                DrawTextEx(a.fontMono, "...", Vector2{win.x + 22.f, y}, 20, 1, Color{150, 160, 170, 255});
            y += 12.f;
            if (phoneActive == "security" && secKind.empty()) {
                // two dispatch types - guards need to know what they're walking into
                DrawTextEx(a.fontMono, "Dispatch for:", Vector2{win.x + 22.f, y + 6.f}, 20, 1, Color{180, 190, 200, 255});
                if (Button(a, Rectangle(win.x + 22.f, y + 34.f, 260.f, 40.f), "INTRUDER - a person")) {
                    secKind = "intruder"; callWaiting = true; callReply.clear();
                    if (onCallSecurity) onCallSecurity("intruder");
                }
                if (Button(a, Rectangle(win.x + 22.f, y + 84.f, 260.f, 40.f), "ANOMALY - not a person")) {
                    secKind = "anomaly"; callWaiting = true; callReply.clear();
                    if (onCallSecurity) onCallSecurity("anomaly");
                }
            } else if (phoneActive != "security") {
                if (phoneActive == "frontdesk" &&
                    Button(a, Rectangle(win.x + 22.f, y + 30.f, 240.f, 40.f), "SEND NEXT VISITOR IN")) {
                    if (onCallNext) { callReply = onCallNext(); callWaiting = false; }
                }
                TextField(a, Rectangle(win.x + 22.f, win.y + win.height - 90.f, win.width - 180.f, 38.f), callInput, focusCall, 220, reqFocusCall);
                bool say = Button(a, Rectangle(win.x + win.width - 150.f, win.y + win.height - 90.f, 120.f, 38.f), "SAY", callInput.empty());
                if (focusCall && !callInput.empty() && IsKeyPressed(KEY_ENTER)) say = true;   // enter = say
                if (say) {
                    if (phoneActive == "facilities" && onCallFacilities) { onCallFacilities(callInput); callWaiting = true; callReply.clear(); }
                    if (phoneActive == "frontdesk" && onCallFrontDesk) { onCallFrontDesk(callInput); callWaiting = true; callReply.clear(); }
                    callInput.clear();
                }
            }
            if (Button(a, Rectangle(win.x + 22.f, win.y + win.height - 44.f, 130.f, 32.f), "HANG UP")) {
                phoneActive.clear(); secKind.clear(); callReply.clear(); callWaiting = false; callInput.clear();
            }
        }
    }

    // taskbar
    DrawRectangle(24, H - 52, W - 48, 28, Color{14, 18, 24, 255});
    DrawTextEx(a.fontSmall, "MERIDIAN HR TERMINAL v3.1", Vector2{36.f, H - 47.f}, 19, 1, Color{135, 155, 172, 255});
    if (clockHour >= 0.f) {
        // right-edge system clock - same dim cyan-grey as the taskbar label
        std::string clk = FmtClock(clockHour);
        float fs = 18.f;
        Vector2 cs = MeasureTextEx(a.fontSmall, clk.c_str(), fs, 1);
        float cx = (float)W - 24.f - 16.f - cs.x;   // 16px padding from taskbar edge
        DrawLine((int)cx - 12, H - 46, (int)cx - 12, H - 30, Color{55, 68, 84, 255});
        DrawTextEx(a.fontSmall, clk.c_str(), Vector2{cx, H - 46.f}, fs, 1, Color{135, 155, 172, 255});
    }

    // cursor (only when the player is aiming at the screen)
    if (interactive) DrawCircleV(mouse, 4, Color{235, 235, 235, 230});
}
