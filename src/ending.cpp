#include "game.h"
#include "hud.h"
#include <rlgl.h>
#include <cstdlib>
#include <cstring>


// shift evaluation: score the night's deeds into a 0-100 grade for the
// ending screen + the title's LAST EVAL line
int Game::EvalScore() const {
    auto d = [&](const char* k) { auto it = deeds.find(k); return it == deeds.end() ? 0 : it->second; };
    int score = 40
        + d("anomaly_resolved") * 8
        + d("interview_done") * 6
        + std::min(paperIdx, 8) * 2
        + d("call_calmed") * 3
        + d("call_handled") * 2
        + d("lore_found") * 2
        + d("clean_shift") * 12
        + d("intruder_survived") * 6
        + d("breach_survived") * 4
        - d("report_rejected") * 4
        - d("call_ignored") * 4
        - d("visitor_missed") * 6
        - d("anomaly_released") * 8
        - ad.ActiveCount() * 8;
    return std::clamp(score, 0, 100);
}


const char* Game::EvalGrade(int score) {
    return score >= 90 ? "S" : score >= 75 ? "A" : score >= 60 ? "B" : score >= 45 ? "C" : "D";
}


// true when the player can see (or is parked watching) a world point - used to
// veto positional anomaly mutations that would visibly glide furniture
bool Game::PlayerSees(Vector3 p) const {
    // parked at the desk counts as watching the whole office
    if (mode == Mode::SitVisitor || mode == Mode::SitComputer) return true;
    Vector3 eye, f;
    if (flycam) {
        // the debug lens is what counts while flying - point it at something
        // and anomalies treat it as watched
        eye = cam.position;
        Vector3 d0{cam.target.x - cam.position.x, cam.target.y - cam.position.y,
                   cam.target.z - cam.position.z};
        float l = std::sqrt(d0.x * d0.x + d0.y * d0.y + d0.z * d0.z);
        f = l > 0.001f ? Vector3{d0.x / l, d0.y / l, d0.z / l} : Vector3{0, 0, 1};
    } else {
        eye = player.Eye();
        f = player.Forward();
    }
    Vector3 d{p.x - eye.x, p.y - eye.y, p.z - eye.z};
    float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist < 0.01f) return true;
    if (dist > 26.f) return false;
    float dot = (d.x * f.x + d.y * f.y + d.z * f.z) / dist;
    return dot > 0.40f;   // ~66 deg half-angle: covers fov + peripheral slack
}


// every booked visit actually finished AND the whole forms queue filed
bool Game::AllDutiesDone() const {
    for (auto& e : vs.Schedule())
        if (!e.value("visited", false)) return false;
    return !paperForms.is_array() || paperIdx >= (int)paperForms.size();
}


// every name on the book was dealt with (interviewed, escorted out, or
// left) and nobody is still mid-visit - paperwork is NOT required: filing
// is score padding, the book is the job. The eval still prices any misses.
bool Game::AllVisitorsDone() const {
    auto& sched = vs.Schedule();
    if (sched.empty()) return false;
    for (auto& e : sched)
        if (!e.value("visited", false)) return false;
    return !vs.HasActiveVisitor() || vs.Cur().state == VState::Gone;
}


// the shared "you survived" path - dawn or early clock-out both land here
void Game::WinShift(bool early) {
    mode = Mode::Win;
    EnableCursor();
    ending = JS(A.dialogs["endings"], early ? "survived_early" : "survived");
    if (ending.empty()) ending = JS(A.dialogs["endings"], "survived");
    if (early) deeds["early_done"]++;
    evalScore = EvalScore();
    lastEval = std::string(EvalGrade(evalScore)) + " (" + std::to_string(evalScore) + ")";
    night++;                          // surviving earns the harder shift
    if (night >= 6 && !weekDone) {    // the scaled campaign runs 5 nights;
        weekDone = true;              // surviving night 5 is the milestone -
        weekJustDone = true;          // endless overtime keeps going after
    }
    SaveSettings();
    audio.Play(AudioBank::Sfx::Whoosh, 0.55f);   // dawn breaks
}


void Game::DrawEnding() {
    int W = GetScreenWidth(), H = GetScreenHeight();
    Color bg = (mode == Mode::Win) ? Color{14, 20, 16, 255} : Color{18, 8, 10, 255};
    DrawRectangle(0, 0, W, H, bg);
    std::string t = (mode == Mode::Win) ? "SHIFT COMPLETE" : deathTitle;
    Color tc = (mode == Mode::Win) ? Color{180, 220, 190, 255} : Color{220, 90, 80, 255};
    DrawTextEx(A.fontBig, t.c_str(), {W / 2.f - MeasureTextEx(A.fontBig, t.c_str(), 65, 2).x / 2, H * 0.22f}, 65, 2, tc);
    if (mode == Mode::Win && weekJustDone) {
        const char* wb = "FULL WEEK SURVIVED";
        DrawTextEx(A.font, wb, {W / 2.f - MeasureTextEx(A.font, wb, 26, 1).x / 2, H * 0.295f}, 26, 1,
                   Color{230, 195, 90, 255});
        const char* wb2 = "the building lets you go. for now.";
        DrawTextEx(A.fontSmall, wb2, {W / 2.f - MeasureTextEx(A.fontSmall, wb2, 16, 1).x / 2, H * 0.325f}, 16, 1,
                   Color{170, 160, 120, 235});
    }
    float y = H * 0.36f;
    for (auto& l : WrapText(A.font, ending, 24, 1, W * 0.6f)) {
        DrawTextEx(A.font, l.c_str(), {W / 2.f - MeasureTextEx(A.font, l.c_str(), 24, 1).x / 2, y}, 24, 1, Color{200, 200, 195, 255});
        y += 30;
    }
    // shift record: what the player actually did tonight
    auto d = [&](const char* k) { auto it = deeds.find(k); return it == deeds.end() ? 0 : it->second; };
    int score = (mode == Mode::Win && evalScore >= 0) ? evalScore : EvalScore();
    const char* grade = (mode == Mode::Win) ? EvalGrade(score) : "F";
    Color dc = (mode == Mode::Win) ? Color{160, 175, 165, 235} : Color{170, 130, 125, 215};
    char buf[96];
    std::vector<std::string> rows;
    snprintf(buf, 96, "INTERVIEWS COMPLETED      %d", d("interview_done")); rows.push_back(buf);
    snprintf(buf, 96, "ANOMALIES RESOLVED        %d  (%d seen, %d still active)",
             d("anomaly_resolved"), d("anomaly_seen"), ad.ActiveCount()); rows.push_back(buf);
    snprintf(buf, 96, "FORMS FILED               %d", paperIdx); rows.push_back(buf);
    snprintf(buf, 96, "REPORTS REJECTED          %d", d("report_rejected")); rows.push_back(buf);
    snprintf(buf, 96, "VISITORS LEFT UNINTERVIEWED %d", d("visitor_missed")); rows.push_back(buf);
    if (d("anomaly_released") > 0) {
        snprintf(buf, 96, "ANOMALIES WAVED THROUGH   %d", d("anomaly_released")); rows.push_back(buf);
    }
    snprintf(buf, 96, "CALLS ANSWERED / IGNORED  %d / %d", d("call_answered"), d("call_ignored")); rows.push_back(buf);
    int surv = d("intruder_survived") + d("breach_survived");
    if (surv > 0) { snprintf(buf, 96, "CLOSE CALLS SURVIVED      %d", surv); rows.push_back(buf); }
    if (d("clean_shift") > 0) rows.push_back("CLEAN SHIFT BONUS       +12");
    y += 14;
    float rx = W / 2.f - 250.f;
    DrawRectangleRounded({rx - 24.f, y - 8.f, 548.f, (float)rows.size() * 26.f + 66.f},
                         0.08f, 6, Color{10, 12, 14, 160});
    for (auto& r : rows) { DrawTextEx(A.fontSmall, r.c_str(), {rx, y}, 20, 1, dc); y += 26; }
    std::string g = "SHIFT EVALUATION: " + std::string(grade) +
                    (mode == Mode::Win ? "  " + std::to_string(score) + "/100" : "");
    DrawTextEx(A.font, g.c_str(), {rx, y + 8.f}, 24, 1,
               (mode == Mode::Win) ? Color{200, 190, 120, 255} : Color{190, 110, 100, 255});
    DrawTextEx(A.font, "[ ENTER ]  Back to title", {W / 2.f - MeasureTextEx(A.font, "[ ENTER ]  Back to title", 24, 1).x / 2, H * 0.85f}, 24, 1, Color{140, 145, 155, 255});
}


void Game::DrawDebugOverlay() const {
    char b[192];
    std::vector<std::string> rows;
    snprintf(b, 192, "MODE %d  hour %.2f  night %d  instab %.1f",
             (int)mode, gameHour, night, ad.Instability());
    rows.push_back(b);
    snprintf(b, 192, "player (%.2f, %.2f, %.2f) yaw %.0f pitch %.0f hide %d",
             player.pos.x, player.pos.y, player.pos.z, player.yaw, player.pitch,
             (int)player.hiding);
    rows.push_back(b);
    snprintf(b, 192, "cam (%.2f, %.2f, %.2f)%s",
             cam.position.x, cam.position.y, cam.position.z, flycam ? "   FLYCAM" : "");
    rows.push_back(b);
    snprintf(b, 192, "door open %d anim %.2f | blinds %d | visitor %d slot %d | ambient %d | crew %d",
             (int)doorOpen, doorAnim, (int)office.blindsOpen, (int)vs.Cur().state,
             vs.ActiveSlot(), (int)vs.ambient.size(), (int)crew.size());
    rows.push_back(b);
    std::string an;
    for (auto& a : ad.Active()) an += a.id + " ";
    rows.push_back("anomalies: " + (an.empty() ? std::string("(none)") : an));
    if (flycam)
        rows.push_back("FLYCAM - WASD fly | SPACE/CTRL up/down | SHIFT fast | F6 drop player | F4 exit");
    float wmax = 0;
    for (auto& s : rows) wmax = std::max(wmax, MeasureTextEx(A.fontSmall, s.c_str(), 16, 1).x);
    float y = 58.f;
    DrawRectangleRounded({8.f, y - 7.f, wmax + 20.f, (float)rows.size() * 20.f + 14.f},
                         0.10f, 4, Color{8, 10, 12, 200});
    for (auto& s : rows) {
        DrawTextEx(A.fontSmall, s.c_str(), {18.f, y}, 16, 1, Color{150, 230, 170, 235});
        y += 20.f;
    }
}


void Game::Jumpscare(const std::string& reason) {
    if (mode == Mode::Jumpscare || mode == Mode::Dead) return;
    if (godMode) {   // EI_GOD: log what would have killed the run and keep going
        TraceLog(LOG_WARNING, "[god] jumpscare suppressed: %s", reason.c_str());
        deathTitle.clear();
        return;
    }
    mode = Mode::Jumpscare;
    scareT = 0;
    // callers may pre-set a specific title (breach deaths) - only default here
    if (deathTitle.empty())
        deathTitle = (reason == "intruder") ? "IT WAS BEHIND YOU" : "THE INTERVIEW IS OVER";
    std::string body = JS(A.dialogs["endings"], reason.c_str());
    ending = body.empty() ? JS(A.dialogs["endings"], "jumpscare") : body;
    audio.Play(AudioBank::Sfx::Stinger, 1.f);
}


void Game::Die(const std::string& title, const std::string& body) {
    if (mode == Mode::Dead) return;
    if (godMode) {
        TraceLog(LOG_WARNING, "[god] death suppressed: %s", title.c_str());
        return;
    }
    mode = Mode::Dead;
    deathTitle = title;
    ending = body;
    EnableCursor();
    audio.Play(AudioBank::Sfx::Stinger, 0.7f);
}


// prefix codes the monitor app parses back out for styling:
//   ~d~ done (struck through)   ~a~ ongoing   ~r~ repeat booking (amber)
std::vector<std::string> Game::AppointmentsLines() {
    std::vector<std::string> out;
    out.push_back("TONIGHT'S EXIT INTERVIEWS");
    out.push_back("");
    auto& sched = vs.Schedule();
    int active = vs.ActiveSlot();
    for (int i = 0; i < (int)sched.size(); i++) {
        auto& e = sched[i];
        std::string an = JS(e, "anomaly", "");
        if (an == "unscheduled") continue;   // genuinely not on the book
        const json* p = vs.RosterFind(JS(e, "visitor"));
        if (!p) continue;
        float t = JF(e, "time", 0);
        int h = (int)t % 24, m = (int)((t - (int)t) * 60);
        char tb[16]; snprintf(tb, 16, "%02d:%02d", h, m);
        std::string line = std::string(tb) + "   " + JS(*p, "name") + "  -  " + JS(*p, "dept");
        // a name booked twice tonight is someone coming BACK - the duplicate
        // stays in the schedule as a "repeat" anomaly (it still arrives and
        // still answers to Security), but the book shows only the first
        // listing. No red flag, no second row - the trick is on the player.
        bool dup = an == "repeat";
        if (!dup) {
            for (int k = 0; k < i; k++)
                if (JS(sched[k], "visitor") == JS(e, "visitor")) { dup = true; break; }
        }
        if (dup) continue;   // hidden even once handled - the record stays clean
        if (e.value("visited", false)) out.push_back("~d~" + line);
        else if (i == active)              out.push_back("~a~" + line + "    ON-GOING");
        else                               out.push_back(line);
    }
    out.push_back("");
    out.push_back("Policy: if a visitor is NOT on this list, call SECURITY.");
    return out;
}


std::vector<std::string> Game::RecordLines() {
    std::vector<std::string> out;
    if (!vs.HasActiveVisitor()) {
        out.push_back("No visitor currently in office.");
        return out;
    }
    const json* p = vs.Cur().person;
    out.push_back("PERSONNEL RECORD");
    out.push_back("");
    out.push_back("NAME:      " + JS(*p, "name"));
    out.push_back("DEPT:      " + JS(*p, "dept"));
    out.push_back("ROLE:      " + JS(*p, "role"));
    out.push_back("EMPLOYED:  " + JS(*p, "employed"));
    out.push_back("REASON:    " + JS(*p, "reason"));
    std::string rs = JS(*p, "reason");
    if (rs.find("Deceased") != std::string::npos)
        out.push_back("*** FILE FLAGGED: DECEASED. IF THIS PERSON IS IN YOUR OFFICE, CALL SECURITY. ***");
    out.push_back("");
    out.push_back("INTERVIEW CHECKLIST:");
    out.push_back("- Name + dept match the APPOINTMENTS book");
    out.push_back("- Ask why they're leaving - compare with REASON above");
    out.push_back("- Answers that contradict this file -> PHONE > Security");
    out.push_back("- Not on tonight's list            -> PHONE > Security");
    out.push_back("- Acts strangely, threatens you    -> PHONE > Security");
    out.push_back("- Everything checks out            -> END INTERVIEW");
    return out;
}
