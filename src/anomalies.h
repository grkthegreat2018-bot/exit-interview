#pragma once
#include "common.h"
#include "office.h"
#include "visitor.h"

struct ActiveAnomaly {
    std::string id;
    std::string channel;        // "office" | "exterior" | "visitor" | "intruder"
    std::string desc;
    std::vector<std::string> keywords;
    // revert data
    bool hadTarget = false;
    Furni snapshot;
    std::string spawnedId;
    std::string kind;           // exterior kind
    float t0 = 0;
    float instab = 1.f;         // drain multiplier: subtle <1, major >1
};

struct ResolveOutcome {
    bool ok = false;
    std::string id, kind, channel, targetId;
    Furni snapshot;
    bool hadTarget = false;
    std::string spawnedId;      // furniture the crew carries out
    std::vector<std::string> riderIds;  // items on the host's top, captured once
    Vector3 workPos{0, 0, 0};   // where the crew stands
    bool deferred = false;      // world effect stays until ApplyResolveFx
};

class AnomalyDirector {
public:
    void Init(const json& anomaliesJson, const json& instabilityCfg);
    void Reset();

    void Update(float dt, float gameHour, Office& office, VisitorSystem& vs,
                const std::function<void(const std::string&)>& notify,
                const std::function<void()>& onIntruderStrike);

    // attempt to resolve: returns true if a matching active anomaly was cleared
    bool TryResolve(const std::string& channel, const std::string& anomalyId, Office& office, VisitorSystem& vs);
    // two-phase resolve: BeginResolve clears bookkeeping + instability instantly;
    // with deferFx the world effect (mutation/lights/figure) stays until
    // ApplyResolveFx runs - used to let a crew physically come fix it
    ResolveOutcome BeginResolve(const std::string& channel, const std::string& anomalyId,
                                Office& office, VisitorSystem& vs, bool deferFx);
    void ApplyResolveFx(const ResolveOutcome& o, Office& office);
    // pick candidate anomalies for validator prompt/stub
    std::vector<std::pair<std::string, std::vector<std::string>>> Candidates(const std::string& channel) const;
    std::string ActiveDescriptions(const std::string& channel, const std::string& skipKindPrefix = "") const;
    int ActiveCount() const { return (int)active.size(); }
    bool Has(const std::string& channel) const;
    bool HasKind(const std::string& kind) const;        // any active anomaly of this kind
    bool IdLive(const std::string& id) const;           // id in active OR pending-crew
    std::string KindOf(const std::string& id) const;    // kind of an active anomaly ("" if none)
    std::vector<std::string> ActiveKeywords(const std::string& kindPrefix) const;
    void EnsureVisitor(const std::string& kind);        // seated visitor carries anomaly -> track it
    void ClearChannel(const std::string& channel);      // drop entries without applying effects
    void DropId(const std::string& id);                 // erase one active entry, no relief/fx
    void DropKind(const std::string& kind);             // erase all active entries of a kind
    void DebugForceOffice(const std::string& id, Office& office);   // EI_ dev hooks
    bool DebugForceKind(const std::string& kind, Office& office,
                        VisitorSystem* vs = nullptr);   // spawn def by kind; vs applies room flags
    void DebugCallEvent(const json& ev);                            // dev hook: ring the desk phone now

    // freeform (LLM) anomaly plumbing - off when the player picks scripted-only
    bool freeform = true;
    int night = 1;                  // 1-indexed shift count - scales difficulty
    bool remixPending = false;              // scheduler wants an LLM-remix anomaly
    json pendingCall;                       // a call_events entry waiting for the Game
    // lore collection the Game owns - the call pool prefers personas the
    // player hasn't met yet so the cast reveals itself across runs
    const std::set<std::string>* loreSeen = nullptr;
    void SetParts(const json& p) { parts = p; }
    // dev hot reload: swap the spawn catalog + tuning without touching live
    // anomalies (they keep their own snapshots/keywords)
    void ReloadDefs(const json& aj, const json& icfg, const json& p) {
        defs = aj; cfg = icfg; parts = p;
    }
    json TakeCallEvent();                   // returns pendingCall and clears it
    // LLM-authored call personas get folded into the call_events pool with
    // clamped effects - they then ring through the normal call channel
    int InjectCallEvents(const json& arr);
    std::string RemixPrompt() const;        // catalog description for the LLM
    bool ApplyRemixJson(const std::string& text, Office& office, std::string& why);
    void TriggerEffect(const json& fx, Office& office, VisitorSystem& vs);  // call outcome fx

    float Instability() const { return instab; }
    void AddInstability(float v);
    float FreezeT() const { return freezeT; }
    // filed paperwork holds the room still - no drain, no decay, for `sec`
    void FreezeInstability(float sec) { freezeT = std::max(freezeT, sec); }
    std::string TierName() const;
    bool IntruderActive() const { return intruderActive; }
    Vector3 IntruderPos() const { return intruderPos; }
    float IntruderTimeLeft() const { return intruderT; }
    void ClearIntruder(Office& office);

    const std::vector<ActiveAnomaly>& Active() const { return active; }

    // channel of the most recent spawn - Game steers the "something changed"
    // cue so a waiting-room anomaly doesn't send the player hunting the office
    const std::string& LastSpawnChannel() const { return lastSpawnChannel; }

    // set by Game: returns true when the player can currently see this point
    // (or is seated at the desk). Positional mutations skip watched targets -
    // furniture gliding across the room in plain sight reads as a bug
    std::function<bool(Vector3)> seenFn;

    // save/load
    json Serialize() const;
    void Deserialize(const json& j, Office& office, VisitorSystem& vs);

private:
    json defs, cfg, parts;
    float instab = 0;
    float freezeT = 0;              // instability paused (paperwork reward)
    float nextSpawnIn = 60.f;
    float elapsed = 0;
    bool intruderActive = false;
    float intruderT = 0;
    std::vector<ActiveAnomaly> pending;   // resolved, awaiting crew fix (flags persist)
    Vector3 intruderPos{0, 0, 0};
    std::vector<ActiveAnomaly> active;
    std::vector<std::string> exhausted;
    std::string lastSpawnChannel = "office";

    json PickTier() const;
    float NightScale(const char* key, float dflt) const;    // per-night array by (night-1)
    int NightUnlock(const char* key, int dflt) const;       // scalar gate
    bool TargetWatched(const json& d, Office& office) const;
    bool Undetectable(const json& d, Office& office,
                      const VisitorSystem* vs = nullptr) const; // ghost-anomaly guard
    void SpawnOffice(Office& office);
    void SpawnExterior(Office& office, VisitorSystem& vs);
    void SpawnVisitorCorruption(VisitorSystem& vs);
    void SpawnIntruder(Office& office);
    void ApplyExteriorFlags(Office& office, VisitorSystem& vs);
};
