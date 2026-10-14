#pragma once
#include "common.h"
#include <map>

// furniture kind - resolved once at load/spawn from the JSON `type` string.
// Per-frame code switches on this instead of comparing strings.
enum class FT {
    Unknown,
    Desk, OfficeChair, VisitorChair, Monitor, Keyboard, Mouse, PcTower,
    Outlet, PaperStack, Rug, Mug, DeskLamp, Bookshelf, FilingCabinet, Printer,
    Plant, Vase, Poster, Phone, WallClock, Couch, Bench, WaterCooler,
    Blinds, Door, Bin, Switch, CeilingLight, Note
};
inline FT TypeOf(const std::string& s) {
    static const std::map<std::string, FT> m = {
        {"desk", FT::Desk}, {"office_chair", FT::OfficeChair},
        {"visitor_chair", FT::VisitorChair}, {"monitor", FT::Monitor},
        {"keyboard", FT::Keyboard}, {"mouse", FT::Mouse},
        {"pc_tower", FT::PcTower}, {"outlet", FT::Outlet},
        {"paper_stack", FT::PaperStack}, {"rug", FT::Rug}, {"mug", FT::Mug},
        {"desk_lamp", FT::DeskLamp}, {"bookshelf", FT::Bookshelf},
        {"filing_cabinet", FT::FilingCabinet}, {"printer", FT::Printer},
        {"plant", FT::Plant}, {"vase", FT::Vase}, {"poster", FT::Poster},
        {"phone", FT::Phone}, {"wall_clock", FT::WallClock},
        {"couch", FT::Couch}, {"bench", FT::Bench},
        {"water_cooler", FT::WaterCooler}, {"blinds", FT::Blinds},
        {"door", FT::Door}, {"bin", FT::Bin}, {"switch", FT::Switch},
        {"ceiling_light", FT::CeilingLight}, {"note", FT::Note},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : FT::Unknown;
}

struct Furni {
    std::string id, type;
    FT ft = FT::Unknown;            // cached enum of `type` - set on load/spawn
    Vector3 pos{0, 0, 0};       // bottom-center
    Vector3 size{0.5f, 0.5f, 0.5f};
    float rotY = 0.f;
    Color color = WHITE;
    bool solid = false;
    bool hidden = false;
    bool stopped = false;       // anomaly: wall clock frozen
    bool backwards = false;     // anomaly: wall clock hands run in reverse
    bool eyes = false;          // anomaly: grows eyes that track the player
    std::string art;            // poster art style: "" | landscape | portrait | abstract | notice | certificate | sign
    std::string interact;       // "sit" | "blinds" | "door" | "switch" | "sit_monitor" | "phone"
    std::string room = "office";
};

// soft fake-AO contact blob: flat radial-gradient quad just over the floor,
// near-black alpha - grounds furniture/humanoids. Safe inside the lit pass
// (texel rgb is 0 so lighting contributes nothing, it only darkens).
void DrawBlobShadow(Vector3 center, float rx, float rz, float alpha = 0.4f, float yawDeg = 0.f);

class Office {
public:
    void Load(const json& officeJson, const json& cfg);

    void Draw() const;
    void DrawEyes() const;    // googly-eye anomaly domes - UNLIT, call after LitOff
    void DrawCables() const;  // power/data cords strung between furniture (pc->monitor, pc->outlet)
    void DrawGlass() const;   // translucent window pane - must draw LAST (depth-write would occlude room)
    void Update(float dt);    // advances move-glides (anomaly furniture sliding into place)
    bool MonitorQuad(Vector3& center, float& w, float& h, float& z) const; // screen face rect
    Vector3 MonitorScreenCenter() const;
    bool MonitorUV(Vector3 ro, Vector3 rd, Vector2& uv) const;    // aim ray -> screen UV
    Vector3 WindowCenter() const { return {(windowX.x + windowX.y) / 2, (windowY.x + windowY.y) / 2, windowZ}; }
    // rebuilt lazily at most once per frame (tick bumps in Update) - callers
    // copy the returned ref when they need to append door/desk sub-boxes
    const std::vector<AABB>& Solids() const;
    Furni* Find(const std::string& id);
    const Furni* FindC(const std::string& id) const;

    // anomaly mutation: caller snapshots the Furni first if it wants to revert
    void ApplyMutation(const std::string& targetId, const json& mutation);
    // true when `it` stands on `host`'s top surface - carried by translates
    bool RestsOn(const Furni& it, const Furni& host) const;
    // same, but positional/rotational changes glide in over ~dur seconds with
    // an overshoot jolt (the "furniture scraped across the floor" read)
    void ApplyMutationAnimated(const std::string& targetId, const json& mutation, float dur = 0.6f);
    struct MoveGlide {
        std::string id;
        Vector3 fromP{0, 0, 0}, toP{0, 0, 0};
        float fromR = 0, toR = 0;
        float t = 0, dur = 0.6f;
        bool announced = false;         // Game plays the screech once
    };
    std::vector<MoveGlide> moveGlides;  // in-flight anomaly slides (Update advances them)
    void RemoveSpawned(const std::string& id);

    AABB bounds{}, waitingBounds{};
    Vector3 seatCam{}, lookMonitor{}, lookVisitor{}, visitorChairPos{};
    float doorZ = -5.f;
    Vector2 doorX{0, 0};
    Vector2 windowX{0, 0}, windowY{0, 0};
    float windowZ = -5.f;
    std::map<std::string, std::vector<Vector2>> paths;
    struct Seat { Vector3 pos; float yaw; float seatH = 0.25f; };
    std::vector<Seat> waitingSeats;
    Vector3 VisitorSitPos() const;   // chair pos nudged forward so the back clears
    std::map<std::string, Vector3> figurePositions;
    std::vector<Vector3> intruderSpawns;
    bool blindsOpen = false;
    bool waitingLightsOut = false;
    int windowView = 0;         // anomaly: 0 normal, 1 waiting room upside down, 2 void
    bool windowBroken = false;  // breach aftermath: hole in the glass until repaired
    bool hallLong = false;      // anomaly: corridor past the hall door stretches forever
    Vector3 eyeTarget{0, 1.6f, 3};  // things with eyes look here (player eye pos)
    float clockHour = 18.f;     // wall clocks show the game hour (set per-frame)
    float lampDim = 1.f;        // office light fixtures dim with the flicker level
    bool drawerOpen = false;    // anomaly: desk drawer pulled itself out
    float drawerT = 0.f;        // drawer slide-out amount 0..1 (animated by game.cpp)

    Color wallC{72, 70, 78, 255}, floorC{38, 34, 36, 255}, ceilC{30, 30, 34, 255},
          monitorGlow{150, 200, 235, 255};

    static void DrawShadowFigure(Vector3 feet, float yawDeg, float heightScale = 1.1f,
                                 float armRaise = 0.f);   // >0 = slamming arm (door banger)

    std::vector<Furni> items;
    uint64_t tick = 0;              // frame counter - bumped by Update()

private:
    void DrawRooms() const;
    void DrawItem(const Furni& f) const;
    mutable std::vector<AABB> solidsCache;
    mutable uint64_t solidsTick = ~0ull;
};
