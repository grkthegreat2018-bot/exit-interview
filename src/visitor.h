#pragma once
#include "common.h"
#include "office.h"
#include <functional>

struct HumanoidApp {
    float height = 1.f;
    Color skin{200, 170, 145, 255}, shirt{80, 80, 95, 255}, hair{40, 35, 30, 255};
    std::string eyes = "normal";   // "normal" | "black" | "none"
    Color eyeCol{74, 52, 34, 255}; // iris tint used while eyes == "normal"
    Color pants{35, 35, 42, 255};
    bool female = false;
    // 0 cap | 1 buzz | 2 long | 3 ponytail | 4 bun | 5 balding | 6 bald
    int hairStyle = 0;
    // accessories - JSON-driven ("glasses","beard","tie","badge" in
    // appearance) with seeded fallbacks so no two staffers read alike
    bool glasses = false;
    Color glassesCol{30, 30, 36, 255};
    int beard = 0;                  // 0 none | 1 stubble/goatee | 2 full
    bool tie = false;
    Color tieCol{90, 40, 45, 255};
    bool badge = false;             // clip-on staff badge, left chest
};
HumanoidApp AppFromJson(const json& j);
// seatH = seat surface height for seated humanoids (chair 0.5, bench 0.25) -
// hips rest on it instead of floating at standing-hip height.
// walkAmt blends the standing walk-cycle swing (0 = standing still, 1 = full
// stride); it is auto-scaled by (1 - armRaise) so ladder work always wins.
// detain = security-escort pose: wrists pinned behind the back, head bowed.
// look = world-space point the eyes glance toward (e.g. the player's eye
// during an interview). Zero vector = eyes face forward. Only "normal" eyes
// track - black-eyed mutants keep their dead straight-ahead stare.
void DrawHumanoid(Vector3 feet, float yawDeg, const HumanoidApp& a, bool seated,
                  float animT, float armRaise = 0.f, float seatH = 0.5f,
                  float walkAmt = 0.f, float detain = 0.f, Vector3 look = {});
// eyes queue up during DrawHumanoid and render here - call after the lit
// pass (fx.LitOff) so sclera aren't crushed to grey by the office lighting
void DrawHumanoidEyes();

enum class VState { None, ToDoor, AtDoor, ToChair, Seated, Leaving, Gone };

struct Visitor {
    const json* person = nullptr;   // roster entry
    std::string anomaly;            // "" or type from anomalies.json visitor_anomalies
    VState state = VState::None;
    Vector3 pos{0, 0, 0};
    float yaw = 0;
    int wp = 0;
    float waitT = 0, seatT = 0, animT = 0;
    bool seated = false;
    float seatH = 0.5f;             // seat surface height while seated
    bool hostileStarted = false;
    bool hostileWarned = false;     // player has been told something changed
    float hostileT = 0;
    HumanoidApp app;
    bool lunge = false;             // currently lunging at player
    Vector3 lungeTarget{};
    Vector3 lungeFrom{};            // chair pos when the lunge started
    float lungeT = 0;
    bool escorted = false;          // security walked them out - a resolved exit
    bool detained = false;          // cuffed - crew drives pos/yaw, no own pathing
    bool detainWalk = false;        // crew is marching them out right now
    int slotIdx = -1;               // schedule slot this visitor came from
    float doorSeenT = 999.f;        // seconds since the office door was last seen open
    bool sawBlockedDoor = false;    // already reacted to the shelf blocking the door

    bool Active() const { return state != VState::None && state != VState::Gone; }
    std::string Name() const { return person ? JS(*person, "name") : std::string("?"); }
};

class VisitorSystem {
public:
    void Init(const json& visitorsJson, const json& visitorCfg);
    void Reset(bool freshNight = true);   // fresh=false on save loads: keep the book as saved

    // notify(text), knock(), lunge()
    void Update(float dt, float gameHour, Office& office,
                const std::function<void(const std::string&)>& notify,
                const std::function<void()>& onKnock,
                const std::function<void(Vector3)>& onLunge);

    void Draw() const;   // current visitor + waiting-room NPCs + exterior figures

    void InviteIn();
    void EndInterview();
    bool CallNextEarly(Office& office);     // true if the next visitor is now inbound
    std::string NextName() const;           // "" when the book is empty
    void SecurityRemove();
    void MarkHostileRevealed();
    bool BangerStrike();            // true on each bang beat (audio cue)
    void WakeWaitingRoom() { ambientTimer = 1.5f; }   // people drift back after a fix

    bool VisitorAtDoor() const { return cur.state == VState::AtDoor; }
    bool VisitorSeated() const { return cur.state == VState::Seated; }
    bool HasActiveVisitor() const { return cur.Active(); }
    const Visitor& Cur() const { return cur; }
    Visitor& CurMut() { return cur; }
    int ScheduleIndex() const { return scheduleIdx; }
    int ActiveSlot() const { return cur.Active() ? cur.slotIdx : -1; }
    int PendingCount() const;   // scheduled but not yet arrived
    int night = 1;              // set by Game - gates lethal schedule slots
    bool doorOpen = false;      // set by Game each frame - semantic door state
                                // (rotY is also watched so a desync can't lie)
    bool doorBlocked = false;   // bookshelf_door live: nobody passes, nobody opens
    const json& Schedule() const { return schedule; }
    const json* RosterFind(const std::string& id) const;
    const json& Roster() const { return data["roster"]; }
    // swap in an LLM-generated roster (JSON array text); rebuilds the night's
    // schedule from the template slots. Returns false if the text isn't usable
    // - caller keeps the stock roster. Only call between shifts.
    bool ApplyRosterJson(const std::string& text);

    // ambient NPCs in waiting room (decorative)
    struct Ambient {
        Vector3 pos{0, 0, 0}; float yaw = 0; HumanoidApp app; float t = 0;
        std::vector<Vector2> path; int wp = 0; bool walking = false;
        float seatH = 0.25f;
        int seatIdx = -1;            // claimed waiting-seat index, -1 = none
        float sitT = -1.f;           // <0 upright, [0,1) lowering into seat
        float seatYaw = 0.f;
        float standYaw = 0.f;        // facing when the sit-down glide started
        Vector3 standSpot{0, 0, 0};  // stand point in front of the seat
        Vector3 seatPos{0, 0, 0};    // final position parked on the cushion
        bool leaving = false;        // stood up and is exiting via ambient_exit
        bool standing = false;       // mid "stand up" glide back to the stand-spot
        float leaveT = 0.f;          // countdown until they decide to go (set when seated)
    };
    std::vector<Ambient> ambient;
    bool waitingEmpty = false;          // exterior anomaly: room cleared
    std::string exteriorFigure;         // "", "figure_at_window", "figure_at_door"
    Vector3 figurePos{0, 0, 0};
    bool figureActive = false;
    // anomaly: someone banging on the door/window from the waiting room
    bool bangerActive = false;
    std::string bangerAt;               // "door" | "window"
    Vector3 bangerPos{0, 0, 0};
    float bangT = 0;                    // arm swing phase
    bool ambientMutated = false;        // anomaly: waiting-room people go wrong
    Vector3 playerPos{0, 0, 0};         // mutated ambient NPCs stare at this

    // transcript of current interview
    std::vector<std::pair<std::string, std::string>> transcript; // speaker, line

    // save/load support
    json Serialize() const;
    void Deserialize(const json& j, Office& office);

    float doorWaitSec = 55, hostileRevealSec = 70, hostileGraceSec = 45, walkSpeed = 1.15f;
    float softLen = 150.f;      // interview_soft_length_sec - visitor wraps up and leaves
    int scheduleIdx = 0;
    // difficulty scaling: Game caps how much of the night's book actually
    // runs - early nights take a short list, later nights the full template
    void SetSlotCap(int n) { slotCap = n < 1 ? 1 : n; }
    int SlotCap() const { return slotCap; }
    // how many anomaly-tagged slots the book may carry tonight - early nights
    // get a thin spread so a new clerk isn't drowning in Security escorts
    void SetAnomalyCap(int n) { anomalyCap = n < 0 ? 0 : n; }
    // owned by Game - lore personas it already met lose the lottery first
    const std::set<std::string>* loreSeen = nullptr;

private:
    json data;
    json schedule;
    json schedTemplate;             // stock slot pattern (times + anomaly tags)
    json schedFull;                 // full working book BEFORE the night cap
    int slotCap = 999;
    int anomalyCap = 999;
    Visitor cur;
    float ambientTimer = 20.f;
    bool knocked = false;
    float lastBangPhase = 0.f;
    // stall watchdog: a walking visitor should always be making progress -
    // if a state/path edge ever wedges one, we log and force it through
    // rather than let the night's schedule stall on a frozen visitor
    float stallT = 0.f;
    Vector3 stallPos{0, 0, 0};
    int stallWp = -1;
    VState stallState = VState::None;
    void SpawnScheduled(Office& office);
    void InjectLore();              // rare: a lore persona takes a mundane slot
    void WalkAlong(Visitor& v, const std::vector<Vector2>& path, float dt, float speed);
};
