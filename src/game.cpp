#include "game.h"
#include <rlgl.h>
#include "hud.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <system_error>
#include <vector>

bool Game::Init() {
    // staged boot card: heavy init runs between InitWindow and the first
    // real frame, so keep the window painted and say what it's doing
    auto boot = [](const char* s) {
        // GetTime counts from InitWindow - the [boot] lines show which stage eats the wait
        TraceLog(LOG_INFO, "[boot %.2fs] %s", GetTime(), s);
        BeginDrawing();
        ClearBackground(Color{6, 7, 9, 255});
        DrawText("EXIT INTERVIEW", 40, 40, 22, Color{52, 58, 66, 255});
        DrawText(s, 40, GetScreenHeight() - 56, 18, Color{80, 86, 96, 255});
        EndDrawing();
    };
    boot("loading assets");
    if (!A.Load()) {
        TraceLog(LOG_ERROR, "%s", A.lastError.c_str());
        return false;
    }
    for (auto& w : A.warnings) TraceLog(LOG_WARNING, "assets: %s", w.c_str());
    boot("building the office");
    office.Load(A.office, Cfg());
    json g = Cfg()["game"];
    sens = JF(g, "mouse_sensitivity", 0.12f);
    player.height = JF(g, "player_height", 1.62f);
    player.radius = JF(g, "player_radius", 0.32f);
    player.speed = JF(g, "walk_speed", 3.1f);
    gameHour = JF(g, "shift_start_hour", 18.f);

    Vector3 sp = JV3(A.office["player_spawn"]["pos"]);
    player.pos = sp;
    player.yaw = JF(A.office["player_spawn"], "yaw", 180);

    // the clerk's own body - fixed identity, seen looking down / on title
    playerApp.height = 1.0f; playerApp.build = 1.02f;
    playerApp.skin = Color{196, 162, 132, 255};
    playerApp.shirt = Color{58, 66, 82, 255};    // slate work shirt
    playerApp.pants = Color{34, 34, 40, 255};
    playerApp.hair = Color{46, 38, 32, 255};
    playerApp.hairStyle = 1;                     // buzz - cheap, tidy
    playerApp.badge = true;

    vs.Init(A.visitors, Cfg()["visitor"]);
    ad.Init(A.anomalies, Cfg()["instability"]);
    ad.SetParts(A.anomalyParts);
    ad.seenFn = [this](Vector3 p) { return PlayerSees(p); };
    ad.loreSeen = &loreSeen;       // director prefers callers never met
    vs.loreSeen = &loreSeen;       // book injection prefers personas never met
    boot("synthesizing audio");
    audio.Init();

    cam.up = {0.f, 1.f, 0.f};
    cam.fovy = JF(g, "fov", 68);
    fovBase = cam.fovy;
    cam.projection = CAMERA_PERSPECTIVE;
    cam.position = player.Eye();
    cam.target = {0.f, 1.4f, 0.f};

    osTex = LoadRenderTexture(1248, 768);
    osTexOk = osTex.id != 0;
    if (osTexOk) SetTextureFilter(osTex.texture, TEXTURE_FILTER_BILINEAR);

    boot("compiling shaders");
    fx.Init(JF(g, "render_scale", 1.25f));
    CollectLights();

    // LLM setup: env key wins, otherwise use saved settings
    const char* env = std::getenv(JS(Cfg()["llm"], "api_key_env", "GROQ_API_KEY").c_str());
    if (env && *env) apiKey = env;
    LoadSettings();
    ApplyProvider();
    llm.Start(JI(Cfg()["llm"], "worker_threads", 2));

    // fake OS callbacks
    os.getAppointments = [this] { return AppointmentsLines(); };
    os.getRecord = [this] { return RecordLines(); };
    os.onSubmitReport = [this](const std::string& t) { SubmitReport(t); };
    os.onCallSecurity = [this](const std::string& kind) { CallSecurity(kind); };
    os.onCallFacilities = [this](const std::string& t) { CallFacilities(t); };
    os.onCallFrontDesk = [this](const std::string& t) { CallFrontDesk(t); };
    os.onCallNext = [this]() -> std::string {
        if (vs.HasActiveVisitor()) return "Someone is with you already. Finish that interview first.";
        if (vs.PendingCount() <= 0) return "Nobody else on the book tonight.";
        std::string nm = vs.NextName();
        vs.CallNextEarly(office);
        deeds["visitor_early"]++;
        Notify(nm.empty() ? "Next visitor is on the way up." : nm + " is on the way up.");
        audio.Play(AudioBank::Sfx::Ring, 0.45f);
        return nm.empty() ? "Sending them up now." : "Sending " + nm + " up now.";
    };
    // paperwork: tonight's forms (LLM-generated or stock), filed one per
    // submission - each completed form pauses instability for a stretch
    os.getPaperwork = [this]() -> json {
        if (!paperForms.is_array() || paperIdx >= (int)paperForms.size()) return json();
        return paperForms[paperIdx];
    };
    // a checkbox can only be ticked if the deed behind it actually happened
    os.canCertify = [this](const std::string& verify) -> std::string {
        auto req = VERIFY_REQ.find(verify);
        if (req == VERIFY_REQ.end()) return "";
        auto it = deeds.find(verify);
        return (it != deeds.end() && it->second > 0) ? "" : req->second;
    };
    os.onPaperworkFiled = [this]() {
        paperIdx++;
        float s = JF(Cfg()["instability"], "paperwork_freeze_sec", 25.f);
        ad.FreezeInstability(s);
        Notify("Form filed. The room holds still for a while.");
        os.SetStatus("FILED - instability held " + std::to_string((int)s) + "s");
        audio.Play(AudioBank::Sfx::TypeTick, 0.5f);
    };
    dlg.onSay = [this](const std::string& t) { SayToVisitor(t); };
    phoneDlg.onSay = [this](const std::string& t) { PhoneSay(t); };
    phoneDlg.onEnd = [this] { HangUpPhone(); };
    // no instant credit here: the leave-edge submits the transcript to the
    // intervieweval job, and only a judged-clean wrap-up earns interview_done
    dlg.onEnd = [this] { vs.EndInterview(); dlg.Reset(); Notify("Interview ended."); };

    saveExists = FileExists(JS(Cfg(), "save_file", "save.json").c_str());

    // updater: check github releases once per boot while on the title menu.
    // EI_UPDATE=off disables; EI_UPDATE=fake injects a canned popup for
    // screenshots; a completed apply leaves _upd_done.txt -> "UPDATED" card
    gameVersion = JS(Cfg()["game"], "version", "1.0.0");
    repoUrl = JS(Cfg()["game"], "repo_url", "");
    updater = std::make_unique<GameUpdater>();
    updater->SetCurrent(gameVersion);
    {
        std::string dv, dn;
        const char* uh = std::getenv("EI_UPDATE");
        if (GameUpdater::ConsumeAppliedMarker(dv, dn)) {
            updater->ForceState(GameUpdater::State::Applied, dv.c_str(), dn.c_str());
            updOpen = 1;
        } else if (uh && !std::strcmp(uh, "fake")) {
            updater->ForceState(GameUpdater::State::Available, "v9.9.9",
                "- fixed a visitor who arrived without a face\n"
                "- filing cabinet drawers now contain the correct year\n"
                "- hallway extended; the end no longer renders\n"
                "- plants stop counting as witnesses\n"
                "- clocks agreed to disagree\n"
                "- improved pause menu kerning (very important)\n"
                "- the basement level stays where it was left\n"
                "- audio: the hum is now 4% more ominous\n"
                "- door seals tightened after the incident\n"
                "- clipboard paper is now regulation weight\n");
            updOpen = 1;
        } else if (!uh || std::strcmp(uh, "off")) {
            // dev hooks: EI_VER/EI_SHA simulate an outdated installed build
            const char* ev = std::getenv("EI_VER");
            const char* es = std::getenv("EI_SHA");
            updater->Start(repoUrl, ev && *ev ? ev : gameVersion,
                           es ? es : EI_BUILD_SHA);
        }
    }
    // dev screenshots (EI_SHOT / F12 / EI_DUMPRT) collect in shots/ instead
    // of littering the exe folder - raylib's TakeScreenshot strips dirs, so
    // capture by hand (LoadImageFromScreen + ExportImage respects the path)
    std::filesystem::create_directories("shots");
    if (const char* s = std::getenv("EI_SHOT")) shotAt = (float)std::atof(s);
    if (const char* p = std::getenv("EI_PLAY")) {  // dev hook: skip title
        NewShift(false);
        if (std::strcmp(p, "sit") == 0) SitDown(); // sit at the computer
    }
    if (std::getenv("EI_DEBUG")) debug = true;
    if (const char* y = std::getenv("EI_YAW")) player.yaw = (float)std::atof(y);
    if (const char* pt = std::getenv("EI_PITCH")) player.pitch = (float)std::atof(pt);
    if (const char* cp = std::getenv("EI_CAM")) {   // dev: exact camera pose "x,y,z,yaw,pitch" (enables flycam)
        std::vector<float> v;
        std::string s = cp; size_t p = 0;
        while (p <= s.size()) {
            size_t q = s.find(',', p);
            v.push_back((float)atof(s.substr(p, q - p).c_str()));
            if (q == std::string::npos) break;
            p = q + 1;
        }
        if (v.size() >= 5) {
            debug = true; flycam = true;
            flyPos = {v[0], v[1], v[2]};
            player.yaw = v[3]; player.pitch = v[4];
        }
    }
    if (const char* pp = std::getenv("EI_PATH")) {
        // trailer/dev camera path: "t,x,y,z,yaw,pitch; t,x,y,z,yaw,pitch; ..."
        // flycam rides smoothstep-lerped keyframes on wall-clock time; the
        // run auto-quits 1s past the last key. HUD is suppressed while a
        // path is active - pair with EI_RECDUMP for clean footage.
        std::string s = pp; size_t p0 = 0;
        while (p0 <= s.size()) {
            size_t p1 = s.find(';', p0);
            std::string k = s.substr(p0, p1 == std::string::npos ? p1 : p1 - p0);
            p0 = (p1 == std::string::npos) ? s.size() + 1 : p1 + 1;
            float v[6] = {}; int n = 0;
            for (size_t a = 0, b; a <= k.size() && n < 6; a = b + 1) {
                b = k.find(',', a); if (b == std::string::npos) b = k.size();
                v[n++] = (float)atof(k.substr(a, b - a).c_str());
            }
            if (n >= 6) pathKeys.push_back({v[0], v[1], v[2], v[3], v[4], v[5]});
        }
        if (!pathKeys.empty()) {
            std::sort(pathKeys.begin(), pathKeys.end(),
                      [](const PathKey& a, const PathKey& b) { return a.t < b.t; });
            debug = true; flycam = true;
            flyPos = {pathKeys[0].x, pathKeys[0].y, pathKeys[0].z};
            player.yaw = pathKeys[0].yaw; player.pitch = pathKeys[0].pitch;
            pathT0 = (float)GetTime();
            pathEnd = pathKeys.back().t + 1.0f;
        }
    }
    if (const char* rd = std::getenv("EI_RECDUMP")) {
        // dev hook: dump every rendered frame as shots/rec/f_%05d.png at
        // the given fps - ffmpeg reassembles them into video offline.
        recDumpFps = (float)std::atof(rd);
        if (recDumpFps > 0.f) std::filesystem::create_directories("shots/rec");
    }
    // EI_CLEAN=1 or any scripted-path run: keep flycam/path mechanics but hide
    // all debug chrome - F3 overlay, osTex thumbnail, prompts, toasts, clock.
    // Gameplay captures (EI_SEQ player POV, no path) KEEP the HUD - prompts,
    // crosshair, clock and the hide note are part of the footage.
    if (!pathKeys.empty() || std::getenv("EI_CLEAN")) cleanDebug = true;
    if (const char* np = std::getenv("EI_NPC")) {
        // dev: posed humanoid dummies, no AI - for debug screenshots.
        // spec: "x,y,z[,yaw][,flag|key=value...] ; x,y,z[...] ..."
        //   4th bare number = yaw deg. Flags: seated walk detain female frozen
        //   Keys:   raise=0..1 anim=rad hair=0..6 h=scale seat=h eyes=kind
        //           skin/shirt/pants/hairc/eyecol=r.g.b (dots, not commas)
        auto parseCol = [](const std::string& s, Color& c) {
            float p0, p1, p2;
            if (sscanf(s.c_str(), "%f.%f.%f", &p0, &p1, &p2) != 3) return false;
            c = Color{(unsigned char)p0, (unsigned char)p1, (unsigned char)p2, 255};
            return true;
        };
        std::string spec = np;
        size_t n0 = 0; int idx = 0;
        while (n0 <= spec.size()) {
            size_t n1 = spec.find(';', n0);
            std::string one = spec.substr(n0, n1 == std::string::npos ? n1 : n1 - n0);
            n0 = (n1 == std::string::npos) ? spec.size() + 1 : n1 + 1;
            std::vector<std::string> tk;
            for (size_t p = 0, q; p <= one.size(); p = (q == std::string::npos ? one.size() + 1 : q + 1)) {
                q = one.find(',', p);
                std::string t = one.substr(p, q == std::string::npos ? q : q - p);
                while (!t.empty() && isspace((unsigned char)t.front())) t.erase(t.begin());
                while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
                if (!t.empty()) tk.push_back(t);
            }
            if (tk.size() < 3) continue;
            DbgNpc n;
            n.pos = {(float)atof(tk[0].c_str()), (float)atof(tk[1].c_str()), (float)atof(tk[2].c_str())};
            size_t fi = 3;
            if (tk.size() > 3 && tk[3].find('=') == std::string::npos &&
                (isdigit((unsigned char)tk[3][0]) || tk[3][0] == '-' || tk[3][0] == '+'))
                { n.yaw = (float)atof(tk[3].c_str()); fi = 4; }
            for (; fi < tk.size(); fi++) {
                const std::string& t = tk[fi];
                size_t eq = t.find('=');
                std::string k = ToLower(eq == std::string::npos ? t : t.substr(0, eq));
                std::string val = eq == std::string::npos ? "" : t.substr(eq + 1);
                if      (k == "seated")  n.seated = true;
                else if (k == "walk")    n.walk = 1.f;
                else if (k == "detain")  n.detain = 1.f;
                else if (k == "female")  n.female = true;
                else if (k == "male")    n.male = true;
                else if (k == "frozen")  n.frozen = true;
                else if (k == "look")    n.look = true;   // eyes track the camera
                else if (k == "yaw")     n.yaw = (float)atof(val.c_str());
                else if (k == "walkamt") n.walk = (float)atof(val.c_str());
                else if (k == "raise")   n.raise = (float)atof(val.c_str());
                else if (k == "detainamt") n.detain = (float)atof(val.c_str());
                else if (k == "anim")    n.anim = (float)atof(val.c_str());
                else if (k == "hair")    n.hair = atoi(val.c_str());
                else if (k == "h" || k == "height") n.height = (float)atof(val.c_str());
                else if (k == "seat")    n.seatH = (float)atof(val.c_str());
                else if (k == "eyes")    n.eyes = val;
                else if (k == "skin")    n.hasSkin = parseCol(val, n.skin);
                else if (k == "shirt")   n.hasShirt = parseCol(val, n.shirt);
                else if (k == "pants")   n.hasPants = parseCol(val, n.pants);
                else if (k == "hairc")   n.hasHairC = parseCol(val, n.hairC);
                else if (k == "eyecol")  n.hasEyeCol = parseCol(val, n.eyeCol);
                // accessory passthroughs -> appearance json (true when bare)
                else if (k == "glasses" || k == "beard" || k == "tie" || k == "badge")
                    n.acc[k] = val.empty() ? "1" : val;
            }
            // appearance: reuse the roster path - a fabricated persona json
            // gives the same seeded variety (hair/skin/eyes by identity)
            json pj;
            pj["id"] = "dbg_npc_" + std::to_string(idx);
            pj["name"] = pj["id"];
            auto& ap = pj["appearance"];
            ap["height"] = n.height;
            if (n.female) ap["gender"] = "female";
            if (n.male)   ap["gender"] = "male";   // seed can roll female otherwise
            if (n.hair >= 0) ap["hair_style"] = n.hair;
            if (!n.eyes.empty()) ap["eyes"] = n.eyes;
            if (n.hasSkin) ap["skin"] = {n.skin.r, n.skin.g, n.skin.b};
            if (n.hasShirt) ap["shirt"] = {n.shirt.r, n.shirt.g, n.shirt.b};
            if (n.hasPants) ap["pants"] = {n.pants.r, n.pants.g, n.pants.b};
            if (n.hasHairC) ap["hair"] = {n.hairC.r, n.hairC.g, n.hairC.b};
            if (n.hasEyeCol) ap["eye_color"] = {n.eyeCol.r, n.eyeCol.g, n.eyeCol.b};
            for (auto& [k, val] : n.acc) {
                Color c{};
                if ((k == "glasses" || k == "tie") && parseCol(val, c)) ap[k] = {c.r, c.g, c.b};
                else if (k == "beard" && !val.empty() && !isdigit((unsigned char)val[0])) ap[k] = val;
                else if (k == "beard") ap[k] = atoi(val.c_str());
                else ap[k] = atoi(val.c_str()) != 0;
            }
            n.app = AppFromJson(pj);
            dbgNpcs.push_back(n);
            idx++;
        }
    }
    if (const char* sq = std::getenv("EI_SEQ")) {
        // dev: scripted gameplay timeline "t,cmd; t,cmd; ..." driven off the
        // fixed-dt clock so EI_RECDUMP runs stay frame-exact. Commands:
        //   tp:x,z,yaw[,pitch]   teleport+look     look:yaw,pitch
        //   hold:W|A|S|D|SHIFT|Q,secs            interact  door  invite
        //   sit  stand  visitor  blinds  lights  say:<line>
        //   app:report|book|records|phone|paper|building|none
        //   anom:<id>  kind:<kind>  call  intruder  notify:<text>  quit
        //   god (deaths log, never fire)  calm/heat (scheduler park/resume)
        //   instab:<n>  hour:<n>  night:<n>  drop:<kind|id>  clear
        //   threat:door|window  vleave  vgo  freeze:<sec>  hide  unhide
        //   envs: EI_GOD EI_CALM EI_INSTAB EI_HOUR EI_DOOR EI_BLINDS EI_HIDE
        std::string spec = sq;
        for (size_t p = 0, q; p <= spec.size(); p = (q == std::string::npos ? spec.size() + 1 : q + 1)) {
            q = spec.find(';', p);
            std::string one = spec.substr(p, q == std::string::npos ? q : q - p);
            size_t c = one.find(',');
            if (c != std::string::npos)
                seq.push_back({(float)atof(one.c_str()), one.substr(c + 1)});
        }
        std::sort(seq.begin(), seq.end(), [](auto& a, auto& b) { return a.first < b.first; });
        g_simKeys = &simActive;
    }
    if (std::getenv("EI_HIDE")) {   // dev: spawn crouched under the desk
        if (const Furni* desk = office.Find("desk"))
            player.pos = {desk->pos.x, 0.f, desk->pos.z};
        player.eyeH = 0.55f;
    }
    if (std::getenv("EI_DOOR")) doorOpen = true;   // dev: start with door swung open
    if (std::getenv("EI_GOD")) godMode = true;     // dev: deaths log, never fire
    if (std::getenv("EI_CALM")) ad.devCalm = true; // dev: spawn scheduler parked
    if (const char* iv = std::getenv("EI_INSTAB")) // dev: preset instability
        ad.AddInstability((float)std::atof(iv));
    if (const char* fa = std::getenv("EI_ANOM"))  // dev hook: force-spawn an office anomaly
        ad.DebugForceOffice(fa, office);
    if (const char* fk = std::getenv("EI_KIND"))  // dev hook: force-spawn any def by kind
        ad.DebugForceKind(fk, office, &vs);
    if (const char* ap = std::getenv("EI_APP")) {  // dev: pin a monitor app open
        if (!std::strcmp(ap, "book"))      os.app = FakeOS::App::Appointments;
        else if (!std::strcmp(ap, "phone"))    os.app = FakeOS::App::Phone;
        else if (!std::strcmp(ap, "paper"))    os.app = FakeOS::App::Paperwork;
    }
    if (std::getenv("EI_CALL") && A.anomalies.contains("call_events"))  // dev hook: phone rings now
        ad.DebugCallEvent(A.anomalies["call_events"][0]);
    if (std::getenv("EI_WIN")) {   // dev: jump to the shift-end eval screen
        NewShift(false);
        gameHour = JF(Cfg()["game"], "shift_end_hour", 30.f) - 0.001f;
        deeds["interview_done"] = 5; deeds["anomaly_resolved"] = 7; deeds["anomaly_seen"] = 9;
        deeds["report_rejected"] = 2; deeds["call_answered"] = 3; deeds["call_ignored"] = 1;
        deeds["intruder_survived"] = 1; deeds["breach_survived"] = 1; paperIdx = 4;
    }
    if (const char* lt = std::getenv("EI_LLMTEST")) {   // live LLM connectivity check, logs verdict
        LLMClient::Request t;
        int tid;
        if (std::strcmp(lt, "dialog") == 0) {
            // seat a real roster visitor and run the actual dialog path
            vs.CurMut().person = vs.RosterFind("m_okafor");
            if (!vs.CurMut().person && !vs.Roster().empty()) vs.CurMut().person = &vs.Roster()[0];
            vs.CurMut().state = VState::Seated;
            SayToVisitor("What brings you in tonight?");
            tid = dlgReq;
            TraceLog(LOG_WARNING, "[llmtest] sys=%.220s", BuildVisitorSystem().c_str());
        } else if (std::strcmp(lt, "roster") == 0) {
            RequestRoster();
            tid = rosterReq;
        } else if (std::strcmp(lt, "report") == 0) {
            // submit but let PollLLM consume the verdict in the game loop -
            // that's what drives crew dispatch + status updates
            if (const char* rt = std::getenv("EI_REPORT")) SubmitReport(rt);
            else SubmitReport("the office lights went out");
            TraceLog(LOG_WARNING, "[llmtest] report submitted - watch [llm] verdict line");
            tid = -1;
        } else {
            t.purpose = "frontdesk";
            t.systemPrompt = JS(A.dialogs, "frontdesk_prompt");
            t.user = "ping - reply with one short line";
            tid = llm.Submit(t);
        }
        if (tid != -1) {
            LLMClient::Result tr;
            for (int i = 0; i < 480; i++) {   // ~12s worst case
                if (llm.Poll(tid, tr)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            TraceLog(LOG_WARNING, "[llmtest] backend=%s http=%d ok=%d err=%s text=%.140s",
                     tr.backend.c_str(), tr.httpStatus, (int)tr.ok,
                     tr.errDetail.c_str(), tr.text.c_str());
            if (std::strcmp(lt, "roster") == 0 && tr.ok) {
                bool okv = vs.ApplyRosterJson(tr.text);
                TraceLog(LOG_WARNING, "[llmtest] roster apply=%d names:", (int)okv);
                for (auto& p : vs.Roster())
                    TraceLog(LOG_WARNING, "  - %s | %s | %s", JS(p, "name").c_str(),
                             JS(p, "role").c_str(), JS(p, "always_anomaly", "-").c_str());
            }
        }
    }
    return true;
}


// Build the light list from furniture + optional config "extra_lights".
void Game::CollectLights() {
    fx.lights.clear();
    for (auto& f : office.items) {
        if (f.ft == FT::CeilingLight)
            fx.lights.push_back({{f.pos.x, f.pos.y - 0.4f, f.pos.z},
                                 {235, 228, 205, 255}, 9.5f, 1.6f, f.room, true});
        else if (f.ft == FT::Desk)
            // faint warm bounce trapped under the desktop - its downward-facing
            // underside gets zero light from the ceiling fixtures, so it reads
            // as invisible black while hiding under it
            fx.lights.push_back({{f.pos.x, f.size.y - 0.2f, f.pos.z},
                                 {130, 112, 90, 255}, 1.9f, 0.6f, "office", false});
        else if (f.ft == FT::DeskLamp) {
            float c = std::cos(f.rotY * DEG2RAD), s = std::sin(f.rotY * DEG2RAD);
            Vector3 off{0.1f * c + -0.06f * s, f.size.y - 0.06f, -0.1f * s + -0.06f * c};
            // separate circuit: never flickers or dies - the desk stays readable
            // even during a lights_office outage
            fx.lights.push_back({{f.pos.x + off.x, f.pos.y + off.y, f.pos.z + off.z},
                                 {255, 198, 130, 255}, 4.5f, 1.15f, "office", false});
        }
    }
    Vector3 mc = office.MonitorScreenCenter();
    fx.lights.push_back({{mc.x, mc.y + 0.05f, mc.z + 0.45f}, office.monitorGlow,
                         3.2f, 0.8f, "office", false});
    if (Cfg().contains("extra_lights"))
        for (auto& e : Cfg()["extra_lights"])
            fx.lights.push_back({JV3(e["pos"]), JC(e["color"], {200, 205, 190, 255}),
                                 JF(e, "radius", 5.f), JF(e, "intensity", 0.8f),
                                 JS(e, "room", "waiting"), JB(e, "flicker", false)});
}


void Game::Notify(const std::string& s) { notes.push_back({s, 7.f}); }


// fire-once tutorial toast - night 1 walks the new clerk through the job
void Game::Tut(const char* key, const std::string& msg) {
    if (night != 1 || !tutSeen.insert(key).second) return;
    notes.push_back({msg, 10.f});
}


void Game::NewShift(bool fromSave) {
    // the ORIENTATION: SKIP setting means "my record says 0 but start me at 1"
    // - bypassed only by an explicit save load, never quietly
    if (!fromSave && night == 0 && tutorialSkip) night = 1;
    if (!fromSave) {
        gameHour = JF(Cfg()["game"], "shift_start_hour", 18.f);
        if (const char* h = std::getenv("EI_HOUR"))   // dev: jump the shift clock
            gameHour = std::clamp((float)std::atof(h), 18.f, 30.f);
        vs.night = night;   // before ApplyRosterJson - slot casting gates on it
        office.Load(A.office, Cfg());
        office.blindsOpen = std::getenv("EI_BLINDS") != nullptr;   // dev: breach testing
        // an LLM roster waits between shifts - swap it in before Reset so
        // tonight's visitors, ambient NPCs and the book all use fresh faces
        if (!rosterRaw.empty()) {
            if (vs.ApplyRosterJson(rosterRaw)) {
                TraceLog(LOG_INFO, "[roster] LLM cast applied (%s)", llm.ProviderName().c_str());
                for (auto& p : vs.Roster()) {
                    seenNames.push_back(JS(p, "name"));
                    if (seenNames.size() > 80) seenNames.erase(seenNames.begin());
                }
                SaveSettings();
            } else {
                // keep a tail sample so the rejection is diagnosable (truncated
                // JSON, prose wrapper, empty content...)
                std::string tail = rosterRaw.size() > 120
                    ? rosterRaw.substr(rosterRaw.size() - 120) : rosterRaw;
                TraceLog(LOG_WARNING, "[roster] generation unusable - stock cast tonight (%u bytes, tail: %s)",
                         (unsigned)rosterRaw.size(), tail.c_str());
            }
            rosterRaw.clear();
        }
        // same deal for tonight's paperwork: LLM forms if the generation
        // landed, the stock set from dialogs.json otherwise
        paperForms = json::array();
        if (!paperRaw.empty()) {
            if (ApplyPaperworkJson(paperRaw))
                TraceLog(LOG_INFO, "[paperwork] LLM forms applied (%s)", llm.ProviderName().c_str());
            else TraceLog(LOG_WARNING, "[paperwork] generation unusable - stock forms tonight");
            paperRaw.clear();
        }
        if (paperForms.empty() && A.dialogs.contains("paperwork")) paperForms = A.dialogs["paperwork"];
        // difficulty sizes tonight's workload: early shifts get a short
        // visitor book and a thin forms stack, deep nights run everything
        {
            int slotCap = 999, formCap = 999;
            auto arrV = Cfg()["game"].value("visitor_slots_by_night", json::array());
            if (night >= 1 && night - 1 < (int)arrV.size() && arrV[night - 1].is_number())
                slotCap = arrV[night - 1].get<int>();
            auto arrF = Cfg()["game"].value("paper_forms_by_night", json::array());
            if (night >= 1 && night - 1 < (int)arrF.size() && arrF[night - 1].is_number())
                formCap = arrF[night - 1].get<int>();
            if (night == 0) { slotCap = 999; formCap = 0; }   // training: scripted book, no forms
            vs.SetSlotCap(slotCap);
            // anomalous bookings are capped per night too - the stock book
            // fronts 4 of 6 slots as anomalies on night 1, which is how new
            // players ate four releases before learning the escort call
            auto arrA = Cfg()["instability"]["night_scaling"].value("max_anomalous_slots", json::array());
            if (night >= 1 && night - 1 < (int)arrA.size() && arrA[night - 1].is_number())
                vs.SetAnomalyCap(arrA[night - 1].get<int>());
            else vs.SetAnomalyCap(999);
            if (paperForms.is_array() && (int)paperForms.size() > formCap)
                paperForms.erase(paperForms.begin() + formCap, paperForms.end());
        }
        deeds.clear();
        vs.night = night;   // Reset()->InjectLore() gates on tonight's number
        vs.Reset();
        ad.Reset();
        Vector3 sp = JV3(A.office["player_spawn"]["pos"]);
        player.pos = sp;
        player.yaw = JF(A.office["player_spawn"], "yaw", 180);
    }
    os.Reset(); dlg.Reset(); phoneDlg.Reset();
    phone = PhoneEvt{};
    notes.clear(); ending.clear(); deathTitle.clear();
    tutTitle.clear(); tutMsg.clear(); tutFocus.clear(); tutMsgT = 0;
    scareT = 0; sitT = 0; lean = unseatT = 0;
    shake = 0; blackoutT = 0; doorOpen = false; doorAnim = 0; intruderSeen = false; manualLightsOff = false;
    camRoll = 0; settleDip = 0; lastBobSin = 0; wasMoving = false; cam.up = {0.f, 1.f, 0.f};
    breachT = 0; breachWarned = false; breachRush = -1; breachSearchT = 0; breachCooldown = 0;
    breachDoor = false;
    heartT = 0; evalScore = -1;
    lastVState = VState::None; askedCur = false;
    office.moveGlides.clear();
    noteSfx = false; hallWhisperT = 0;
    workCallsUsed.clear();
    allDutiesToasted = false; visitorClosedDoor = false;
    autoEndT = -1.f; earlyEndT = -1.f; doorRattle = 0.f;
    loreText.clear(); loreCardT = 0;
    workCallT = JF(Cfg()["instability"], "work_call_first_sec", 45.f);
    if (night == 0) workCallT = 1e9f;   // training shift: no mundane phone noise
    paperIdx = 0;
    player.crouching = player.forcedCrouch = player.hiding = false;
    player.pitch = 0;
    cam.fovy = fovBase;
    dlgReq = repReq = facReq = deskReq = remixReq = phoneReq = phoneEvalReq = rosterReq = paperReq = -1;
    ad.freeform = !hardcodedOnly;
    ad.night = night;
    vs.night = night;
    tutSeen.clear();
    tutStep = (night == 0) ? 0 : -1;   // night 0 runs the scripted lesson plan
    mode = Mode::Roam;
    DisableCursor();
    Notify("Shift started. Survive until " + FmtClock(JF(Cfg()["game"], "shift_end_hour", 30)) + ".");
    if (night == 1)
        Notify("Orientation: someone will knock. Let them in, sit at your desk, run the interview, then dismiss them.");
}


// ---------- interactions ----------

// furniture id -> player-facing label ("wr_bench_1" -> "Bench")
static std::string PrettyName(std::string id) {
    for (const char* p : {"wr_", "anom_", "rx_"})
        if (id.rfind(p, 0) == 0) { id.erase(0, std::strlen(p)); break; }
    size_t u = id.rfind('_');
    if (u != std::string::npos && u + 1 < id.size() &&
        std::all_of(id.begin() + u + 1, id.end(), [](char ch) { return std::isdigit((unsigned char)ch); }))
        id.erase(u);
    for (auto& c : id) if (c == '_') c = ' ';
    if (!id.empty()) id[0] = (char)std::toupper((unsigned char)id[0]);
    return id;
}


// EI_SEQ action executor - every command drives the same functions real
// input does, so trailer captures show actual gameplay, not fakes
void Game::ExecSeq(const std::string& c) {
    size_t col = c.find(':');
    std::string op = ToLower(col == std::string::npos ? c : c.substr(0, col));
    std::string arg = col == std::string::npos ? "" : c.substr(col + 1);
    std::vector<float> num;
    for (size_t p = 0, q; p <= arg.size(); p = (q == std::string::npos ? arg.size() + 1 : q + 1)) {
        q = arg.find(',', p);
        num.push_back((float)atof(arg.substr(p, q == std::string::npos ? q : q - p).c_str()));
    }
    if (op == "tp") {          // tp:x,z[,yaw[,pitch]]
        if (num.size() >= 2) player.pos = {num[0], 0.f, num[1]};
        if (num.size() >= 3) player.yaw = num[2];
        if (num.size() >= 4) player.pitch = num[3];
    } else if (op == "look") { // look:yaw[,pitch]
        if (!num.empty()) player.yaw = num[0];
        if (num.size() >= 2) player.pitch = num[1];
    } else if (op == "hold") { // hold:W|A|S|D|SHIFT|Q,secs
        size_t kc = arg.find(',');
        std::string kn = ToLower(kc == std::string::npos ? arg : arg.substr(0, kc));
        float secs = kc == std::string::npos ? 1.f : (float)atof(arg.substr(kc + 1).c_str());
        int k = kn == "w" ? keys.fwd : kn == "s" ? keys.back :
                kn == "a" ? keys.left : kn == "d" ? keys.right :
                kn == "shift" ? keys.sprint : kn == "q" ? keys.crouch : 0;
        if (k && secs > 0.f) simHold.push_back({k, secs});
    } else if (op == "interact") {
        DoInteract();
    } else if (op == "door") {
        if (doorBlocked) { audio.Play(AudioBank::Sfx::Thump, 0.6f); Notify("Blocked - it won't move."); }
        else if (vs.VisitorAtDoor()) { doorOpen = false; vs.InviteIn(); audio.Play(AudioBank::Sfx::Whoosh, 0.7f); }
        else { doorOpen = !doorOpen; audio.Play(AudioBank::Sfx::Creak, 0.55f); }
    } else if (op == "invite") {
        seqInvite = true;   // fires the moment the visitor reaches the door
    } else if (op == "sit") {
        SitDown();
    } else if (op == "updload") {   // updater: kick the package download
        if (updater) updater->BeginDownload();
    } else if (op == "updapply") {  // updater: stage apply + quit (script relaunches)
        if (updater && updater->ApplyAndRelaunch()) quit = true;
    } else if (op == "sitv") {
        // sit and face the visitor - SitDown lands on the monitor when the
        // seat choice races their sit; flip back if they're already seated
        SitDown();
        if (mode == Mode::SitComputer && vs.VisitorSeated()) {
            mode = Mode::SitVisitor;
            dlg.focused = true;
        }
    } else if (op == "stand") {
        if (mode == Mode::SitVisitor || mode == Mode::SitComputer) StandUp();
    } else if (op == "swap") {   // TAB: monitor <-> visitor while seated
        if (mode == Mode::SitComputer || mode == Mode::SitVisitor) {
            mode = (mode == Mode::SitComputer) ? Mode::SitVisitor : Mode::SitComputer;
            dlg.focused = (mode == Mode::SitVisitor);
            if (mode == Mode::SitComputer) RefocusOsField();
        }
    } else if (op == "visitor") {
        vs.CallNextEarly(office);
    } else if (op == "promote") {   // scheduled spawn that MAY match a waiting ambient
        vs.DebugPromote(office);
    } else if (op == "blinds") {
        office.blindsOpen = !office.blindsOpen;
    } else if (op == "lights") {
        manualLightsOff = !manualLightsOff;
    } else if (op == "say") {
        if (!arg.empty()) SayToVisitor(arg);
    } else if (op == "end") {      // END INTERVIEW button equivalent
        if (vs.VisitorSeated()) vs.EndInterview();
    } else if (op == "app") {
        std::string a = ToLower(arg);
        os.app = a == "report" ? FakeOS::App::Report :
                 a == "book" || a == "appt" ? FakeOS::App::Appointments :
                 a == "records" ? FakeOS::App::Records :
                 a == "phone" ? FakeOS::App::Phone :
                 a == "paper" ? FakeOS::App::Paperwork : FakeOS::App::None;
    } else if (op == "report") {   // exercises the real REPORT pipeline end to end
        SubmitReport(arg.empty() ? "the mug moved" : arg);
    } else if (op == "fac") {      // PHONE > FACILITIES, verbatim description
        CallFacilities(arg.empty() ? "the lights" : arg);
    } else if (op == "sec") {      // PHONE > SECURITY > intruder|anomaly
        CallSecurity(arg.empty() ? "intruder" : arg);
    } else if (op == "anom") {
        ad.DebugForceOffice(arg.c_str(), office);
    } else if (op == "kind") {
        ad.DebugForceKind(arg.c_str(), office, &vs);
    } else if (op == "drop") {       // yank a live anomaly by kind (dev)
        ad.DropKind(arg.c_str());
        ad.ApplyExteriorFlags(office, vs);
    } else if (op == "call") {
        if (A.anomalies.contains("call_events"))
            ad.DebugCallEvent(A.anomalies["call_events"][0]);
    } else if (op == "intruder") {
        ad.DebugIntruder(office);
    } else if (op == "vanom") {      // corrupt whoever sits in the chair
        ad.DebugVanom(vs);
    } else if (op == "god") {        // deaths get logged, not fired
        godMode = true;
    } else if (op == "calm") {       // park the spawn scheduler
        ad.devCalm = true;
    } else if (op == "heat") {       // resume rolling (undo calm)
        ad.devCalm = false;
    } else if (op == "instab") {     // instab:<n> nudges the meter
        ad.AddInstability(arg.empty() ? 10.f : (float)atof(arg.c_str()));
    } else if (op == "hide") {       // duck under the desk (survives breach searches)
        if (const Furni* d = office.Find("desk")) player.pos = {d->pos.x, 0.f, d->pos.z};
        player.crouching = player.forcedCrouch = true;
    } else if (op == "unhide") {
        player.forcedCrouch = player.crouching = player.hiding = false;
    } else if (op == "crouch") {    // plain crouch, not under the desk
        player.crouching = !player.crouching;
        player.hiding = false;
    } else if (op == "hour") {       // hour:<n> jumps the shift clock (18..30)
        gameHour = std::clamp(arg.empty() ? 20.f : (float)atof(arg.c_str()), 18.f, 30.f);
    } else if (op == "night") {      // night:<n> restarts the shift at night n
        night = std::max(0, arg.empty() ? 1 : atoi(arg.c_str()));
        NewShift(false);
    } else if (op == "drop") {       // drop:<kind-or-id> clears live anomalies
        ad.DropKind(arg); ad.DropId(arg);
        ad.ApplyExteriorFlags(office, vs);
        if (arg == "intruder") ad.ClearIntruder(office);
    } else if (op == "clear") {      // drop every active anomaly, keep pending crew
        std::vector<std::string> ids;
        for (auto& a : ad.Active()) ids.push_back(a.id);
        for (auto& i : ids) ad.DropId(i);
        ad.ApplyExteriorFlags(office, vs);
        ad.ClearIntruder(office);
        TraceLog(LOG_INFO, "[dev] cleared %d live anomalies", (int)ids.size());
    } else if (op == "threat") {     // threat:door|window forces a banger there
        ad.DebugForceKind(arg == "window" ? "banging_window" : "banging_door", office, &vs);
    } else if (op == "vleave") {     // the seated visitor wraps up right now
        if (vs.VisitorSeated()) { vs.CurMut().seatT = vs.softLen + 1.f; }
    } else if (op == "vgo") {        // force the active visitor straight to Gone
        if (vs.HasActiveVisitor()) vs.CurMut().state = VState::Gone;
    } else if (op == "freeze") {     // freeze:<sec> holds instability (paperwork fx)
        ad.FreezeInstability(arg.empty() ? 30.f : (float)atof(arg.c_str()));
    } else if (op == "shot") {    // shot:<name> -> shots/<name>.png this frame
        shotReq = "shots/" + arg + ".png";
    } else if (op == "notify") {
        Notify(arg);
    } else if (op == "save") {       // write the shift save where it stands
        Save();
        TraceLog(LOG_INFO, "[dev] save written");
    } else if (op == "load") {       // rebuild from the save - exercises round-trip
        Load();
        TraceLog(LOG_INFO, "[dev] save reloaded");
    } else if (op == "quit") {
        quit = true;
    }
}


void Game::DoInteract() {
    if (!interactTarget) return;
    const std::string& it = interactTarget->interact;
    if (it == "sit") SitDown();
    else if (it == "sit_monitor") {
        // touching the monitor = intent to use the computer - go straight to it
        SitDown();
        mode = Mode::SitComputer;
        dlg.focused = false;
        RefocusOsField();
        SetMousePosition(GetScreenWidth() / 2, GetScreenHeight() / 2);
    }
    else if (it == "switch") {
        manualLightsOff = !manualLightsOff;
        deeds["lights_used"]++;
        audio.Play(AudioBank::Sfx::Click, 0.8f);
        if (ad.HasKind("lights_office"))
            Notify("The switch clicks uselessly. The lights stay dead.");
        else
            Notify(manualLightsOff ? "You flick the office lights off." : "You flick the office lights on.");
    }
    else if (it == "blinds") {
        office.blindsOpen = !office.blindsOpen;
        deeds["blinds_used"]++;
        audio.Play(AudioBank::Sfx::Click, 0.7f);
        Notify(office.blindsOpen ? "Blinds open." : "Blinds closed.");
    } else if (it == "phone") {
        if (phone.ringing) {
            if (interactTarget->hidden) Notify("Your hand finds the handset in the dark.");
            AnswerPhone();
        }
        else { audio.Play(AudioBank::Sfx::Click, 0.4f); Notify("Just a dial tone."); }
    } else if (it == "note") {
        // already reading it -> E folds the slip away again
        if (loreCardT > 0.f) {
            loreCardT = 0.f;
            audio.Play(AudioBank::Sfx::TypeTick, 0.4f);
            return;
        }
        // a slid-in / misplaced note is readable - lore slips delivered by
        // whoever keeps writing them. Unseen slips surface first so the set
        // completes across replays; re-reads show the text but don't re-score
        auto& pool = A.dialogs["lore_notes"];
        if (pool.is_array() && !pool.empty()) {
            std::vector<int> fresh;
            for (int i = 0; i < (int)pool.size(); i++)
                if (!loreSeen.count("note_" + std::to_string(i))) fresh.push_back(i);
            int idx = fresh.empty() ? RandInt(0, (int)pool.size() - 1)
                                    : fresh[RandInt(0, (int)fresh.size() - 1)];
            std::string lid = "note_" + std::to_string(idx);
            loreText = pool[idx].get<std::string>();
            loreCardT = 16.f;
            if (loreSeen.insert(lid).second) {
                deeds["lore_found"]++;
                SaveSettings();   // the collection persists immediately
            }
        } else {
            loreText = "the building keeps what it takes.";
            loreCardT = 16.f;
        }
        audio.Play(AudioBank::Sfx::TypeTick, 0.5f);
    } else if (it == "door") {
        if (doorBlocked) {
            audio.Play(AudioBank::Sfx::Thump, 0.6f);
            Notify("You shove the door. It barely gives - something heavy is holding it shut.");
        } else if (office.doorBroken) {
            audio.Play(AudioBank::Sfx::Creak, 0.5f);
            Notify("The door hangs off its hinges. It won't close until it's repaired.");
        } else if (vs.VisitorAtDoor()) {
            doorOpen = false;                     // open anim is driven by the walk-through
            vs.InviteIn();
            audio.Play(AudioBank::Sfx::Whoosh, 0.7f);
            Notify(vs.Cur().Name() + " is coming in. Get seated.");
        } else {
            doorOpen = !doorOpen;                 // peek into the waiting room
            audio.Play(AudioBank::Sfx::Creak, 0.55f);
            Notify(doorOpen ? "You ease the door open a crack." : "You pull the door shut.");
        }
    }
}


// re-focus whichever fake-OS text field is on screen when sitting back down
// or TAB-ing to the monitor - no extra click needed to keep typing
void Game::RefocusOsField() {
    if (os.app == FakeOS::App::Report) os.reqFocusReport = true;
    else if (os.app == FakeOS::App::Phone && !os.phoneActive.empty() && os.phoneActive != "security")
        os.reqFocusCall = true;
}


void Game::SitDown() {
    if (phone.answered) HangUpPhone();   // can't juggle the handset and the desk
    mode = vs.VisitorSeated() ? Mode::SitVisitor : Mode::SitComputer;
    // plant the player at the chair so the camera animates into it and they
    // stand back up in the right spot
    player.pos = {office.seatCam.x, 0.f, office.seatCam.z};
    player.yaw = 180.f;
    player.pitch = 0.f;
    sitT = 0;
    lean = 0;
    camRoll = 0; cam.up = {0.f, 1.f, 0.f};   // seated views stay level
    sitFromPos = cam.position; sitFromTgt = cam.target;
    fovFrom = cam.fovy;
    EnableCursor();
    // the E press that sat us down already queued an 'e' char - flush it now
    // so no freshly-focused field types it (the focusGrace windows below are
    // the second line of defense)
    while (GetCharPressed() > 0) {}
    dlg.focused = (mode == Mode::SitVisitor);
    if (mode == Mode::SitComputer) {
        RefocusOsField();
        // hardware cursor starts where the crosshair was - the in-world OS
        // dot is the cursor while seated
        SetMousePosition(GetScreenWidth() / 2, GetScreenHeight() / 2);
    }
}


void Game::StandUp() {
    mode = Mode::Unseating;
    unseatT = 0;
    // step out behind the spot we actually sat in - NOT the chair's live furni
    // position, which an anomaly may have carried across the room while we hid
    // under the desk (that read as a teleport)
    if (const Furni* desk = office.Find("desk")) {
        Vector2 d{office.seatCam.x - desk->pos.x, office.seatCam.z - desk->pos.z};
        float l = std::sqrt(d.x * d.x + d.y * d.y);
        if (l < 0.01f) { d = {0.f, 1.f}; l = 1; }
        player.pos = {office.seatCam.x + d.x / l * 0.95f, 0.f,
                      office.seatCam.z + d.y / l * 0.95f};
    }
    unseatFromPos = cam.position; unseatFromTgt = cam.target;
    EnableCursor();
}


// ---------- update ----------

void Game::UpdateRoam(float dt) {
    auto solids = office.Solids();
    // the swung-open panel is real geometry once the door moves off its frame
    if (doorAnim > 0.05f) {
        if (const Furni* d = office.Find("door")) {
            float a = d->rotY * DEG2RAD;
            Vector2 h = {d->pos.x - d->size.x / 2, d->pos.z};           // hinge
            Vector2 e = {h.x + std::cos(a) * d->size.x,
                         h.y - std::sin(a) * d->size.x};                // free edge
            solids.push_back({{std::min(h.x, e.x) - 0.1f, 0.f, std::min(h.y, e.y) - 0.1f},
                              {std::max(h.x, e.x) + 0.1f, 2.4f, std::max(h.y, e.y) + 0.1f}});
        }
    }
    // crouch-under-desk: drop the desk's collision box while low so the player
    // can slip beneath it, and stay crouched until fully clear of it
    bool underDesk = false;
    if (const Furni* desk = office.Find("desk")) {
        AABB db{{desk->pos.x - desk->size.x / 2 - 0.08f, 0.f, desk->pos.z - desk->size.z / 2 - 0.08f},
                {desk->pos.x + desk->size.x / 2 + 0.08f, 0.8f, desk->pos.z + desk->size.z / 2 + 0.08f}};
        underDesk = player.pos.x > db.mn.x && player.pos.x < db.mx.x &&
                    player.pos.z > db.mn.z && player.pos.z < db.mx.z;
        if (player.crouching || underDesk) {
            AABB ds = BottomBox(desk->pos, desk->size);
            std::erase_if(solids, [&](const AABB& s) {
                return std::fabs(s.mn.x - ds.mn.x) < 0.01f && std::fabs(s.mn.z - ds.mn.z) < 0.01f;
            });
            // the desk stops being a slab but its WALLS stay real: side legs
            // and the modesty panel (visitor side) still block a low player,
            // leaving the chair-side edge as the only way in/out
            float a = desk->rotY * DEG2RAD, ca = std::cos(a), sa = std::sin(a);
            auto part = [&](float lx, float lz, float w, float d, float h) {
                float ox = lx * ca + lz * sa, oz = -lx * sa + lz * ca;
                float hx = (std::fabs(w * ca) + std::fabs(d * sa)) / 2;
                float hz = (std::fabs(w * sa) + std::fabs(d * ca)) / 2;
                solids.push_back({{desk->pos.x + ox - hx, 0.f, desk->pos.z + oz - hz},
                                  {desk->pos.x + ox + hx, h, desk->pos.z + oz + hz}});
            };
            part(-desk->size.x / 2 + 0.08f, 0.f, 0.16f, desk->size.z, desk->size.y);          // legs (full depth)
            part(desk->size.x / 2 - 0.08f, 0.f, 0.16f, desk->size.z, desk->size.y);
            part(0.f, -desk->size.z / 2 + 0.04f, desk->size.x - 0.32f, 0.09f,
                 desk->size.y - 0.1f);                                                        // back panel, flush to legs
            part(desk->size.x / 2 - 0.41f, 0.10f, 0.5f, desk->size.z - 0.44f,
                 desk->size.y - 0.1f);                                                        // drawer pedestal
        }
    }
    player.forcedCrouch = underDesk;

    if (flycam) {
        if (!pathKeys.empty()) {
            // scripted camera path (EI_PATH): Catmull-Rom through keyframes -
            // C1-continuous velocity, no per-segment stall like smoothstep.
            // Input ignored - this is a playback head, not a free camera.
            // Under EI_RECDUMP the head runs on the fixed-step clock so path
            // timing stays frame-exact regardless of dump speed.
            float t = (recDumpFps > 0.f) ? recTime : (float)GetTime() - pathT0;
            size_t i = 0, n = pathKeys.size();
            while (i + 1 < n && t >= pathKeys[i + 1].t) i++;
            const PathKey& P0 = pathKeys[i ? i - 1 : 0];
            const PathKey& P1 = pathKeys[i];
            const PathKey& P2 = pathKeys[std::min(i + 1, n - 1)];
            const PathKey& P3 = pathKeys[std::min(i + 2, n - 1)];
            float u = (P2.t > P1.t) ? std::clamp((t - P1.t) / (P2.t - P1.t), 0.f, 1.f) : 1.f;
            auto cr = [](float p0, float p1, float p2, float p3, float uu) {
                float u2 = uu * uu, u3 = u2 * uu;
                return 0.5f * (2.f * p1 + (-p0 + p2) * uu +
                               (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * u2 +
                               (-p0 + 3.f * p1 - 3.f * p2 + p3) * u3);
            };
            // yaw keys get shortest-arc unwrapped off P1 before splining so a
            // 350->10 pan goes +20 through 0, not -340 the long way
            auto wrap = [](float d) { return std::fmod(d + 540.f, 360.f) - 180.f; };
            float y0 = P1.yaw + wrap(P0.yaw - P1.yaw);
            float y2 = P1.yaw + wrap(P2.yaw - P1.yaw);
            float y3 = y2 + wrap(P3.yaw - P2.yaw);
            flyPos = {cr(P0.x, P1.x, P2.x, P3.x, u),
                      cr(P0.y, P1.y, P2.y, P3.y, u),
                      cr(P0.z, P1.z, P2.z, P3.z, u)};
            player.yaw = std::fmod(cr(y0, P1.yaw, y2, y3, u) + 360.f, 360.f);
            player.pitch = cr(P0.pitch, P1.pitch, P2.pitch, P3.pitch, u);
            Vector3 f = player.Forward();
            cam.position = flyPos;
            cam.target = {flyPos.x + f.x, flyPos.y + f.y, flyPos.z + f.z};
            cam.up = {0.f, 1.f, 0.f};
            camRoll = 0;
            cam.fovy = fovBase;
            player.moving = false; player.strafe = 0; player.moveAmt = 0;
            interactTarget = nullptr;
            interactLabel.clear();
            if (pathEnd > 0.f && t >= pathEnd) quit = true;
            return;
        }
        // F4 debug fly camera: the player body stays put and untouched while
        // the lens roams - WASD fly, SPACE/CTRL vertical, F6 drops the player
        // at the camera (ground level) for collision/nav testing
        Vector2 md = GetMouseDelta();
        player.yaw -= md.x * sens;
        player.pitch = std::clamp(player.pitch - md.y * sens, -89.f, 89.f);
        Vector3 f = player.Forward();
        float yr = player.yaw * DEG2RAD;
        Vector3 rt{-std::cos(yr), 0.f, std::sin(yr)};
        float spd = IsKeyDown(keys.sprint) ? 9.f : 3.2f;
        Vector3 mv{0, 0, 0};
        if (IsKeyDown(keys.fwd))   { mv.x += f.x;  mv.y += f.y;  mv.z += f.z; }
        if (IsKeyDown(keys.back))  { mv.x -= f.x;  mv.y -= f.y;  mv.z -= f.z; }
        if (IsKeyDown(keys.right)) { mv.x += rt.x; mv.z += rt.z; }
        if (IsKeyDown(keys.left))  { mv.x -= rt.x; mv.z -= rt.z; }
        if (IsKeyDown(KEY_SPACE))        mv.y += 1.f;
        if (IsKeyDown(KEY_LEFT_CONTROL)) mv.y -= 1.f;
        flyPos.x += mv.x * spd * dt;
        flyPos.y += mv.y * spd * dt;
        flyPos.z += mv.z * spd * dt;
        player.moving = false;
        player.strafe = 0; player.moveAmt = 0;
        cam.position = flyPos;
        cam.target = {flyPos.x + f.x, flyPos.y + f.y, flyPos.z + f.z};
        cam.up = {0.f, 1.f, 0.f}; camRoll = 0;
        cam.fovy += (fovBase - cam.fovy) * std::min(1.f, dt * 6.f);
        interactTarget = nullptr;
        interactLabel.clear();
        if (IsKeyPressed(KEY_F6)) {
            player.pos = {flyPos.x, 0.f, flyPos.z};
            Notify("[flycam] player teleported to lens position");
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            pauseReturn = mode; pauseReason.clear(); menuPage = 0;
            mode = Mode::Pause; EnableCursor();
        }
        return;
    }

    // look + move pause while the phone-call panel is up so the cursor can
    // type/click - you're holding a handset, not pacing the room
    player.Update(dt, solids, office.bounds, !phone.answered, !phone.answered, sens,
                  JF(Cfg()["game"], "sprint_mul", 1.6f),
                  JF(Cfg()["game"], "crouch_height", 0.55f), keys);
    player.hiding = underDesk && player.crouching;   // danger events miss you here
    // FP-body walk phase rides bobT exactly - the footstep sfx fires on
    // sin(bobT) zero-crossings, so legs land ON their steps (and bobT's
    // cadence already encodes walk/sprint/crouch speed). While idle the
    // phase freewheels slowly so the chest keeps breathing
    if (player.moving) fpStepT = player.bobT;
    else fpStepT += dt * 0.55f;

    // find interactable
    interactTarget = nullptr;
    interactLabel.clear();
    Vector3 eye = player.Eye(), fwd = player.Forward();
    float bestT = JF(Cfg()["game"], "interact_distance", 2.6f);
    for (auto& f : office.items) {
        if (f.interact.empty()) continue;
        // notes are readable wherever they slide in (waiting room included) -
        // other interactions stay office-side
        if (f.room != "office" && f.interact != "note") continue;
        // a ringing handset can still be found in the dark even if the
        // phone prop "vanished" - otherwise an unanswered call is unfair
        if (f.hidden && !(phone.ringing && f.interact == "phone")) continue;
        if (f.interact == "sit_monitor") {      // chair-side + close only
            if (player.pos.z <= f.pos.z + 0.32f ||
                std::fabs(player.pos.x - f.pos.x) > 1.35f ||
                player.pos.z - f.pos.z > 2.3f) continue;
        }
        float t;
        if (f.interact == "door") {
            // ray vs the swung panel itself: hinge edge pinned at local -x,
            // panel sweeps with rotY - aim at the door where it IS, not
            // where it hangs when closed
            float a = f.rotY * DEG2RAD;
            Vector3 c{f.pos.x - f.size.x / 2 + std::cos(a) * f.size.x / 2,
                      f.pos.y + f.size.y / 2,
                      f.pos.z - std::sin(a) * f.size.x / 2};
            t = RayVsOBB_Y(eye, fwd, c,
                           {f.size.x / 2 + 0.1f, f.size.y / 2, f.size.z / 2 + 0.12f}, a);
        } else if (f.interact == "blinds") {
            // slats move with anomaly mutations; once raised they stack at
            // the top rail, which becomes the (thinner) grab zone
            float a = f.rotY * DEG2RAD;
            float hh = office.blindsOpen ? 0.28f : f.size.y / 2;
            float cy = office.blindsOpen ? f.pos.y + f.size.y - 0.2f
                                       : f.pos.y + f.size.y / 2;
            t = RayVsOBB_Y(eye, fwd, {f.pos.x, cy, f.pos.z},
                           {f.size.x / 2 + 0.1f, hh, f.size.z / 2 + 0.14f}, a);
        } else {
            t = RayVsAABB(eye, fwd, BottomBox(f.pos, f.size + Vector3{0.16f, 0.16f, 0.16f}));
        }
        if (t >= 0 && t < bestT) { bestT = t; interactTarget = &f; }
    }
    // look-at-name ray: same geometry rules so a swung door still names "Door"
    auto lookT = [&](const Furni& f, float pad) {
        if (f.ft == FT::Door) {
            float a = f.rotY * DEG2RAD;
            Vector3 c{f.pos.x - f.size.x / 2 + std::cos(a) * f.size.x / 2,
                      f.pos.y + f.size.y / 2,
                      f.pos.z - std::sin(a) * f.size.x / 2};
            return RayVsOBB_Y(eye, fwd, c, {f.size.x / 2 + pad, f.size.y / 2, f.size.z / 2 + pad}, a);
        }
        if (f.ft == FT::Blinds) {
            float a = f.rotY * DEG2RAD;
            float hh = office.blindsOpen ? 0.28f : f.size.y / 2;
            float cy = office.blindsOpen ? f.pos.y + f.size.y - 0.2f
                                       : f.pos.y + f.size.y / 2;
            return RayVsOBB_Y(eye, fwd, {f.pos.x, cy, f.pos.z},
                              {f.size.x / 2 + pad, hh, f.size.z / 2 + pad}, a);
        }
        return RayVsAABB(eye, fwd, BottomBox(f.pos, f.size + Vector3{pad, pad, pad}));
    };
    std::string ek = "[" + std::string(KeyName(keys.interact)) + "]";
    if (interactTarget) {
        if (interactTarget->interact == "sit") interactLabel = "Sit at your desk  " + ek;
        else if (interactTarget->interact == "sit_monitor") interactLabel = "Sit at the computer  " + ek;
        else if (interactTarget->interact == "switch") interactLabel =
            manualLightsOff ? "Turn the lights on  " + ek : "Turn the lights off  " + ek;
        else if (interactTarget->interact == "blinds") interactLabel =
            office.blindsOpen ? "Close blinds  " + ek : "Open blinds  " + ek;
        else if (interactTarget->interact == "phone") interactLabel =
            phone.ringing ? "Answer the phone  " + ek : "Desk phone";
        else if (interactTarget->interact == "door") interactLabel =
            vs.VisitorAtDoor() ? "Invite them in  " + ek
                               : (doorOpen ? "Close the door  " + ek : "Open the door  " + ek);
        else if (interactTarget->interact == "note") interactLabel = "Read the note  " + ek;
    }
    // name whatever furniture the player is looking at - knowing the exact
    // object name is what makes writing a valid report possible
    const Furni* lookF = interactTarget;
    float lt = 7.5f, lt2 = interactTarget ? bestT : 7.5f;
    if (!interactTarget) {
        for (auto& f : office.items) {
            if (f.hidden || (f.room != "office" && f.room != "waiting")) continue;
            float t = lookT(f, 0.12f);
            if (t >= 0 && t < lt2) { lt2 = t; lookF = &f; }
        }
    }
    (void)lt;
    // while hiding, the only thing to name is the desk wall in your face -
    // skip the chip so it doesn't fight the taped-note card
    if (interactLabel.empty() && lookF && !player.hiding) interactLabel = PrettyName(lookF->id);
    // on a live phone call, E must type into the call transcript - not also
    // re-trigger the phone interact under the crosshair (silent-phone ding)
    if (IsKeyPressed(keys.interact) && !phone.answered) DoInteract();
    // ESC hangs up first - pause only when not holding the handset
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (phone.answered) HangUpPhone();
        else { pauseReturn = mode; pauseReason.clear(); menuPage = 0; mode = Mode::Pause; EnableCursor(); }
    }

    // ---- presence: footsteps, strafe lean, breath, settle, sprint fov ----
    // footstep lands each time the head-bob crosses zero - cadence is free
    float sb = std::sin(player.bobT);
    if (player.moving && lastBobSin * sb < 0.f) {
        stepSide = -stepSide;
        audio.PlayPanned(AudioBank::Sfx::Step,
                         player.sprinting ? 0.55f : player.crouching ? 0.18f : 0.34f,
                         0.5f + stepSide * 0.18f);
    }
    lastBobSin = player.moving ? sb : 0.f;
    // a small settle dip when a walk/sprint stops
    if (wasMoving && !player.moving) settleDip = 1.f;
    wasMoving = player.moving;
    settleDip = std::max(0.f, settleDip - dt * 3.2f);
    float dip = -std::sin(settleDip * 3.14159f) * 0.045f * settleDip;

    // strafe lean: camera rolls a couple of degrees into lateral movement
    float rollTgt = -player.strafe * (player.sprinting ? 3.0f : 2.2f);
    camRoll += (rollTgt - camRoll) * std::min(1.f, dt * 8.f);
    cam.up = {std::sin(camRoll * DEG2RAD), std::cos(camRoll * DEG2RAD), 0.f};
    // sprint widens fov a touch for speed
    cam.fovy += ((player.sprinting ? fovBase + 4.5f : fovBase) - cam.fovy) *
                std::min(1.f, dt * 4.f);

    // idle breathing is stronger than walk sway; lateral hip sway while moving
    float breath = std::sin((float)GetTime() * 1.35f) * (0.004f + 0.005f * (1.f - player.moveAmt));
    float swayX = std::cos(player.bobT * 0.5f) * 0.02f * player.moveAmt;
    Vector3 e = player.Eye();
    Vector3 f = player.Forward();
    float yr = player.yaw * DEG2RAD;
    Vector3 rt{-std::cos(yr), 0, std::sin(yr)};
    float bobY = player.BobOffset() + breath + dip;
    cam.position = {e.x + rt.x * swayX, e.y + bobY, e.z + rt.z * swayX};
    cam.target = {e.x + f.x + rt.x * swayX, e.y + bobY + f.y, e.z + f.z + rt.z * swayX};
}


void Game::UpdateSeated(float dt) {
    // smooth sit-down blend
    sitT = std::min(1.f, sitT + dt * 1.7f);
    float k = sitT * sitT * (3 - 2 * sitT);

    // lean: hold A = peek left toward the visitor, hold D = peek right toward window.
    // Suppressed while a text field has focus so A/D type instead of lean.
    bool typing = (mode == Mode::SitComputer) ? os.typing : dlg.focused;
    float lt = typing ? 0.f : (IsKeyDown(keys.left) ? -1.f : 0.f) + (IsKeyDown(keys.right) ? 1.f : 0.f);
    lean += (lt - lean) * std::min(1.f, dt * 7.f);
    float la = std::fabs(lean);

    Vector3 scrC = office.MonitorScreenCenter();
    Vector3 seatPos = office.seatCam;
    Vector3 baseTgt = office.lookVisitor;
    float fovTgt = fovBase;
    if (mode == Mode::SitComputer) {
        // lean in close to the monitor so the OS is readable
        seatPos = {scrC.x, scrC.y + 0.06f, scrC.z + 0.64f};
        baseTgt = scrC;
        fovTgt = 46.f;
    }
    Vector3 leanTgt = (lean < 0) ? office.lookVisitor : office.WindowCenter();
    Vector3 pos{seatPos.x + lean * 0.45f, seatPos.y + la * 0.05f, seatPos.z - la * 0.08f};
    Vector3 tgt{baseTgt.x + (leanTgt.x - baseTgt.x) * la,
                baseTgt.y + (leanTgt.y - baseTgt.y) * la,
                baseTgt.z + (leanTgt.z - baseTgt.z) * la};
    if (k < 1.f) {
        cam.position.x = sitFromPos.x + (pos.x - sitFromPos.x) * k;
        cam.position.y = sitFromPos.y + (pos.y - sitFromPos.y) * k;
        cam.position.z = sitFromPos.z + (pos.z - sitFromPos.z) * k;
        cam.target.x = sitFromTgt.x + (tgt.x - sitFromTgt.x) * k;
        cam.target.y = sitFromTgt.y + (tgt.y - sitFromTgt.y) * k;
        cam.target.z = sitFromTgt.z + (tgt.z - sitFromTgt.z) * k;
    } else {
        // seated: damp toward goal so TAB/lean transitions stay smooth
        float s = std::min(1.f, dt * 9.f);
        cam.position.x += (pos.x - cam.position.x) * s;
        cam.position.y += (pos.y - cam.position.y) * s;
        cam.position.z += (pos.z - cam.position.z) * s;
        cam.target.x += (tgt.x - cam.target.x) * s;
        cam.target.y += (tgt.y - cam.target.y) * s;
        cam.target.z += (tgt.z - cam.target.z) * s;
    }
    float fovGoal = fovTgt + la * (fovBase - fovTgt);
    cam.fovy += (fovGoal - cam.fovy) * std::min(1.f, dt * 6.f);

    if (IsKeyPressed(keys.swap)) {
        mode = (mode == Mode::SitComputer) ? Mode::SitVisitor : Mode::SitComputer;
        audio.Play(AudioBank::Sfx::Click, 0.4f);
        dlg.focused = (mode == Mode::SitVisitor);
        if (mode == Mode::SitComputer) RefocusOsField();
    }
    // ESC stands up from the chair; pause is one more press away on your feet
    if (IsKeyPressed(KEY_ESCAPE)) { StandUp(); return; }

    os.Update(dt);
}


void Game::UpdateUnseating(float dt) {
    unseatT = std::min(1.f, unseatT + dt * 2.4f);
    float k = unseatT * unseatT * (3 - 2 * unseatT);
    Vector3 eye = player.Eye();
    Vector3 f = player.Forward();
    Vector3 roamTgt{eye.x + f.x, eye.y + f.y, eye.z + f.z};
    cam.position.x = unseatFromPos.x + (eye.x - unseatFromPos.x) * k;
    cam.position.y = unseatFromPos.y + (eye.y - unseatFromPos.y) * k;
    cam.position.z = unseatFromPos.z + (eye.z - unseatFromPos.z) * k;
    cam.target.x = unseatFromTgt.x + (roamTgt.x - unseatFromTgt.x) * k;
    cam.target.y = unseatFromTgt.y + (roamTgt.y - unseatFromTgt.y) * k;
    cam.target.z = unseatFromTgt.z + (roamTgt.z - unseatFromTgt.z) * k;
    cam.fovy += (fovBase - cam.fovy) * std::min(1.f, dt * 6.f);
    if (unseatT >= 1.f) { mode = Mode::Roam; DisableCursor(); }
    // mid-stand ESC still pauses; resume drops back into the same blend
    if (IsKeyPressed(KEY_ESCAPE)) {
        pauseReturn = mode; pauseReason.clear(); menuPage = 0;
        mode = Mode::Pause; EnableCursor();
    }
}


void Game::UpdateCommon(float dt) {
    dbgT += dt;
    // EI_SEQ scripted timeline + held simulated keys
    while (seqIdx < seq.size() && seq[seqIdx].first <= dbgT)
        ExecSeq(seq[seqIdx++].second);
    if (seqInvite && vs.VisitorAtDoor()) {
        seqInvite = false;
        doorOpen = false;
        vs.InviteIn();
        audio.Play(AudioBank::Sfx::Whoosh, 0.7f);
        Notify(vs.Cur().Name() + " is coming in. Get seated.");
    }
    if (!simHold.empty()) {
        simActive.clear();
        for (auto& h : simHold) { h.second -= dt; if (h.second > 0.f) simActive.insert(h.first); }
        std::erase_if(simHold, [](const auto& h) { return h.second <= 0.f; });
    }
    gameHour += dt / JF(Cfg()["game"], "real_seconds_per_game_hour", 200.f);
    if (gameHour >= JF(Cfg()["game"], "shift_end_hour", 30.f)) {
        WinShift(false);
        return;
    }
    // the whole book is done - nobody left to interview, nobody mid-visit.
    // Clock out early after a short settle beat instead of watching the
    // clock run to 6 AM. Never fires mid-breach or mid-scare.
    if (earlyEndT >= 0.f) {
        earlyEndT -= dt;
        if (earlyEndT <= 0.f) { earlyEndT = -1.f; WinShift(true); return; }
    } else if (breachRush < 0.f && breachSearchT <= 0.f && !phone.ringing &&
               !phone.answered && mode != Mode::Jumpscare && AllVisitorsDone()) {
        earlyEndT = JF(Cfg()["game"], "early_end_delay_sec", 5.f);
        Notify("Every name on the book is accounted for. The building lets you go.");
        audio.Play(AudioBank::Sfx::Whoosh, 0.4f);
    }
    // bookshelf_door wedges the doorway shut - nobody opens it, nobody passes
    doorBlocked = ad.IdLive("bookshelf_door");
    vs.doorBlocked = doorBlocked;
    vs.doorOpen = doorOpen;   // semantic door truth - visitors watch it every
                              // frame alongside the live panel angle
    vs.Update(dt, gameHour, office,
              [this](const std::string& s) { Notify(s); },
              [this] { audio.Play(AudioBank::Sfx::Knock, 0.9f); shake = std::max(shake, 0.2f); },
              [this](Vector3 from) {
                  // the vault lands on whoever's in the chair. Caught across
                  // the room it just proves it was never a person - loudly
                  float dx = player.pos.x - office.seatCam.x, dz = player.pos.z - office.seatCam.z;
                  bool inRange = (mode == Mode::SitVisitor || mode == Mode::SitComputer ||
                                  mode == Mode::Unseating) || (dx * dx + dz * dz < 4.8f);
                  if (inRange) {
                      deathTitle = "IT CAME OVER THE DESK";
                      Jumpscare("lunge");
                  } else {
                      deeds["anomaly_released"]++;
                      ad.AddInstability(JF(Cfg()["instability"], "release_instab", 14.f));
                      Notify("It vaulted the desk at an empty chair and kept going. It was never a person.");
                      audio.Play(AudioBank::Sfx::Stinger, 0.7f);
                  }
                  (void)from;
              });

    // a visitor who walks out un-interviewed is a write-up on your record.
    // anomalous visitors are exempt - Security on them WAS the correct answer
    {
        VState vst = vs.Cur().state;
        if (vst == VState::ToDoor && lastVState != VState::ToDoor) askedCur = false;
        // a lore persona in the chair is met the moment they sit - collecting
        // them feeds the prefer-unseen pick so the cast reveals itself
        if (vst == VState::Seated && lastVState == VState::ToChair && vs.Cur().person) {
            std::string pid = JS(*vs.Cur().person, "id");
            if (pid.rfind("lore_", 0) == 0 && loreSeen.insert(pid).second) {
                deeds["lore_found"]++;
                SaveSettings();
            }
        }
        if (lastVState == VState::Seated && (vst == VState::Leaving || vst == VState::Gone) &&
            vs.Cur().anomaly.empty() && !vs.Cur().escorted) {
            // did that interview actually conclude? An empty room says yes to
            // the miss instantly; anything with real back-and-forth goes to
            // the eval - natural goodbyes and typed dismissals count as a
            // proper ending, not a write-up
            int playerLines = 0;
            for (auto& t : vs.transcript) if (t.first == "You") playerLines++;
            if (playerLines == 0) {
                deeds["visitor_missed"]++;
                TraceLog(LOG_INFO, "[interview] %s left with zero interviewer lines -> visitor_missed",
                         vs.Cur().Name().c_str());
                Notify("WARNING ISSUED - " + vs.Cur().Name() +
                       " left without a proper interview. It goes on your record.");
                notes.back().second = 10.f;
                audio.Play(AudioBank::Sfx::Buzz, 0.55f);
            } else if (vs.Cur().autoWrap) {
                // the appointment simply ran out while you were talking -
                // that IS a concluded interview, not a walkout; sending it
                // to the eval would get "left mid-conversation" back and
                // wrongly charge a miss
                deeds["interview_done"]++;
                TraceLog(LOG_INFO, "[interview] %s appointment elapsed with %d player lines -> interview_done",
                         vs.Cur().Name().c_str(), playerLines);
                os.SetStatus("Interview logged - " + vs.Cur().Name());
            } else {
                std::string convo;
                for (auto& [s, tx] : vs.transcript)
                    convo += (s == "You" ? "Interviewer" : s) + ": " + tx + "\n";
                LLMClient::Request r;
                r.purpose = "intervieweval";
                r.systemPrompt =
                    "You are judging whether an exit interview was properly concluded. "
                    "Reply ONLY with JSON {\"match\":true/false,\"feedback\":\"one short sentence\"}. "
                    "match=true means the interviewer engaged the visitor AND the conversation "
                    "reached a clear end - a farewell, a dismissal, a wrap-up, or the visitor's "
                    "own goodbye. match=false means the visitor left mid-conversation, was "
                    "dismissed without acknowledgment, or was never really interviewed.";
                r.user = convo;
                ivEvals.push_back({llm.Submit(r), vs.Cur().Name()});
            }
        }
        // ...but an anomalous one who talked their way through the interview
        // and walked out under their own power is worse. It only registers
        // once they're actually gone - out of the room, out of the building
        if (lastVState == VState::Leaving && vst == VState::Gone &&
            !vs.Cur().anomaly.empty() && !vs.Cur().escorted) {
            deeds["anomaly_released"]++;
            // early nights hit softer - the mechanic has to be learned, and a
            // first-shift player can't know the escort call exists yet
            float relMul = 1.f;
            auto arr = Cfg()["instability"]["night_scaling"].value("release_instab_mul", json::array());
            if (night >= 1 && night - 1 < (int)arr.size() && arr[night - 1].is_number())
                relMul = arr[night - 1].get<float>();
            else if (night == 0) relMul = 0.3f;   // training wheels
            ad.AddInstability(JF(Cfg()["instability"], "release_instab", 14.f) * relMul);
            TraceLog(LOG_INFO, "[release] %s walked out anomalous -> instab +%.1f (mul %.2f)",
                     vs.Cur().Name().c_str(), JF(Cfg()["instability"], "release_instab", 14.f) * relMul, relMul);
            std::string msg = "LOGGING ERROR - " + vs.Cur().Name() +
                              " was never cleared to leave. Your signature let it out.";
            if (deeds["anomaly_released"] == 1)   // the first one teaches the fix
                msg += " Next time: PHONE > SECURITY > INTRUDER while they're seated.";
            Notify(msg);
            notes.back().second = 12.f;
            audio.Play(AudioBank::Sfx::Stinger, 0.55f);
        }
        lastVState = vst;
    }

    // a typed dismissal wraps the interview up on its own - "that's all",
    // "you can go", etc. The visitor answers, then rises and leaves
    if (autoEndT > 0.f) {
        autoEndT -= dt;
        if (autoEndT <= 0.f) {
            autoEndT = -1.f;
            if (vs.VisitorSeated()) {
                vs.EndInterview();
                Notify(vs.Cur().Name() + " takes the hint and wraps up.");
            }
        }
    }

    // a visitor who lets themselves in through an open door still pulls it
    // shut behind them once they're through the doorway
    if (!visitorClosedDoor && doorOpen && vs.HasActiveVisitor() &&
        vs.Cur().state == VState::ToChair && vs.Cur().pos.z > -4.6f) {
        visitorClosedDoor = true;
        doorOpen = false;
        Notify(vs.Cur().Name() + " pulls the door shut behind them.");
        audio.Play(AudioBank::Sfx::Creak, 0.45f);
    }
    if (vs.Cur().state == VState::ToDoor || vs.Cur().state == VState::AtDoor)
        visitorClosedDoor = false;

    // finishing the whole book AND the whole forms queue pays off
    if (!allDutiesToasted && AllDutiesDone()) {
        allDutiesToasted = true;
        deeds["clean_shift"]++;
        ad.AddInstability(-6.f);
        Notify("All duties complete - the office settles. Good clerk.");
        audio.Play(AudioBank::Sfx::Whoosh, 0.45f);
    }

    int anomsBefore = ad.ActiveCount();
    office.lightsManuallyOff = manualLightsOff;   // spawn guard reads this
    ad.Update(dt, gameHour, office, vs,
              [this](const std::string& s) { Notify(s); },
              [this] { OnIntruderStrike(); });
    // keep the visitor channel synced with who's actually in the chair BEFORE
    // counting - visitor anomalies land via EnsureVisitor, not ad.Update
    if (vs.VisitorSeated() && !vs.Cur().anomaly.empty())
        ad.EnsureVisitor(vs.Cur().anomaly);
    if (!vs.HasActiveVisitor() || vs.Cur().state == VState::Leaving || vs.Cur().state == VState::Gone)
        ad.ClearChannel("visitor");

    if (ad.ActiveCount() > anomsBefore) {
        deeds["anomaly_seen"] += ad.ActiveCount() - anomsBefore;
        // "something changed" cue - a creak, a dip in the lights, a felt note.
        // Steer by channel so a waiting-room spawn doesn't send the player
        // hunting the office (phone calls announce themselves by ringing).
        audio.Play(AudioBank::Sfx::Creak, 0.6f);
        blackoutT = std::max(blackoutT, 0.07f);
        const std::string& ch = ad.LastSpawnChannel();
        if (ch == "exterior")      Notify("Something feels different past the door.");
        else if (ch == "visitor")  Notify("Something about them is different.");
        else if (ch == "intruder") Notify("Something got inside.");
        else if (ch != "call")     Notify("Something about the room is different.");
        Tut("spawn", "Something changed. Office oddities go in the REPORT app - people and things outside go by PHONE.");
    }
    // night-1 tutorial triggers: teach the job as things happen
    if (night == 1) {
        if (phone.ringing) Tut("ring", "The desk phone is ringing - walk to it and press [" + std::string(KeyName(keys.interact)) + "].");
        if (vs.VisitorAtDoor()) Tut("door", "Someone's at the door - press [" + std::string(KeyName(keys.interact)) + "] to let them in, then sit across from them.");
        if (vs.VisitorSeated()) Tut("sit", "Sit at your desk [" + std::string(KeyName(keys.interact)) + "] to interview. [" + KeyName(keys.swap) + "] swaps between monitor and visitor.");
        if (ad.Instability() > 30.f) Tut("paper", "Terminal > PAPERWORK: filing a form holds the building still for a while.");
    }
    if (tutStep >= 0) UpdateTutorial(dt);
    UpdateCrew(dt);
    // anomaly move glides: the object scrapes into its new spot over ~0.6s
    // (each fresh one lands with a screech + a flinch, once)
    office.Update(dt);
    for (auto& g : office.moveGlides) {
        if (g.announced) continue;
        g.announced = true;
        audio.Play(AudioBank::Sfx::Screech, 0.85f);
        shake = std::max(shake, 0.10f);
    }
    // the hall door gives way for bodies passing through it (crew, visitors,
    // ambient leavers) instead of everyone clipping a shut panel
    {
        bool bodyNear = false;   // `near` is a windows.h macro via httplib
        auto closeToHall = [](Vector3 p) {
            // covers the doorway and the corridor behind it (incl. the turn)
            return p.x > 4.2f && p.x < 9.6f && p.z < -9.2f && p.z > -13.4f;
        };
        for (auto& e : crew) for (auto& m : e.men) if (closeToHall(m.pos)) bodyNear = true;
        if (vs.Cur().Active() && closeToHall(vs.Cur().pos)) bodyNear = true;
        for (auto& a : vs.ambient) if (closeToHall(a.pos)) bodyNear = true;
        if (Furni* hd = office.Find("hall_door"))
            hd->rotY += ((bodyNear ? -100.f : 0.f) - hd->rotY) * std::min(1.f, dt * 5.f);
    }

    UpdateAnomalyKinds(dt);

    // what things with eyes / mutated NPCs should track
    office.eyeTarget = player.Eye();
    vs.playerPos = player.Eye();   // heads/gaze target: eye height, not feet

    // anomaly banging: each swing lands with a real knock + a flinch; hits
    // on the DOOR shudder the panel itself (a transient jitter on rotY that
    // the swing code then overwrites back - reads as the panel taking blows)
    if (vs.BangerStrike()) {
        audio.Play(AudioBank::Sfx::Knock, vs.bangerAt == "door" ? 0.95f : 0.75f);
        shake = std::max(shake, 0.14f);
        if (vs.bangerAt == "door") doorRattle = 1.f;
    }
    doorRattle = std::max(0.f, doorRattle - dt * 6.f);

    ad.freeform = !hardcodedOnly;

    // a call-event anomaly rolled: desk phone starts ringing
    if (!ad.pendingCall.is_null() && !phone.ringing && !phone.answered) {
        phone.ev = ad.TakeCallEvent();
        phone.isWork = false;
        phone.anomalyId = JS(phone.ev, "id");
        phone.ringing = true; phone.ignoreT = 0; phone.ringT = 0;
        phone.transcript.clear(); phone.exchanges = 0;
        Notify("The desk phone is ringing.");
    }
    UpdatePhone(dt);

    // mundane work calls - payroll, IT, security check-ins. No anomaly weight,
    // just a phone that rings; ignoring them only costs a little calm.
    workCallT -= dt;
    if (workCallT <= 0 && !phone.ringing && !phone.answered && ad.pendingCall.is_null() &&
        A.dialogs.contains("work_calls")) {
        auto& wc = A.dialogs["work_calls"];
        auto iv = Cfg()["instability"]["work_call_interval_sec"];
        workCallT = RandRange(iv[0].get<float>(), iv[1].get<float>());
        // each caller phones once a night - payroll never rings twice
        std::vector<int> fresh;
        for (int i = 0; i < (int)wc.size(); i++)
            if (!workCallsUsed.count(i)) fresh.push_back(i);
        if (!fresh.empty()) {
            int pick = fresh[RandInt(0, (int)fresh.size() - 1)];
            workCallsUsed.insert(pick);
            phone.ev = wc[pick];
            phone.isWork = true;
            phone.anomalyId.clear();
            phone.ringing = true; phone.ignoreT = 0; phone.ringT = 0;
            phone.transcript.clear(); phone.exchanges = 0;
            Notify("The desk phone is ringing.");
        }
    }

    UpdateThreats(dt);

    // LLM remix spawn: director asked for one, submit the catalog prompt
    if (ad.remixPending && remixReq == -1) {
        LLMClient::Request r;
        r.purpose = "remix";
        r.systemPrompt = ad.RemixPrompt();
        r.user = "Design one anomaly now. JSON only.";
        remixReq = llm.Submit(r);
    }

    if (ad.IntruderActive() && !intruderSeen) {
        intruderSeen = true;
        audio.Play(AudioBank::Sfx::Thump, 0.35f); // faint cue only
    }
    if (!ad.IntruderActive()) intruderSeen = false;

    // hostile visitor reveal on long interviews even without player input
    if (vs.VisitorSeated() && vs.Cur().anomaly == "hostile" && vs.Cur().seatT > vs.hostileRevealSec)
        vs.MarkHostileRevealed();
    if (vs.Cur().hostileStarted && !vs.Cur().hostileWarned) {
        vs.CurMut().hostileWarned = true;
        Notify("Something about them has changed. PHONE > Security - now.");
        audio.Play(AudioBank::Sfx::Heart, 0.85f);
        shake = std::max(shake, 0.35f);
    }

    float ins = ad.Instability();
    // dread drone scales with instability; hum stays constant
    audio.SetDroneLevel(0.15f + ins / 100.f * 0.85f);
    // heartbeat joins in past 72% - pace + volume tighten toward the ceiling
    if (ins > 72.f) {
        heartT -= dt;
        if (heartT <= 0.f) {
            float k = (ins - 72.f) / 28.f;
            audio.Play(AudioBank::Sfx::Heart, 0.30f + 0.45f * k);
            heartT = 2.1f - 1.3f * k;
        }
    } else heartT = 0;

    // the building settles around you - sparse distant one-shots at low
    // volume. Seasoning, never signal: nothing to report here.
    ambT -= dt;
    if (ambT <= 0.f) {
        ambT = RandRange(24.f, 70.f);
        float v = RandRange(0.08f, 0.22f);
        switch (RandInt(0, 3)) {
            case 0: audio.Play(AudioBank::Sfx::Creak, v); break;         // joists flexing
            case 1: audio.Play(AudioBank::Sfx::Thump, v * 0.85f); break; // pipes, far off
            case 2: audio.Play(AudioBank::Sfx::Buzz, v * 0.7f); break;   // mains hum
            default: audio.Play(AudioBank::Sfx::Step, v); break;         // someone, somewhere
        }
    }

    // dust motes over the desk: slow drift + gentle sin sway, wrapped in a
    // box so the count never decays. Seeded lazily on first update.
    if (motes.empty()) {
        for (int i = 0; i < 42; i++)
            motes.push_back({RandRange(-1.4f, 1.4f), RandRange(0.75f, 2.1f),
                             RandRange(0.6f, 3.4f)});
    }
    for (auto& m : motes) {
        float ph = m.x * 3.1f + m.z * 1.7f;
        m.y += dt * (0.022f + 0.016f * std::sin((float)GetTime() * 0.4f + ph));
        m.x += dt * 0.014f * std::sin((float)GetTime() * 0.31f + ph * 2.f);
        if (m.y > 2.15f) m.y = 0.75f;                                   // rise & wrap
        if (m.x < -1.5f) m.x = 1.5f; else if (m.x > 1.5f) m.x = -1.5f;
    }

    // light flicker: ambient instability only shimmers - it must never kill
    // visibility or impersonate a reportable anomaly. Sustained darkness is
    // reserved for the lights_office ANOMALY / manual switch, and even then
    // the room stays dim-but-navigable (desk lamp + monitor carry it).
    bool lightsOut = ad.HasKind("lights_office") || manualLightsOff;
    float fl = 1.f;
    if (ins > 45.f)
        fl -= (ins - 45.f) / 55.f * 0.09f * (0.5f + 0.5f * std::sin((float)GetTime() * 11.f));
    if (lightsOut) {
        float t = (float)GetTime();
        // dead fixtures, not dim ones: the only ceiling glow left is a dying
        // sputter. The desk lamp, monitor and under-desk bounce (non-flicker)
        // carry what little visibility remains.
        fl = 0.02f + 0.05f * (0.5f + 0.5f * std::sin(t * 17.f) * std::sin(t * 6.3f));
        if (Rand01() < dt * 0.3f) audio.Play(AudioBank::Sfx::Buzz, 0.35f);
    } else if (ins > 60.f && blackoutT <= 0.f &&
               Rand01() < dt * 0.03f * ((ins - 60.f) / 40.f)) {
        blackoutT = RandRange(0.04f, 0.09f);              // brief brownout, not darkness
        audio.Play(AudioBank::Sfx::Buzz, 0.45f);
    }
    if (blackoutT > 0.f) blackoutT -= dt;
    fx.flicker = fl * (blackoutT > 0.f ? 0.55f : 1.f);
    fx.blackout = lightsOut ? 0.22f : (blackoutT > 0.f ? 0.5f : 0.f);
    fx.ambientMul = lightsOut ? 0.22f : 1.f;   // kill the glow floor with the lights
    office.lampDim = fx.flicker;
    fx.waitingDark = office.waitingLightsOut;

    if (DevEnv("EI_DIAG")) {   // per-second lighting state dump
        static float acc = 0; acc += dt;
        if (acc >= 1.f) {
            acc = 0;
            TraceLog(LOG_WARNING, "[diag] t=%.0f fl=%.2f flick=%.2f ins=%.1f lightsOut=%d man=%d bo=%.2f waitDark=%d anom=%d amb=%d vis=%d door=%d",
                     (float)GetTime(), fl, fx.flicker, ins, (int)lightsOut, (int)manualLightsOff,
                     blackoutT, (int)office.waitingLightsOut, ad.ActiveCount(), (int)vs.ambient.size(),
                     (int)vs.HasActiveVisitor(), (int)doorOpen);
        }
    }

    shake = std::max(0.f, shake - dt * 1.6f);

    if (ad.Instability() >= 100.f) {
        if (godMode) ad.AddInstability(99.f - ad.Instability());   // clamp under lethal
        else Die("TERMINATED", JS(A.dialogs["endings"], "consumed"));
    }

    PollLLM();
}


// ---------- night 0: scripted orientation ----------
// Two appointments and two anomalies, fired by hand - every step gates on
// what the PLAYER did, so a slow reader can't lose the thread and a fast
// one can't break it. Scripted fires reuse the real anomaly defs, so the
// channels it teaches are the real ones (REPORT app, PHONE > FACILITIES,
// PHONE > SECURITY). The dice-driven scheduler is parked for the night.
void Game::UpdateAnomalyKinds(float dt) {
    // ---- animated anomaly kinds - flag-driven, run every frame ----
    // a folded note slid under the door keeps creeping inward until it
    // settles just inside the doorway (crew carry it out on resolve)
    if (ad.HasKind("note_slid"))
        if (Furni* n = office.Find("anom_note")) {
            const float zt = -4.28f;
            if (n->pos.z < zt) {
                n->pos.z += (zt - n->pos.z) * std::min(1.f, dt * 0.55f);
                if (n->pos.z > -4.5f && !noteSfx) {
                    noteSfx = true;
                    audio.Play(AudioBank::Sfx::TypeTick, 0.5f);   // paper whisper
                }
            }
        }
    // the visitor chair slowly turns; a crew that's reached it holds it still
    if (ad.HasKind("spin_chair")) {
        bool held = false;
        for (auto& e : crew) if (e.phase >= 1)
            for (auto& o : e.outcomes) if (o.kind == "spin_chair") held = true;
        if (!held) if (Furni* c = office.Find("visitor_chair")) c->rotY += dt * 22.f;
    }
    // desk drawer open flag -> office.cpp slides a drawer out by drawerT;
    // while the assigned crew works it they're physically pushing it back
    office.drawerOpen = ad.HasKind("desk_drawer");
    bool drawerWorked = false;
    for (auto& e : crew) if (e.phase == 1)
        for (auto& o : e.outcomes) if (o.kind == "desk_drawer") drawerWorked = true;
    float drawerGoal = (office.drawerOpen && !drawerWorked) ? 1.f : 0.f;
    office.drawerT += (drawerGoal - office.drawerT) *
                      std::min(1.f, dt * (drawerWorked ? 1.1f : 4.f));
    // scratching and whispering inside the waiting room walls
    if (ad.HasKind("hall_writing")) {
        hallWhisperT -= dt;
        if (hallWhisperT <= 0.f) {
            hallWhisperT = RandRange(2.0f, 5.0f);
            audio.Play(RandRange(0.f, 1.f) < 0.6f ? AudioBank::Sfx::TypeTick
                                                  : AudioBank::Sfx::Creak,
                       RandRange(0.15f, 0.3f));
        }
    }
}


void Game::UpdateThreats(float dt) {
    // ----- breach: something outside finds a way in -----
    // window: blinds open (or glass already broken) while a figure/banger is
    //   at the window.
    // door: the office door standing open with a figure/banger at it.
    // crouched under the desk it can't find you; anywhere else is a kill.
    const json nsc = Cfg()["instability"].value("night_scaling", json::object());
    float breachUnlock = JF(nsc, "unlock_breach_night", 3.f);
    bool windowThreat = night >= breachUnlock && (office.blindsOpen || office.windowBroken) &&
        (vs.exteriorFigure == "figure_at_window" || (vs.bangerActive && vs.bangerAt == "window"));
    // a door held open BY a work crew isn't the player's negligence - it
    // won't push past uniforms. doorAnim mirrors doorOpen for crew too, so
    // the whole door clause must yield while crewOpenedDoor is set.
    bool doorAjar = (doorOpen || doorAnim > 0.45f || office.doorBroken) && !crewOpenedDoor;
    bool doorThreat = night >= breachUnlock && doorAjar &&
        (vs.exteriorFigure == "figure_at_door" || (vs.bangerActive && vs.bangerAt == "door"));
    // a door banger kept waiting behind a SHUT door doesn't give up - it
    // pounds the panel off its hinges, like the window glass. After that the
    // doorway is permanently open until Facilities repairs it.
    if (vs.bangerActive && vs.bangerAt == "door" && !doorAjar && !office.doorBroken) {
        bangDoorT += dt;
        if (bangDoorT > JF(Cfg()["instability"], "door_break_sec", 26.f)) {
            bangDoorT = 0;
            ad.DebugForceKind("broken_door", office, &vs);   // sets office.doorBroken
            doorRattle = 1.f;
            shake = std::max(shake, 0.5f);
            Notify("A final slam - the office door hangs wrecked on its hinges.");
            audio.Play(AudioBank::Sfx::Screech, 0.9f);
            audio.Play(AudioBank::Sfx::Thump, 0.9f);
            TraceLog(LOG_INFO, "[breach] door banger broke the door down");
        }
    } else bangDoorT = std::max(0.f, bangDoorT - dt * 2.f);
    // a broken line of defence means almost no grace: shattered glass or an
    // open door lets it come straight through instead of stalking the gap
    float need = JF(Cfg()["instability"], "breach_sec", 8.f);
    if (windowThreat && office.windowBroken) need *= 0.35f;
    if (doorThreat) need *= 0.30f;
    if (breachCooldown > 0) breachCooldown -= dt;
    if (breachRush >= 0) {
        // the rush crosses the room in well under a second - hide now or die
        breachRush += dt * 1.6f;
        shake = std::max(shake, 0.4f);
        if (breachRush >= 1.f) {
            breachRush = -1;
            if (player.hiding) {
                breachSearchT = JF(Cfg()["instability"], "breach_search_sec", 6.f);
                audio.Play(AudioBank::Sfx::Heart, 0.9f);
                TraceLog(LOG_INFO, "[breach] entered via %s - searching, player hidden",
                         breachDoor ? "door" : "window");
            } else if (VisitorShielded()) {
                // it took the interviewee and left fed - the line held for you
                breachCooldown = JF(Cfg()["instability"], "breach_cooldown_sec", 18.f);
                deeds["breach_survived"]++;
            } else {
                deathTitle = breachDoor ? "IT CAME THROUGH THE DOOR"
                                        : "IT CAME THROUGH THE GLASS";
                Jumpscare(breachDoor ? "breach_door" : "breach");
            }
        }
    } else if (breachSearchT > 0) {
        breachSearchT -= dt;
        if (!player.hiding) {           // stood up / left cover mid-search
            breachSearchT = 0;
            if (VisitorShielded()) {    // it grabbed the visitor instead
                breachCooldown = JF(Cfg()["instability"], "breach_cooldown_sec", 18.f);
                deeds["breach_survived"]++;
            } else {
                deathTitle = "IT FOUND YOU";
                Jumpscare("breach");
            }
        } else if (breachSearchT <= 0) {
            breachCooldown = JF(Cfg()["instability"], "breach_cooldown_sec", 18.f);
            deeds["breach_survived"]++;
            Notify(breachDoor ? "It searched the room and left. Check the door."
                              : "It searched the room and left. The window is broken - call Facilities.");
            audio.Play(AudioBank::Sfx::Creak, 0.6f);
            TraceLog(LOG_INFO, "[breach] search ended - survived via %s",
                     breachDoor ? "door" : "window");
        }
    } else if ((windowThreat || doorThreat) && breachCooldown <= 0) {
        breachT += dt;
        if (!breachWarned && breachT > std::min(2.5f, need * 0.45f)) {
            breachWarned = true;
            Notify(doorThreat ? "It has noticed the open door."
                              : "Something outside is watching the open blinds.");
            audio.Play(AudioBank::Sfx::Thump, 0.4f);
        }
        if (breachT > need) {
            breachT = 0; breachWarned = false;
            breachRush = 0;
            breachDoor = doorThreat;
            bool fromBanger = vs.bangerActive &&
                (vs.bangerAt == "window" || vs.bangerAt == "door");
            breachFrom = fromBanger ? vs.bangerPos : vs.figurePos;
            // it left its spot - remove the anomaly so the outside figure is
            // gone too (and doesn't linger there after a successful dodge).
            // The door figure also drives the banger state, so drop both
            // kinds - whichever is active is the thing that just came in.
            if (doorThreat) { ad.DropKind("banging_door"); ad.DropKind("figure_at_door"); }
            else            { ad.DropKind("banging_window"); ad.DropKind("figure_at_window"); }
            if (!doorThreat)
                // ...and it left the glass in pieces: stays live until
                // Facilities is called out to repair it
                ad.DebugForceKind("broken_window", office, &vs);
            audio.Play(AudioBank::Sfx::Stinger, 0.7f);
        }
    } else {
        breachT = std::max(0.f, breachT - dt * 2.f);
        breachWarned = false;
    }
}


void Game::UpdateTutorial(float dt) {
    // copy lives in dialogs.json "tutorial" for tuning; literals keep a
    // stripped assets dir playable
    auto line = [&](const char* k, const char* fb) -> std::string {
        std::string s = A.dialogs.contains("tutorial") ? JS(A.dialogs["tutorial"], k) : "";
        return s.empty() ? std::string(fb) : s;
    };
    // lesson cards persist on-screen until the step advances - no expiry mid-
    // lesson. The last card expires so orientation doesn't hang a sign forever.
    auto say = [&](const char* t, const std::string& m, const char* focus,
                   float secs = 0.f) {
        tutTitle = t; tutMsg = m; tutFocus = focus;
        tutMsgT = secs;                      // 0 = lives until the step moves
        audio.Play(AudioBank::Sfx::TypeTick, 0.35f);
    };
    if (tutMsgT > 0.f) { tutMsgT -= dt; if (tutMsgT <= 0.f) tutMsg.clear(); }
    switch (tutStep) {
        case 0:   // cold open - the book is the job
            say("WELCOME TO MERIDIAN",
                line("welcome", "Two appointments are on the book tonight. When someone knocks, let them in."),
                "the appointment book");
            tutStep = 1;
            break;
        case 1:   // first appointment reaches the door
            if (vs.VisitorAtDoor() || vs.VisitorSeated() ||
                vs.Cur().state == VState::ToChair) {
                say("VISITOR AT THE DOOR",
                    line("door", "Someone's at the door. Open it and they'll see themselves in."),
                    "the office door");
                tutStep = 2;
            }
            break;
        case 2:   // seated - run the interview
            if (vs.VisitorSeated()) {
                say("RUN THE INTERVIEW",
                    line("sit", "Sit at your desk and ask them something. The RECORD app on the terminal has their file."),
                    "your desk");
                tutStep = 3;
            }
            break;
        case 3:   // engaged - now end it properly
            if (askedCur) {
                say("WRAP IT UP",
                    line("wrap", "That's an interview. When you're satisfied, END INTERVIEW - or just tell them they can go."),
                    "the visitor");
                tutStep = 4;
            } else if (!vs.HasActiveVisitor()) {
                tutStep = 4;   // they left anyway - the warning teaches the rest
            }
            break;
        case 4:   // alone again - the room misbehaves
            if (!vs.HasActiveVisitor()) {
                ad.DebugForceOffice("mug_moved", office);
                say("SOMETHING MOVED",
                    line("report", "While you weren't watching, something moved on your desk. File a REPORT at the terminal - describe what changed."),
                    "the REPORT app");
                tutStep = 5;
            }
            break;
        case 5:   // an accepted report sends maintenance in - the id check
                  // keeps a mid-lesson save/load from deadlocking the script
            if (deeds["report_filed"] > 0 || !ad.IdLive("mug_moved")) {
                ad.DebugForceKind("lights_out", office, &vs);
                say("BUILDING PROBLEM",
                    line("facilities", "Confirmed - crew on the way. Now the waiting room lights just died. Look through the door, then PHONE > FACILITIES."),
                    "the PHONE app");
                tutStep = 6;
            }
            break;
        case 6:   // fixed - the next booking is wrong on purpose
            if (!ad.HasKind("lights_out")) {
                vs.CallNextEarly(office);
                say("CHECK THE STORY",
                    line("security", "Handled. One more appointment is coming - check their file against what they tell you. If it doesn't match: PHONE > SECURITY > INTRUDER."),
                    "PERSONNEL record");
                tutStep = 7;
            }
            break;
        case 7:   // the wrong appointment takes a seat
            if (vs.VisitorSeated()) {
                say("THEY DON'T MATCH",
                    line("record", "Sit. Interview them like anyone else - but read their file in RECORD. If the story and the paperwork disagree, they are not your appointment."),
                    "the visitor");
                tutStep = 8;
            }
            break;
        case 8:   // however it ended, the book decides when the night does
            if (!vs.HasActiveVisitor()) {
                say("THAT'S THE JOB",
                    line("done", "That's the whole job: clear the book, keep the room honest, last to 6 AM. An empty book clocks you out early."),
                    "", 18.f);
                tutStep = 9;
            }
            break;
        default: break;   // lesson plan over - the clock runs like any night
    }
}


// ---------- draw ----------

// Projects the monitor face to screen space and draws the live OS texture.
// Called in the 2D pass after the scene blit (unlit - reads as emissive).
// OS screen as real world geometry: drawn inside the 3D pass after LitOff so
// it renders unlit (a monitor glows). Depth-tested and clipped like any other
// surface - it can never warp, smear, or swing with the camera.
void Game::DrawMonitorPoly() {
    Vector3 c; float w, h, z;
    if (!osTexOk || !office.MonitorQuad(c, w, h, z)) return;
    rlBegin(RL_QUADS);
        rlSetTexture(osTex.texture.id);
        rlColor4ub(255, 255, 255, 255);
        rlNormal3f(0, 0, 1);   // faces +z toward the chair
        rlTexCoord2f(0, 1); rlVertex3f(c.x - w, c.y + h, z);  // TL (FBO v-flip)
        rlTexCoord2f(0, 0); rlVertex3f(c.x - w, c.y - h, z);  // BL
        rlTexCoord2f(1, 0); rlVertex3f(c.x + w, c.y - h, z);  // BR
        rlTexCoord2f(1, 1); rlVertex3f(c.x + w, c.y + h, z);  // TR
    rlEnd();
    rlSetTexture(0);  // no-op on GL3.3, but documents intent
}


void Game::DrawDbgNpcs() const {
    // posed debug dummies (EI_NPC) - frozen or animated in place, no AI
    for (const DbgNpc& n : dbgNpcs) {
        float t = n.frozen ? n.anim
                           : n.anim + dbgT * (n.walk > 0.f ? 5.5f : 1.f);
        DrawHumanoid(n.pos, n.yaw, n.app, n.seated, t, n.raise, n.seatH, n.walk, n.detain,
                     n.look ? cam.position : Vector3{});
    }
}

// the clerk's own body: headless while first-person (the camera is the
// head), a full seated figure at the desk on the title screen
void Game::DrawPlayerBody() {
    float t = (float)GetTime();
    if (mode == Mode::Title) {
        // the menu drifts across the office facing the desk - the clerk is
        // actually there, hunched over the paperwork between shifts
        DrawHumanoid({office.seatCam.x, 0.f, office.seatCam.z + 0.06f},
                     180.f, playerApp, true, t * 0.7f, 0.f, 0.5f);
        return;
    }
    if (mode == Mode::Dead || mode == Mode::Win || mode == Mode::Jumpscare) return;
    bool seated = (mode == Mode::SitVisitor || mode == Mode::SitComputer);
    if (seated) {
        // hips just behind the eye point, thighs reaching under the desk
        DrawHumanoid({office.seatCam.x, 0.f, office.seatCam.z + 0.06f},
                     180.f, playerApp, true, t * 0.7f, 0.f, 0.5f,
                     0.f, 0.f, {}, true);
        return;
    }
    // hidden under the desk: the tucked body would clip through the desk
    // underside and fill the whole view - you're curled up, don't draw it
    if (player.hiding) return;
    bool crouch = player.crouching;
    // your own chin hides your chest until you actually look down - and the
    // mid-pitch band just shows a shoulder slab, so the body only appears
    // once the view is steep enough to read chest-front -> legs -> feet.
    // same gate while crouched: the tucked body must not fill a forward view
    if (player.pitch > -40.f) return;
    float yr = player.yaw * DEG2RAD;
    // Minecraft-style: the eye sits at the body's own head (a hair behind
    // the neck axis so the near plane clips the chin/neck, not the chest),
    // so looking down reads MY torso below me -> legs -> feet, not a body
    // standing in front of me
    float band = std::clamp((-player.pitch - 40.f) / 16.f, 0.f, 1.f);
    // crouched: the tucked body is closer to the lowered eye - push it a
    // touch further forward so the same read holds at the smaller scale
    float fwd = crouch ? 0.22f : 0.02f;
    Vector3 at{player.pos.x + std::sin(yr) * fwd, -(1.f - band) * 1.35f,
               player.pos.z + std::cos(yr) * fwd};
    // close under the eye + slightly forward: torso fills center frame,
    // legs and feet extend to the bottom edge (Minecraft-style framing)
    HumanoidApp fp = playerApp; fp.build = 0.92f; fp.height = 0.95f;
    // leaning the whole humanoid ~20 deg forward about its hip axis is
    // what sells "looking along my own body": the chest turns to face the
    // downward view and the legs/feet recede past it to the bottom edge
    // instead of hiding behind the chest-top face. The crouch pose is
    // already tucked - the lean would pitch knees into the camera, skip it
    // sprint leans the whole body a few extra degrees - the tucked-chin
    // pitch sells the speed alongside the wider fov
    bool lean = !crouch;
    float leanDeg = -20.f - 7.f * player.moveAmt * (player.sprinting ? 1.f : 0.f);
    if (lean) {
        rlPushMatrix();
        float pivY = at.y + 0.95f * fp.height;
        rlTranslatef(at.x, pivY, at.z);
        rlRotatef(leanDeg, std::cos(yr), 0.f, -std::sin(yr));
        rlTranslatef(-at.x, -pivY, -at.z);
    }
    // stride phase is bobT-locked (footsteps land on the swing) and the
    // amplitude follows speed: sprint throws the full cycle, the crouch-
    // shuffle stays small under the tucked pose
    float wAmt = player.moveAmt * (player.sprinting ? 1.15f :
                                   crouch ? 0.55f : 0.9f);
    DrawHumanoid(at, player.yaw, fp, crouch,
                 fpStepT, 0.f, crouch ? 0.30f : 0.5f,
                 wAmt, 0.f, {}, true);
    if (lean) rlPopMatrix();
}

void Game::DrawWorld() {
    office.clockHour = gameHour;
    if (shake > 0.f) {
        Vector3 so{(Rand01() - 0.5f) * shake * 0.10f,
                   (Rand01() - 0.5f) * shake * 0.10f,
                   (Rand01() - 0.5f) * shake * 0.05f};
        cam.position = Vector3Add(cam.position, so);
        cam.target = Vector3Add(cam.target, so);
    }
    if (fx.ok) {
        fx.EnsureSize();
        BeginTextureMode(fx.scene);
        ClearBackground(fx.fogColor);
        fx.SetViewPos(cam.position);
        fx.PushLights();
        BeginMode3D(cam);
        fx.BeginWorld();              // lighting shader on
        if (!DevEnv("EI_NOITEMS")) office.Draw();
        if (!DevEnv("EI_NOVIS")) vs.Draw();
        if (!DevEnv("EI_NOBODY")) DrawPlayerBody();
        DrawDbgNpcs();
        DrawCrew();
        fx.LitOff();                  // figures stay unlit silhouettes
        if (ad.IntruderActive()) {
            Vector3 ip = ad.IntruderPos();
            Vector3 e = player.Eye();
            float yaw = std::atan2(e.x - ip.x, e.z - ip.z) * RAD2DEG;
            Office::DrawShadowFigure(ip, yaw, 1.08f);
        }
        if (breachRush >= 0.f) {          // the window figure crossing the room
            Vector3 e = player.Eye();
            Vector3 p{breachFrom.x + (e.x - breachFrom.x) * breachRush, 0.f,
                      breachFrom.z + (e.z - breachFrom.z) * breachRush};
            float yaw = std::atan2(e.x - p.x, e.z - p.z) * RAD2DEG;
            Office::DrawShadowFigure(p, yaw, 1.18f);
        } else if (breachSearchT > 0) {   // it looms beside the desk, hunting
            Vector3 d = office.Find("desk") ? office.Find("desk")->pos : Vector3{0.f, 0.f, 1.6f};
            Vector3 p{d.x - 0.9f, 0.f, d.z - 0.35f};
            float yaw = std::atan2(d.x - p.x, d.z - p.z) * RAD2DEG;
            Office::DrawShadowFigure(p, yaw, 1.18f);
        }
        if (mode == Mode::Jumpscare) {    // the killer, inches from the lens
            float s = std::min(1.f, scareT / 1.35f);
            Vector3 e = player.Eye();
            float yaw = std::atan2(e.x - scareFig.x, e.z - scareFig.z) * RAD2DEG;
            Office::DrawShadowFigure({scareFig.x, 0.f, scareFig.z},
                                     yaw, 1.02f + s * 0.12f, s, s);
        }
        DrawMonitorPoly();            // OS screen: real world-space quad, unlit
        office.DrawEyes();            // anomaly eyes: unlit pale domes, never blacked out
        DrawHumanoidEyes();           // queued humanoid eyes - unlit, readable
        // dust motes: faint warm specks drifting in the lamp/monitor glow -
        // unlit so they still read during lights-out, twinkling per-mote
        for (size_t i = 0; i < motes.size(); i++) {
            float tw = 0.55f + 0.45f * std::sin((float)GetTime() * 1.7f + (float)i * 1.3f);
            DrawCube(motes[i], 0.0055f, 0.0055f, 0.0055f,
                     Color{255, 236, 200, (unsigned char)(70 * tw)});
        }
        office.DrawGlass();           // translucent - must be last in 3D pass
        if (DevEnv("EI_DIAG")) {  // leftover transform here corrupts whole batch
            static double bdNext = 0;
            if (GetTime() >= bdNext) {
                bdNext = GetTime() + 1.0;
                Matrix t = rlGetMatrixTransform();
                bool ident = std::fabs(t.m0 - 1) < 1e-4f && std::fabs(t.m5 - 1) < 1e-4f &&
                             std::fabs(t.m10 - 1) < 1e-4f && std::fabs(t.m15 - 1) < 1e-4f &&
                             std::fabs(t.m12) < 1e-4f && std::fabs(t.m13) < 1e-4f && std::fabs(t.m14) < 1e-4f;
                TraceLog(LOG_WARNING, "[batch] transform ident=%d m0=%.3f m5=%.3f m12=%.3f m13=%.3f m14=%.3f",
                         ident, t.m0, t.m5, t.m12, t.m13, t.m14);
            }
        }
        EndMode3D();
        EndTextureMode();
        if (DevEnv("EI_DUMPRT")) {   // diag: dump raw scene RT at fixed times
            static float dumpT[4] = {8.f, 22.f, -1.f, -1.f};
            static int dumped = 0;
            if (dumped < 2) {
                float t = (float)GetTime();
                for (int i = 0; i < 2; i++) if (dumpT[i] > 0.f && t >= dumpT[i]) {
                    dumpT[i] = -1.f; dumped++;
                    Image img = LoadImageFromTexture(fx.scene.texture);
                    ImageFlipVertical(&img);
                    char fn[40]; snprintf(fn, 40, "shots/ei_rt_%d.png", (int)t);
                    ExportImage(img, fn);
                    UnloadImage(img);
                }
            }
        }
        return;
    }
    // fallback: unlit direct render if shaders failed
    BeginMode3D(cam);
    office.Draw();
    office.DrawEyes();
    vs.Draw();
    DrawDbgNpcs();
    DrawCrew();
    DrawHumanoidEyes();
    if (ad.IntruderActive()) {
        Vector3 ip = ad.IntruderPos();
        Vector3 e = player.Eye();
        float yaw = std::atan2(e.x - ip.x, e.z - ip.z) * RAD2DEG;
        Office::DrawShadowFigure(ip, yaw, 1.08f);
    }
    if (mode == Mode::Jumpscare) {
        float s = std::min(1.f, scareT / 1.35f);
        Vector3 e = player.Eye();
        float yaw = std::atan2(e.x - scareFig.x, e.z - scareFig.z) * RAD2DEG;
        Office::DrawShadowFigure({scareFig.x, 0.f, scareFig.z},
                                 yaw, 1.02f + s * 0.12f, s, s);
    }
    office.DrawGlass();
    EndMode3D();
    if (debug && !cleanDebug && osTexOk) // F3: OS texture thumbnail to verify FBO content/orientation
        DrawTexturePro(osTex.texture,
                       Rectangle{0, 0, (float)osTex.texture.width, -(float)osTex.texture.height},
                       Rectangle{10, 10, 312, 192}, Vector2{0, 0}, 0, WHITE);
}


// ---------- main loop ----------

void Game::Run() {
    while (!quit && !WindowShouldClose()) {
        float dt = GetFrameTime();
        dt = std::min(dt, 0.1f);
        if (recDumpFps > 0.f) {
            // recording mode: fixed timestep so PNG-dump cost never warps the
            // footage - every rendered frame is exactly one video frame
            dt = 1.f / recDumpFps;
            recTime += dt;
        }
        int W = GetScreenWidth(), H = GetScreenHeight();

        if (IsKeyPressed(KEY_F3)) debug = !debug;
        if (debug && IsKeyPressed(KEY_F4)) {
            flycam = !flycam;
            if (flycam) flyPos = cam.position;
        }
        // F7 bookmarks the flycam pose, F8 snaps back to it (entering flycam
        // if needed) - repeatable camera spots for debug screenshots
        if (debug && flycam && IsKeyPressed(KEY_F7)) {
            dbgPos = flyPos; dbgYaw = player.yaw; dbgPitch = player.pitch;
            dbgHasCam = true;
            Notify("[flycam] pose bookmarked");
        }
        if (debug && dbgHasCam && IsKeyPressed(KEY_F8)) {
            flycam = true;
            flyPos = dbgPos; player.yaw = dbgYaw; player.pitch = dbgPitch;
            Notify("[flycam] snapped to bookmark");
        }
        // F9: hot-reload content JSON (anomaly catalog, dialogs, config,
        // remix parts). Live anomalies keep their snapshots; office layout
        // stays put - only defs/tuning swap.
        if (debug && IsKeyPressed(KEY_F9)) {
            if (A.ReloadData()) {
                ad.ReloadDefs(A.anomalies, Cfg()["instability"], A.anomalyParts);
                Notify("assets reloaded");
                for (auto& w : A.warnings) TraceLog(LOG_WARNING, "assets: %s", w.c_str());
            } else Notify("reload failed: " + A.lastError);
        }

        // menu music: title + ending screens carry a streamed theme loop,
        // rotating through every MP3 found in assets/Music (drop new tracks
        // in and they join the rotation); the second a shift starts it stops
        // - the building's own drone takes over. Failed loads are remembered
        // per-path in AudioBank so bad files cost one attempt each, then
        // silence.
        {
            static std::vector<std::string> themes;
            static int themeIdx = 0, themesNight = -1;
            if (themes.empty() || themesNight != night) {   // rescan on night change
                themes.clear();
                std::string mdir = A.dir + "/Music";
                std::error_code ec;
                for (auto& e : std::filesystem::directory_iterator(mdir, ec)) {
                    std::string ext = e.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char ch) { return std::tolower(ch); });
                    if (ext != ".mp3" && ext != ".flac" && ext != ".ogg" &&
                        ext != ".wav") continue;
                    // "Not as it seems" only unlocks once the clerk has
                    // survived a whole shift - night 2 onward
                    std::string fn = e.path().filename().string();
                    std::transform(fn.begin(), fn.end(), fn.begin(),
                                   [](unsigned char ch) { return std::tolower(ch); });
                    if (fn == "not as it seems.flac" && night < 2) continue;
                    themes.push_back(e.path().string());
                }
                std::sort(themes.begin(), themes.end());
                themesNight = night;
                if (!themes.empty()) themeIdx %= (int)themes.size();
                TraceLog(LOG_INFO, "[music] %d menu tracks in rotation (night %d)",
                         (int)themes.size(), night);
            }
            bool menuCtx = (mode == Mode::Title || mode == Mode::Dead || mode == Mode::Win);
            if (!musicEnabled && audio.MenuPlaying()) audio.StopMenuMusic();
            if (menuCtx && musicEnabled && !audio.MenuPlaying() && !themes.empty()) {
                audio.StartMenuMusic(themes[themeIdx % themes.size()].c_str());
                themeIdx = (themeIdx + 1) % (int)themes.size();
            }
            else if (!menuCtx && audio.MenuPlaying()) audio.StopMenuMusic();
            audio.UpdateMenuMusic();
        }

        // HUD pop-up lifetimes tick in EVERY mode - pausing with a toast or
        // the lore card up used to freeze their timers while the draw kept
        // running (the translucent pause dim let them bleed through), so a
        // note could sit on the screen forever until unpaused
        for (auto& n : notes) n.second -= dt;
        std::erase_if(notes, [](auto& n) { return n.second <= 0; });
        if (loreCardT > 0.f) loreCardT -= dt;

        switch (mode) {
            case Mode::Title: {
                // back at the menu the theme unbends - same stream, normal pitch,
                // position preserved (never restarted by the death screen)
                audio.SetMenuMusicPitch(1.f);
                // the boot release check often fires before wifi associates -
                // while the menu sits open, keep retrying a failed/Idle check
                // on a slow timer so a late connection still finds updates
                if (updater) {
                    updRetryT += dt;
                    if (updRetryT >= (updRetries == 0 ? 6.f : 15.f) &&
                        updRetries < 15) {
                        if (updater->GetState() == GameUpdater::State::Idle) {
                            updater->Retry();
                            updRetries++;
                        }
                        updRetryT = 0.f;
                    }
                    // AUTO-UPDATE (settings): a found release downloads in the
                    // background, then applies the moment the package is ready -
                    // the script relaunches the game itself. Saves/settings/key
                    // ride along untouched (they live outside the manifest)
                    if (autoUpdate) {
                        auto us = updater->GetState();
                        if (us == GameUpdater::State::Available &&
                            !updater->Assetless())
                            updater->BeginDownload();
                        else if (us == GameUpdater::State::Ready &&
                                 updater->ApplyAndRelaunch())
                            quit = true;
                    }
                }
                // dev hook: EI_MENU=<tab 0..3> pins the settings page open (shots)
                if (const char* e = DevEnv("EI_MENU")) {
                    menuPage = 1;
                    settingsTab = std::clamp(atoi(e), 0, 3);
                }
                // slow drift on the office's front-left arc, looking over the
                // desk's lamp-side corner at the seated clerk in the chair
                float t = (float)GetTime();
                cam.position = {-1.0f + std::sin(t * 0.07f) * 1.5f,
                                1.62f + std::sin(t * 0.05f) * 0.09f,
                                1.3f + std::cos(t * 0.055f) * 0.7f};
                cam.up = {0.f, 1.f, 0.f};
                cam.target = {0.f, 1.0f, 2.7f};
                cam.fovy = fovBase;
                audio.SetDroneLevel(0.22f);
                // the night's cast generates while the menu idles (stub makes
                // procedural personas offline, so runs stay fresh either way)
                if (rosterReq == -1 && rosterRaw.empty()) RequestRoster();
                if (paperReq == -1 && paperRaw.empty()) RequestPaperwork();
                if (callEvReq == -1 && !callGenDone) RequestCallPersonas();
                PollLLM();
                if (confirmTut) {
                    // the tutorial-offer modal owns the keyboard
                    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_Y)) {
                        confirmTut = false; NewShift(false);
                    } else if (IsKeyPressed(KEY_N)) {
                        confirmTut = false; tutorialSkip = true; night = 1;
                        nightBeforeEnv = -1; SaveSettings(); NewShift(false);
                    } else if (IsKeyPressed(KEY_ESCAPE)) confirmTut = false;
                }
                else if (menuPage == 1) {
                    // settings page: ESC backs out (or cancels an armed bind/field)
                    if (IsKeyPressed(KEY_ESCAPE)) {
                        if (rebinding >= 0) rebinding = -1;
                        else if (focusApiKey || editId) { focusApiKey = false; editId = 0; }
                        else menuPage = 0;
                    }
                } else {
                    // ENTER applies the API key while its field has focus instead of starting
                    if (IsKeyPressed(KEY_ENTER) && !focusApiKey && !editId) {
                        if (night == 0 && !tutorialSkip) confirmTut = true;
                        else NewShift(false);
                    }
                    else if (saveExists && IsKeyPressed(KEY_C) && !focusApiKey) { if (Load()) { mode = Mode::Roam; DisableCursor(); Notify("Save loaded."); } }
                    else if (IsKeyPressed(KEY_Q) && !focusApiKey) { quit = true; }
                    else if (IsKeyPressed(KEY_R) && !focusApiKey && night != 1) { night = 1; SaveSettings(); }   // wipe record, or skip training on 0
                }
                break;
            }
            case Mode::Roam:
                UpdateRoam(dt);
                UpdateCommon(dt);
                // dev hook: EI_PAUSE=1 pops the pause menu once (shots)
                if (DevEnv("EI_PAUSE")) {
                    static bool once = false;
                    if (!once) { once = true; pauseReturn = mode; pauseReason.clear(); menuPage = 0; mode = Mode::Pause; EnableCursor(); }
                }
                break;
            case Mode::SitVisitor:
            case Mode::SitComputer:
                UpdateSeated(dt);
                UpdateCommon(dt);
                if (DevEnv("EI_PAUSE")) {
                    static bool onceS = false;
                    if (!onceS) { onceS = true; pauseReturn = mode; pauseReason.clear(); menuPage = 0; mode = Mode::Pause; EnableCursor(); }
                }
                break;
            case Mode::Unseating:
                UpdateUnseating(dt);
                UpdateCommon(dt);
                break;
            case Mode::Pause:
                if (IsKeyPressed(KEY_ESCAPE)) {
                    if (rebinding >= 0) rebinding = -1;                    // unarm, keep the key
                    else if (focusApiKey || editId) { focusApiKey = false; editId = 0; }
                    else if (menuPage == 1) menuPage = 0;
                    else { mode = pauseReturn; if (pauseReturn == Mode::Roam) DisableCursor(); }
                }
                break;
            case Mode::Jumpscare: {
                scareT += dt;
                float s = std::min(1.f, scareT / 1.35f);
                // it closes in: the figure rushes from its standing point to a
                // third of a meter off the player's eye while the camera is
                // dragged to meet it - no input, nowhere else to look
                Vector3 e = player.Eye();
                Vector3 flat{scareFrom.x - e.x, 0.f, scareFrom.z - e.z};
                float d = Vector3Length(flat);
                Vector3 dir = d > 0.01f ? Vector3Scale(flat, 1.f / d)
                                        : Vector3{std::sin(player.yaw * DEG2RAD), 0.f,
                                                  std::cos(player.yaw * DEG2RAD)};
                float lunge = 1.f - (1.f - s) * (1.f - s);        // ease-out rush
                float dist = std::max(0.34f, d - (d - 0.34f) * lunge);
                scareFig = {e.x + dir.x * dist, 0.f, e.z + dir.z * dist};
                cam.position = e;
                // look up into the face - head drops toward lens height as s->1
                cam.target = {scareFig.x + dir.x * 0.12f * s, 1.50f + s * 0.12f,
                              scareFig.z + dir.z * 0.12f * s};
                cam.up = {0.f, 1.f, 0.f};
                cam.fovy = fovBase - s * 9.f;                   // dolly punch
                shake = 0.55f + s * 0.85f;
                if (!scareHit2 && scareT > 0.62f) {             // contact hit
                    scareHit2 = true;
                    audio.Play(AudioBank::Sfx::Screech, 1.f);
                    audio.Play(AudioBank::Sfx::Heart, 0.9f);
                }
                if (scareT > 1.35f) Die(deathTitle, ending);
                break;
            }
            case Mode::Dead:
            case Mode::Win:
                if (IsKeyPressed(KEY_ENTER)) { mode = Mode::Title; weekJustDone = false; }
                break;
        }

        // one keyboard owner per frame: the OS text fields only read chars while
        // the player is at the computer; other modes drain/discard below
        os.acceptsText = (mode == Mode::SitComputer);
        // on the phone the real cursor drives the call panel - keep it free and
        // visible whatever path we took here (EnableCursor unlocks + unhides)
        if (mode == Mode::Roam && phone.answered) { if (IsCursorHidden()) EnableCursor(); }
        // at the computer only the projected OS cursor exists - the real one
        // hides so the two can never appear misaligned
        else if (mode == Mode::SitComputer && sitT > 0.85f) { if (!IsCursorHidden()) HideCursor(); }
        else if (mode != Mode::Roam && mode != Mode::Unseating && IsCursorHidden()) ShowCursor();
        if (mode != Mode::Title && mode != Mode::Pause) { focusApiKey = false; editId = 0; rebinding = -1; }
        if (mode != Mode::SitComputer && mode != Mode::SitVisitor && !focusApiKey &&
            !phone.answered && editId == 0)
            while (GetCharPressed() > 0) {}

        // fake OS renders into a texture that lives on the in-world monitor
        if (osTexOk) {
            Vector2 vm{-9999.f, -9999.f};
            os.interactive = false;
            if (mode == Mode::SitComputer && sitT > 0.85f) {
                // traditional cursor: cast the real mouse onto the monitor face
                Ray r = GetScreenToWorldRay(GetMousePosition(), cam);
                Vector2 uv;
                if (office.MonitorUV(r.position, r.direction, uv)) {
                    vm = {uv.x * (float)osTex.texture.width, uv.y * (float)osTex.texture.height};
                    os.interactive = true;
                }
            }
            os.mouse = vm;
            os.clockHour = gameHour;              // taskbar clock tracks shift time
            // POST overlay only runs while the player actually sits at the
            // terminal - not during the walk over
            if (mode == Mode::SitComputer && os.bootT > 0.f) os.bootT -= dt;
            BeginTextureMode(osTex);
            ClearBackground(Color{14, 17, 22, 255});
            os.Draw(A, osTex.texture.width, osTex.texture.height);
            EndTextureMode();
        }

        // post params for this frame
        fx.time = (float)GetTime();
        fx.instab = ad.Instability() / 100.f;
        fx.scare = (mode == Mode::Jumpscare) ? std::min(1.f, scareT / 1.35f) : 0.f;

        // door panel swings for a passing visitor, or stays cracked if the player peeked
        if (Furni* d = office.Find("door")) {
            bool crossing = vs.HasActiveVisitor() &&
                (vs.Cur().state == VState::ToChair || vs.Cur().state == VState::Leaving) &&
                std::fabs(vs.Cur().pos.z + 5.f) < 1.6f;
            float tgt = (crossing || doorOpen) ? 1.f : 0.f;
            float dt = GetFrameTime();
            // dead-band: at the target we STOP - the old ternary read "at
            // target" as "closing" and stepped down every frame, so the open
            // door sat there visibly buzzing 1.0 -> .95 -> .99 -> 1.0
            if (tgt > doorAnim)      doorAnim = std::min(tgt, doorAnim + 2.6f * dt);
            else if (tgt < doorAnim) doorAnim = std::max(tgt, doorAnim - 3.2f * dt);
            float e = doorAnim * doorAnim * (3.f - 2.f * doorAnim);   // smoothstep
            if (office.doorBroken)
                // torn off its hinges: the panel hangs sprung open at a
                // crooked angle - it can no longer close, or be closed
                d->rotY = -58.f + std::sin((float)GetTime() * 1.7f) * 0.8f;
            else
                d->rotY = -78.f * e - doorRattle * (0.6f + doorRattle) *
                          std::sin((float)GetTime() * 55.f);   // swings inward; banger hits rattle it
            if (DevEnv("EI_DOORTRACE"))
                TraceLog(LOG_INFO, "[door] anim=%.3f rotY=%.2f open=%d cross=%d vst=%d z=%.2f",
                         doorAnim, d->rotY, (int)doorOpen, (int)crossing,
                         (int)vs.Cur().state, vs.Cur().pos.z);
        }

        bool worldVisible = (mode != Mode::Dead && mode != Mode::Win);
        if (fx.ok && worldVisible) DrawWorld();   // fills the scene render texture

        BeginDrawing();
        ClearBackground(Color{8, 8, 10, 255});

        if (mode == Mode::Title) {
            if (fx.ok) fx.Blit(); else DrawWorld();
            DrawTitle();
        }
        else if (mode == Mode::Dead || mode == Mode::Win) DrawEnding();
        else {
            if (fx.ok) fx.Blit(); else DrawWorld();
            if (debug && !cleanDebug && osTexOk) // F3: OS texture thumbnail (FBO content/orientation)
                DrawTexturePro(osTex.texture,
                               Rectangle{0, 0, (float)osTex.texture.width, -(float)osTex.texture.height},
                               Rectangle{10, 10, 312, 192}, Vector2{0, 0}, 0, WHITE);
            if (mode == Mode::SitVisitor && vs.VisitorSeated())
                dlg.Draw(A, W, H, vs.Cur().Name(), vs.transcript);
            if (phone.answered && mode != Mode::Pause && mode != Mode::Jumpscare)
                phoneDlg.Draw(A, W, H, JS(phone.ev, "persona"), phone.transcript);
            else if (phone.ringing && mode != Mode::Pause && mode != Mode::Jumpscare &&
                     pathKeys.empty() && !cleanDebug) {
                // visible ring indicator so it can't be mistaken for ambience
                std::string ring = "the desk phone is ringing";
                Vector2 rts = MeasureTextEx(A.fontSmall, ring.c_str(), 21, 1);
                float blink = 0.55f + 0.45f * std::sin((float)GetTime() * 9.f);
                DrawTextEx(A.fontSmall, ring.c_str(),
                           {W / 2.f - rts.x / 2, H - 130.f}, 21, 1,
                           Color{235, 200, 110, (unsigned char)(blink * 255)});
            }

            // no HUD pop-ups over the pause menu or mid-jumpscare: DrawPause's
            // translucent dim lets bright cards bleed through, and their
            // timers now tick every frame so they still expire behind it
            bool hudOk = mode != Mode::Pause && mode != Mode::Jumpscare;
            if (hudOk && pathKeys.empty() && !cleanDebug)
                HUD::DrawClock(A, W, gameHour, debug, ad.Instability(), night);
            if (mode == Mode::Roam && player.hiding && !cleanDebug) {
                // the taped note under the desk - reads itself to you while
                // you hide: why you're down here, and what comes after
                static const char* nl[] = {
                    "a note is taped beneath the desk, in your handwriting:",
                    "\"if the wrong ones get in - get under. don't breathe.",
                    " they check the chair. they never check the floor.",
                    " when the room goes quiet: file the report. - M.\""};
                float nw = 0;
                for (auto* s : nl) nw = std::max(nw, MeasureTextEx(A.font, s, 21, 1).x);
                Rectangle card{W / 2.f - nw / 2.f - 22.f, H - 268.f, nw + 44.f, 128.f};
                // a soft drop-shadow lifts the card off the dark floor behind it
                DrawRectangleRounded({card.x + 5.f, card.y + 6.f, card.width, card.height},
                                     0.07f, 6, Color{0, 0, 0, 110});
                DrawRectangleRounded(card, 0.07f, 6, Color{214, 200, 162, 246});
                DrawRectangleLinesEx(card, 1.5f, Color{126, 104, 70, 200});
                for (int i = 0; i < 4; i++)
                    DrawTextEx(A.font, nl[i], {card.x + 20.f, card.y + 14.f + i * 26.f},
                               21, 1, Color{44, 36, 28, 255});
                const char* hid = "hidden under the desk";
                Vector2 hs = MeasureTextEx(A.fontSmall, hid, 18, 1);
                DrawTextEx(A.fontSmall, hid, {W / 2.f - hs.x / 2, H - 104.f}, 18, 1,
                           Color{140, 175, 190, 200});
            }
            if (pathKeys.empty() && !cleanDebug) {
            if (hudOk) {
            HUD::DrawPrompt(A, W, H, mode == Mode::Roam ?
                (phone.answered ? "[ESC] hang up" : interactLabel) :
                (mode == Mode::SitComputer
                    ? ("[" + std::string(KeyName(keys.swap)) + "] face visitor - hold [" +
                       KeyName(keys.left) + "/" + KeyName(keys.right) + "] lean - [ESC] stand up")
                    : mode == Mode::SitVisitor
                    ? ("[" + std::string(KeyName(keys.swap)) + "] face monitor - hold [" +
                       KeyName(keys.left) + "/" + KeyName(keys.right) + "] lean - [ESC] stand up")
                    : ""));
            HUD::DrawNotes(A, W, notes);
            // night-0 lesson card: the world dims around it, the focus
            // element is named in the header, and it stays up until the step
            // advances - lessons don't evaporate mid-action
            if (!tutMsg.empty() && mode != Mode::Jumpscare && mode != Mode::Dead) {
                DrawRectangle(0, 0, W, H, Color{0, 0, 0, 46});          // soft dim
                float tw = std::sin((float)GetTime() * 3.2f) * 0.5f + 0.5f;
                std::vector<std::string> wl = WrapText(A.font, tutMsg, 19, 1, 560.f);
                float cw = 640.f, ch = 44.f + wl.size() * 24.f + (tutFocus.empty() ? 0.f : 24.f);
                Rectangle card{W / 2.f - cw / 2.f, H - ch - 66.f, cw, ch};
                DrawRectangleRounded(card, 0.10f, 6, Color{12, 14, 20, 235});
                DrawRectangleRoundedLinesEx(card, 0.10f, 6, 2.f,
                    Color{120, 170, 210, (unsigned char)(150 + tw * 80)});
                // header: ORIENTATION + the highlighted focus chip
                float hx = card.x + 18.f;
                DrawTextEx(A.fontSmall, "ORIENTATION", {hx, card.y + 12.f}, 16, 1,
                           Color{150, 200, 235, 255});
                hx += MeasureTextEx(A.fontSmall, "ORIENTATION", 16, 1).x + 12.f;
                if (!tutFocus.empty()) {
                    Vector2 fs = MeasureTextEx(A.fontSmall, tutFocus.c_str(), 15, 1);
                    DrawRectangleRounded({hx - 5.f, card.y + 9.f, fs.x + 12.f, 22.f},
                                         0.3f, 4, Color{60, 90, 120, 235});
                    DrawTextEx(A.fontSmall, tutFocus.c_str(), {hx + 1.f, card.y + 12.f},
                               15, 1, Color{235, 240, 250, 255});
                }
                float ty = card.y + 40.f;
                for (auto& s : wl) {
                    DrawTextEx(A.font, s.c_str(), {card.x + 18.f, ty}, 19, 1,
                               Color{225, 230, 238, 255});
                    ty += 24.f;
                }
            }
            }
            if (debug) DrawDebugOverlay();
            }
            if (hudOk && loreCardT > 0.f && !loreText.empty() && !cleanDebug) {
                // the slid-in note, read: same parchment card the hide note uses
                std::vector<std::string> wl = WrapText(A.font, loreText, 21, 1, W * 0.44f);
                float nw = 0;
                for (auto& s : wl) nw = std::max(nw, MeasureTextEx(A.font, s.c_str(), 21, 1).x);
                Rectangle card{W / 2.f - nw / 2.f - 24.f, H * 0.30f, nw + 48.f,
                               (float)wl.size() * 28.f + 52.f};
                DrawRectangleRounded({card.x + 5.f, card.y + 6.f, card.width, card.height},
                                     0.07f, 6, Color{0, 0, 0, 110});
                DrawRectangleRounded(card, 0.07f, 6, Color{214, 200, 162, 246});
                DrawRectangleLinesEx(card, 1.5f, Color{126, 104, 70, 200});
                for (int i = 0; i < (int)wl.size(); i++)
                    DrawTextEx(A.font, wl[i].c_str(), {card.x + 24.f, card.y + 16.f + i * 28.f},
                               21, 1, Color{44, 36, 28, 255});
                const char* hint = "a note, slid in from somewhere   -   [E] put it down";
                Vector2 hs = MeasureTextEx(A.fontSmall, hint, 18, 1);
                DrawTextEx(A.fontSmall, hint, {W / 2.f - hs.x / 2, card.y + card.height - 22.f},
                           18, 1, Color{120, 100, 72, 255});
            }
            if (mode == Mode::Roam && !phone.answered && pathKeys.empty() && !cleanDebug)
                HUD::DrawCrosshair(W, H);
            if (mode == Mode::Jumpscare) {
                // the world does the talking now - this only tightens the
                // blood-red vignette and swallows the edges as it lands
                float s = scareT / 1.35f;
                DrawRectangle(0, 0, W, H, Color{6, 2, 4, (unsigned char)(30 + s * 150)});
                float pulse = 0.85f + 0.15f * std::sin((float)GetTime() * 34.f);
                int ir = (int)(H * (1.35f - s * 0.85f) * pulse);
                DrawRectangleGradientEx({0, 0, (float)W, (float)H},
                                        Color{90, 10, 14, (unsigned char)(s * 160)},
                                        Color{90, 10, 14, (unsigned char)(s * 160)},
                                        BLANK, BLANK);
                for (int b = 0; b < 5; b++) {   // soft dark ring closing in
                    float a = (float)b / 5.f;
                    DrawRing({W / 2.f, H / 2.f}, (float)ir * (1.15f + a),
                             (float)ir * (1.45f + a * 1.3f), 0, 360, 48,
                             Color{8, 2, 4, (unsigned char)(s * 46)});
                }
            }
            if (mode == Mode::Pause) DrawPause();
        }
        if (IsKeyPressed(KEY_F12)) {
        Image img = LoadImageFromScreen();          // TakeScreenshot strips dirs
        ExportImage(img, "shots/screenshot.png");
        UnloadImage(img);
    }
        EndDrawing();
        static int bootFrames = 0;                   // [boot] prove the loop is presenting
        if (bootFrames >= 0 && ++bootFrames >= 3) {
            TraceLog(LOG_INFO, "[boot %.2fs] render loop alive", GetTime());
            bootFrames = -1;
        }
        if (!shotReq.empty()) {                      // seq-driven capture
            Image img = LoadImageFromScreen();
            ExportImage(img, shotReq.c_str());
            UnloadImage(img);
            shotReq.clear();
        }
        // dev hook: capture the just-presented frame, then quit (EI_SHOT=seconds)
        if (shotAt > 0.f && (float)GetTime() >= shotAt) {
        Image img = LoadImageFromScreen();          // TakeScreenshot strips dirs
        ExportImage(img, "shots/ei_shot.png");
        UnloadImage(img);
        quit = true;
    }
        // dev hook: trailer recording - dump EVERY frame as
        // shots/rec/f_%05d.png (dt is already fixed at 1/fps above, so one
        // rendered frame == one video frame). PNG = lossless, heavy I/O -
        // the fixed clock keeps timing right anyway
        if (recDumpFps > 0.f) {
            char nm[64];
            snprintf(nm, 64, "shots/rec/f_%05d.png", recFrame++);
            Image img = LoadImageFromScreen();
            ExportImage(img, nm);
            UnloadImage(img);
        }
    }
}


void Game::Shutdown() {
    SaveSettings();
    if (updater) updater->Stop();
    if (osTexOk) UnloadRenderTexture(osTex);
    fx.Shutdown();
    llm.Shutdown();
    audio.Shutdown();
    A.Unload();
}
