#pragma once
#include "common.h"
#include "assets.h"
#include "audio.h"
#include "player.h"
#include "office.h"
#include "visitor.h"
#include "anomalies.h"
#include "llm.h"
#include "fakeos.h"
#include "dialogui.h"
#include "fx.h"
#include "update.h"

enum class Mode { Title, Roam, SitVisitor, SitComputer, Unseating, Pause, Dead, Win, Jumpscare };

class Game {
public:
    bool Init();
    void Run();
    void Shutdown();

private:
    Assets A;
    Office office;
    Player player;
    VisitorSystem vs;
    AnomalyDirector ad;
    LLMClient llm;
    FakeOS os;
    DialogPanel dlg;
    AudioBank audio;
    FX::Renderer fx;
    Camera3D cam{};

    Mode mode = Mode::Title;
    Mode pauseReturn = Mode::Roam;
    float gameHour = 18.f;
    float sens = 0.12f;
    bool debug = false;
    bool saveExists = false;

    // seated camera blend
    float sitT = 0;
    Vector3 sitFromPos{}, sitFromTgt{};
    float fovFrom = 68, fovBase = 68;
    float lean = 0;                 // -1 left (visitor peek) .. +1 right (window peek)
    float unseatT = 0;
    Vector3 unseatFromPos{}, unseatFromTgt{};
    bool quit = false;
    float shotAt = -1;              // EI_SHOT env: dev screenshot timer

    RenderTexture2D osTex{};        // live fake-OS texture on the in-world monitor
    bool osTexOk = false;
    void DrawMonitorPoly(); // world-space quad on the monitor face (3D pass, unlit)

    float shake = 0;                // camera shake amount (decays)
    float blackoutT = 0;            // active light-failure timer
    bool doorOpen = false;          // player peeked the office door open
    float doorAnim = 0;             // 0 closed .. 1 swung open (hinge)
    bool doorBlocked = false;       // bookshelf_door live - nobody passes
    float bangDoorT = 0.f;          // door banger pounding progress -> broken_door
    float doorRattle = 0;           // transient door-panel shudder from banger hits
    bool manualLightsOff = false;   // player flicked the light switch off
    bool godMode = false;           // EI_GOD / seq 'god' - deaths log instead of firing
    bool weekDone = false;          // persisted: survived the final scaled night once
    bool weekJustDone = false;      // transient: that completion was THIS shift
    int nightBeforeEnv = -1;        // EI_NIGHT override saves back this value, not the forced night
    void CollectLights();           // build fx.lights from furniture + config

    // llm request ids
    int dlgReq = -1, repReq = -1, facReq = -1, deskReq = -1;
    int remixReq = -1, phoneReq = -1, phoneEvalReq = -1;
    int rosterReq = -1;                     // persona roster generation job
    std::string rosterRaw;                  // validated at shift start
    std::vector<std::string> seenNames;     // personas already used (persisted)
    std::set<std::string> loreSeen;         // lore ids met across runs (persisted)
    // in-flight "did that interview actually conclude" verdicts - a visitor
    // leaving triggers the eval, the reply lands here a few seconds later
    std::vector<std::pair<int, std::string>> ivEvals;   // (req id, visitor name)
    void RequestRoster();                   // ask the LLM for tonight's cast
    int callEvReq = -1;                     // LLM job authoring tonight's call personas
    bool callGenDone = false;               // fires once per title visit - injected into the pool
    void RequestCallPersonas();             // ask the LLM for new creepy callers
    std::string repText, facText;   // last submitted texts - for wrong-channel hints
    bool rlHandled = false;
    bool offlineNotified = false;
    bool hardcodedOnly = true;      // settings: scripted anomalies only (no LLM remix/calls)
    bool tutorialSkip = false;      // settings: night 0 orientation is optional
    bool autoUpdate = true;         // settings: download+apply releases hands-free
    bool confirmTut = false;        // title modal: "start the training shift?"

    // desk-phone anomaly call: an anomaly that phones you, persona-driven
    struct PhoneEvt {
        bool ringing = false, answered = false;
        float ringT = 0, ignoreT = 0;
        int exchanges = 0;
        bool isWork = false;        // mundane work call - flavor, no anomaly
        json ev;                    // anomalies.json call_events entry
        std::string anomalyId;      // the live anomaly entry it represents
        std::vector<std::pair<std::string, std::string>> transcript;
    };
    PhoneEvt phone;
    DialogPanel phoneDlg;

    // open blinds + something at the window = it can come through
    float breachT = 0;              // exposure timer while visible to it
    bool breachWarned = false;      // one-time "it noticed" cue
    float breachRush = -1;          // >=0: figure mid-rush, 0..1 crossing the room
    Vector3 breachFrom{0, 0, 0};    // window-side start position
    float breachSearchT = 0;        // it loiters at the desk while you hide
    float breachCooldown = 0;       // no instant re-breach after a survive
    float workCallT = 45.f;         // mundane inbound calls (payroll, IT, ...)
    std::set<int> workCallsUsed;    // work-call entries already phoned tonight
    bool noteSfx = false;           // the slid-note paper whisper played once
    std::string loreText;           // readable note/lore card contents
    float loreCardT = 0;            // seconds left on the lore card HUD
    bool allDutiesToasted = false;  // "all duties complete" fired once per shift
    bool visitorClosedDoor = false; // one-shot: visitor shut the open door
    float hallWhisperT = 0;         // next scratching sound from the walls
    int paperIdx = 0;               // paperwork queue position for the shift
    int night = 1;                  // persistent shift count - scales difficulty
    std::set<std::string> tutSeen;  // tutorial toasts already shown this shift
    void Tut(const char* key, const std::string& msg);  // fire-once night-1 hint
    int tutStep = -1;               // night-0 scripted lesson plan (-1 = off)
    void UpdateTutorial(float dt);  // advances the orientation sequence
    void UpdateThreats(float dt);   // window/door breach state machine
    void UpdateAnomalyKinds(float dt);  // flag-driven per-frame kinds
    std::string tutTitle;           // active lesson card: header + body + focus
    std::string tutMsg;
    std::string tutFocus;           // what the card points at ("the door")
    float tutMsgT = 0.f;            // final card self-expires on this
    int paperReq = -1;              // LLM paperwork generation job
    std::string paperRaw;           // generated forms, validated at shift start
    json paperForms = json::array();            // tonight's forms (LLM or stock)
    std::map<std::string, int> deeds;           // what the player actually did
    VState lastVState = VState::None;           // edge-detect for the un-interviewed warning
    bool askedCur = false;                      // player actually questioned the seated visitor
    float autoEndT = -1.f;                      // farewell phrase seen - interview wraps itself up
    float earlyEndT = -1.f;                     // every booking done -> clock out early countdown
    void RequestPaperwork();        // ask the LLM for tonight's forms
    bool ApplyPaperworkJson(const std::string& text);
    void OnIntruderStrike();        // intruder timer ran out
    void UpdatePhone(float dt);
    void AnswerPhone();
    void PhoneSay(const std::string& text);
    void HangUpPhone();
    void PhoneOutcome(bool calm);

    float scareT = 0;
    float ambT = 14.f;                  // next distant building-settle sfx
    std::vector<Vector3> motes;         // dust drifting in the desk light
    Vector3 scareFrom{0.f, 0.f, 0.f};   // where the killer stands / stood
    Vector3 scareFig{0.f, 0.f, 0.f};    // its lunging position this frame
    bool scareHit2 = false;             // second audio layer fired
    std::string ending, deathTitle;
    std::string pauseReason;
    float heartT = 0;               // instability heartbeat timer
    int evalScore = -1;             // shift evaluation, computed at win
    std::string lastEval;           // persisted "B (72)" for the title screen
    int EvalScore() const;
    static const char* EvalGrade(int score);

    std::vector<std::pair<std::string, float>> notes;
    std::string interactLabel;
    Furni* interactTarget = nullptr;
    float lastBobSin = 0;         // footstep edge detect on sin(bobT)
    float stepSide = 1;           // alternates L/R footstep pan
    float camRoll = 0;            // smoothed strafe lean (degrees)
    float settleDip = 0;          // stop-settle dip impulse
    bool wasMoving = false;
    bool intruderSeen = false;
    int settingsSel = 0;      // 0 provider, 1 api key, 2 sens, 3 back
    int providerIdx = 0;      // 0 groq, 1 local, 2 stub
    std::string apiKey;
    std::string llmEndpoint, llmModel;  // optional overrides; empty = config.json
    bool focusApiKey = false;
    int editId = 0;           // focused generic text field (0 none, 2 endpoint, 3 model)
    int testReq = -1;         // settings PING job id
    std::string testResult;   // last ping outcome line
    void ApiKeyField(Rectangle field, Rectangle pasteBtn);  // shared title/pause widget

    // menu/settings screens
    KeyMap keys;                        // rebindable controls (persisted)
    int menuPage = 0;                   // title/pause subpage: 0 main, 1 settings
    int settingsTab = 0;                // 0 general, 1 audio, 2 controls, 3 llm
    int rebinding = -1;                 // keybind row waiting for a keypress (-1 none)
    float volMaster = 1.f, volSfx = 1.f, volAmb = 1.f, volMusic = 1.f, volDrone = 1.f;
    bool musicEnabled = true;               // AUDIO tab toggle; off = no menu music, no chip
    std::string tip;                    // hovered widget's tooltip, set during draw
    void DrawSettings();
    bool MenuBtn(Rectangle r, const char* label, const char* tooltip = nullptr);
    void SliderRow(Rectangle r, const char* label, float& v, float lo, float hi,
                   const char* tooltip);
    void TextRow(Rectangle r, const char* label, std::string& s, int id,
                 const char* tooltip);
    void ApplyAudio();
    static std::string KeyName(int k);   // one buffer per call - safe inside big concat chains
    std::string BindLabel(const char* action, int key);   // "Sit at your desk  [E]"
    bool breachDoor = false;            // the breach rush came through the door

    HumanoidApp playerApp;              // the clerk's own look (FP body + title desk)
    void DrawPlayerBody();

    void Notify(const std::string& s);
    void NewShift(bool fromSave);
    void UpdateRoam(float dt);
    void UpdateSeated(float dt);
    void UpdateUnseating(float dt);
    void UpdateCommon(float dt);
    void DrawWorld();
    void DrawUI();
    void DrawTitle();
    void DrawPause();
    void DrawEnding();
    void DrawTooltipBar(float x, float y, float w);

    void DoInteract();
    void SitDown();
    void StandUp();
    void RefocusOsField();
    bool PlayerSees(Vector3 p) const;   // anomaly mut targets: no moves on-camera
    bool AllDutiesDone() const;         // every booked visit + every form done
    bool AllVisitorsDone() const;       // every booked visit resolved + nobody in the room
    void WinShift(bool early);          // shared shift-end path (dawn or early clock-out)

    // debug free-fly camera (F4 while F3 overlay is on): locks the player,
    // WASD + SPACE/CTRL moves the camera, F6 teleports the player to the lens,
    // F7 bookmarks the pose, F8 snaps back to it - EI_CAM sets it exact
    bool flycam = false;
    Vector3 flyPos{0.f, 1.4f, 0.f};
    Vector3 dbgPos{0, 0, 0};                // bookmarked debug-cam pose
    float dbgYaw = 0, dbgPitch = 0;
    bool dbgHasCam = false;
    void DrawDebugOverlay() const;

    // EI_NPC debug dummies - posed humanoids with no AI, for debug shots.
    // Never move on their own; DrawHumanoid renders them each frame
    struct DbgNpc {
        Vector3 pos{0, 0, 0};
        float yaw = 0, seatH = 0.5f;
        bool seated = false, female = false, frozen = false, male = false;
        bool look = false;   // eyes track the camera (EI_NPC 'look' flag)
        float walk = 0, raise = 0, detain = 0, anim = 0, height = 1.f;
        int hair = -1;              // -1 = pick from the seed hash
        std::string eyes;           // "" = persona default (normal)
        bool hasSkin = false, hasShirt = false, hasPants = false, hasHairC = false, hasEyeCol = false;
        Color skin{0,0,0,0}, shirt{0,0,0,0}, pants{0,0,0,0}, hairC{0,0,0,0}, eyeCol{0,0,0,0};
        std::map<std::string, std::string> acc;   // glasses/beard/tie/badge overrides
        HumanoidApp app;            // resolved at parse time
    };
    std::vector<DbgNpc> dbgNpcs;
    float dbgT = 0.f;   // dev anim/seq clock - accumulates dt so EI_RECDUMP's
                        // fixed timestep keeps poses stable frame-to-frame
    // EI_SEQ scripted gameplay timeline (trailer capture): "t,cmd; ..."
    std::vector<std::pair<float, std::string>> seq;
    size_t seqIdx = 0;
    std::set<int> simActive;                    // keys held by `hold:` actions
    std::vector<std::pair<int, float>> simHold; // keycode -> seconds remaining
    bool seqInvite = false;                     // invite the visitor the moment they reach the door
    void ExecSeq(const std::string& cmd);
    void DrawDbgNpcs() const;

    // scripted camera path (EI_PATH env): smoothstep-lerped keyframes the
    // flycam rides for trailer recording; HUD suppressed while active
    struct PathKey { float t, x, y, z, yaw, pitch; };
    std::vector<PathKey> pathKeys;
    float pathT0 = 0.f, pathEnd = -1.f;
    float recDumpFps = 0.f, recTime = 0.f;  // EI_RECDUMP: fixed-step dump clock
    bool cleanDebug = false;   // EI_CLEAN / auto under EI_RECDUMP: flycam on, debug chrome off
    int recFrame = 0;

    // github release auto-updater (src/update.h) - check runs once on title
    std::unique_ptr<class GameUpdater> updater;
    std::string gameVersion{"1.0.0"};
    std::string repoUrl;
    int updOpen = 0;          // update popup visible on title
    bool updSeen = false;     // auto-opened once already this boot
    float updRetryT = 0.f;    // title-screen recheck timer
    int updRetries = 0;       // bounded re-checks while offline/wifi late
    float fpStepT = 0.f;      // fp-body stride phase - locked to bobT while moving
    std::string shotReq;      // pending seq screenshot path (shots/<n>.png)
    float updScroll = 0.f;    // changelog scroll offset
    void DrawUpdatePopup();

    // crew dispatch - workers/guards physically walk in and fix anomalies
    enum CrewProp { PROP_NONE = 0, PROP_LADDER, PROP_BOX, PROP_TOOLBOX };
    struct CrewMan {
        Vector3 pos{0, 0, 0}; float yaw = 0, animT = 0, lift = 0, armRaise = 0, delay = 0;
        int wp = 0; bool carrying = false, guard = false;
        HumanoidApp app;
        std::vector<Vector2> pathIn, pathOut;
    };
    struct CrewEvt {
        std::vector<CrewMan> men;
        std::vector<ResolveOutcome> outcomes;   // applied when the work finishes
        Vector3 workPos{0, 0, 0};
        int phase = 0;                          // 0 walk in, 1 work, 2 walk out, 3 done
        float workT = 0, workLen = 3.0f;
        bool escortVisitor = false;             // guards collect the seated visitor
        std::string carryId;                    // spawned furniture carried out
        std::string dismantleId;                // spawned item too big/wired to carry - torn down on site
        int prop = PROP_NONE;                   // tool the lead man brings
        Vector3 ladderPos{0, 0, 0};             // planted ladder spot (PROP_LADDER)
        Vector3 boxPos{0, 0, 0};                // set-down spot for PROP_BOX
        bool propOut = false;                   // prop planted/set down during work
    };
    std::vector<CrewEvt> crew;
    bool crewOpenedDoor = false;
    void DispatchCrew(std::vector<ResolveOutcome> outs, int men, bool guards, bool escortVisitor = false);
    bool PointBlocked(float x, float z, float mgn);
    Vector2 PickStandSpot(Vector3 wp, float prefA);
    std::vector<Vector2> NavPath(Vector2 from, Vector2 to);   // A* around furniture
    void UpdateCrew(float dt);
    void DrawCrew() const;

    void SayToVisitor(const std::string& text);
    std::string BuildVisitorSystem() const;
    void SubmitReport(const std::string& text);
    void CallSecurity(const std::string& kind);   // "intruder" | "anomaly"
    void CallFacilities(const std::string& desc);
    void CallFrontDesk(const std::string& msg);
    std::string OtherChannelHint(const std::string& text, const std::string& tried);
    void PollLLM();

    void Jumpscare(const std::string& reason);
    void Die(const std::string& title, const std::string& body);
    void ConsumeLethalThreat();         // god mode: drop whatever just tried to kill
    bool VisitorShielded();             // intruder/breach takes an in-office visitor instead
    void Save();
    bool Load();
    void LoadSettings();
    void SaveSettings();
    void ApplyProvider();
    void ExportProfile();               // settings+save -> ei_profile.json (ZIP-build transfer)
    bool ImportProfile();               // ei_profile.json -> settings+save files

    std::vector<std::string> AppointmentsLines();
    std::vector<std::string> RecordLines();
    const json& Cfg() const { return A.config; }
};
