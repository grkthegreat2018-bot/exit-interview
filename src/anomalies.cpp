#include "anomalies.h"

void AnomalyDirector::Init(const json& aj, const json& icfg) {
    defs = aj;
    cfg = icfg;
    Reset();
}
void AnomalyDirector::Reset() {
    instab = JF(cfg, "start", 12.f);
    freezeT = 0;
    nextSpawnIn = 55.f;
    elapsed = 0;
    intruderActive = false;
    active.clear();
    pending.clear();
    exhausted.clear();
}

std::string AnomalyDirector::TierName() const { return JS(PickTier(), "name", "calm"); }

json AnomalyDirector::PickTier() const {
    json best;
    for (auto& t : cfg["tiers"]) if (instab >= JF(t, "min", 0)) best = t;
    return best;
}

void AnomalyDirector::AddInstability(float v) {
    // night 0 is training - mistakes still sting, but the meter can never
    // reach the termination threshold
    float cap = night == 0 ? 85.f : JF(cfg, "max", 100.f);
    instab = std::clamp(instab + v, 0.f, cap);
}

// per-night difficulty scales: later nights drain faster and spawn sooner.
// `key` names an array in cfg["night_scaling"] indexed by (night-1).
float AnomalyDirector::NightScale(const char* key, float dflt) const {
    auto& ns = cfg["night_scaling"];
    if (!ns.contains(key) || !ns[key].is_array() || ns[key].empty()) return dflt;
    auto& arr = ns[key];
    int i = std::clamp(night - 1, 0, (int)arr.size() - 1);
    return arr[i].get<float>();
}
int AnomalyDirector::NightUnlock(const char* key, int dflt) const {
    auto& ns = cfg["night_scaling"];
    return ns.contains(key) ? (int)JF(ns, key, (float)dflt) : dflt;
}

void AnomalyDirector::Update(float dt, float gameHour, Office& office, VisitorSystem& vs,
                             const std::function<void(const std::string&)>& notify,
                             const std::function<void()>& onIntruderStrike) {
    elapsed += dt;
    auto nightScale = [&](const char* key, float dflt) { return NightScale(key, dflt); };
    auto nightUnlock = [&](const char* key, int dflt) { return NightUnlock(key, dflt); };
    if (freezeT > 0.f) {
        freezeT -= dt;   // filed paperwork: instability held, nothing moves
    } else {
        float tick = JF(cfg, "tick_per_active_anomaly_per_sec", 0.045f) * nightScale("instab_mul", 1.f);
        if (active.empty() && !intruderActive) {
            // clean room = the night recovers, slowly (true per-second rate)
            instab -= JF(cfg, "decay_per_sec_when_clean", 0.3f) * dt;
        } else {
            // each anomaly drains at its own rate - a moved mug ticks up far
            // slower than a figure at the window or a hostile visitor
            float rate = 0;
            for (auto& a : active) rate += a.instab;
            instab += tick * rate * dt * 60.f;
        }
        instab = std::clamp(instab, 0.f, night == 0 ? 85.f : JF(cfg, "max", 100.f));
    }

    // intruder countdown - a broken line of defence collapses the search
    // window: shattered glass or a door left ajar lets it home in far faster
    if (intruderActive) {
        float drain = 1.f;
        if (office.windowBroken) drain += 1.4f;
        if (const Furni* d = office.Find("door")) if (d->rotY < -25.f) drain += 1.4f;
        intruderT -= dt * drain;
        if (intruderT <= 0) {
            intruderActive = false;
            onIntruderStrike();
        }
    }

    // spawn scheduler - night 0 runs a scripted lesson plan instead: the
    // tutorial machine fires anomalies itself, nothing rolls here;
    // devCalm (EI_CALM / seq 'calm') parks the scheduler the same way
    if (night == 0 || devCalm) { ApplyExteriorFlags(office, vs); return; }
    nextSpawnIn -= dt;
    if (nextSpawnIn <= 0) {
        // hard cap on live anomalies: without it a bad streak at a hot tier
        // snowballs (more actives -> faster instab -> faster spawns -> dead
        // by math, not by play). Saturated rooms re-check soon instead of
        // taking a whole interval off.
        int cap = (int)nightScale("max_concurrent", 99.f);
        if ((int)active.size() >= cap) {
            nextSpawnIn = 14.f;
        } else {
        json tier = PickTier();
        auto iv = tier["spawn_interval_sec"];
        // later nights: anomalies arrive faster
        nextSpawnIn = RandRange(iv[0].get<float>(), iv[1].get<float>()) *
                      nightScale("spawn_interval_mul", 1.f);
        // weighted pick - channels unlock over nights so night 1 is a
        // tutorial: quiet office/exterior oddities only, nothing lethal
        std::map<std::string, float> w;
        for (auto& [k, v] : tier["weights"].items()) w[k] = v.get<float>();
        if (night < nightUnlock("unlock_visitor_night", 2)) w.erase("visitor");
        if (night < nightUnlock("unlock_call_night", 2)) w.erase("call");
        if (night < nightUnlock("unlock_remix_night", 3)) w.erase("remix");
        if (night < nightUnlock("unlock_intruder_night", 3)) w.erase("intruder");
        float roll = Rand01(), acc = 0;
        std::string pick = "office";
        float total = 0; for (auto& [k, v] : w) total += v;
        for (auto& [k, v] : w) {
            acc += v / std::max(0.001f, total);
            if (roll <= acc) { pick = k; break; }
        }
        if (pick == "office") SpawnOffice(office);
        else if (pick == "exterior") SpawnExterior(office, vs);
        else if (pick == "visitor") {
            if (vs.VisitorSeated() && vs.Cur().anomaly.empty()) SpawnVisitorCorruption(vs);
            else SpawnOffice(office);   // empty chair: the roll falls back to the room
        }
        else if (pick == "intruder") {
            if (!intruderActive && !office.intruderSpawns.empty()) SpawnIntruder(office);
            else SpawnOffice(office);
        }
        else if (pick == "call") {
            // the desk phone rings - persona picked now, Game handles the call.
            // Stock call_events are scripted content - they ring even under
            // ANOM: SCRIPTED; only LLM-authored callgen_* personas stay gated.
            if (pendingCall.is_null() && defs.contains("call_events") &&
                !defs["call_events"].empty() && !HasKind("phone_call")) {
                std::vector<const json*> pool, fresh;
                for (auto& e : defs["call_events"]) {
                    if (JS(e, "id").rfind("callgen_", 0) == 0 && !freeform) continue;
                    pool.push_back(&e);
                    if (loreSeen && !loreSeen->count(JS(e, "id"))) fresh.push_back(&e);
                }
                // unseen callers first - the phone book reveals itself over runs
                auto& pl = fresh.empty() ? pool : fresh;
                if (!pl.empty()) DebugCallEvent(*pl[RandInt(0, (int)pl.size() - 1)]);
                else SpawnOffice(office);
            }
            else SpawnOffice(office);
        }
        else if (pick == "remix") {
            if (freeform && !remixPending) remixPending = true;   // Game submits the LLM job
            else SpawnOffice(office);
        }
        }
    }

    // exterior flags -> visitor system each frame (cheap, keeps state coherent)
    ApplyExteriorFlags(office, vs);
    (void)gameHour; (void)notify;
}

// flag-driven kinds reveal themselves through the ROOM, not the furni they
// may also carry - the corridor stretches, the lights die, something knocks.
// A no-op furni mutation can't hide them, so they skip the visibility check.
static const std::set<std::string> kFlagKinds = {
    "figure_at_window", "figure_at_door", "lights_out", "lights_office",
    "empty_room", "window_flip", "window_void", "hall_long", "banging_door",
    "banging_window", "mutated_waiting", "broken_window", "hall_writing",
    "phone_call", "note_slid", "spin_chair", "desk_drawer", "intruder"};

// would the mutation actually change what the target LOOKS like? pos_set to
// the same spot, rot_set to the same angle, a tint it already wears - any of
// those spawns a ghost anomaly the player can never find, only guess
static bool MutationVisible(const json& m, const Furni& s) {
    for (auto& [k, v] : m.items()) {
        if (k == "translate") {
            Vector3 d = JV3(v);
            if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-3f) return true;
        } else if (k == "pos_set") {
            if (Vector3Distance(JV3(v), s.pos) > 0.02f) return true;
        } else if (k == "rot_set") {
            if (std::fabs(v.get<float>() - s.rotY) > 1.f) return true;
        } else if (k == "scale") {
            Vector3 sc = JV3(v, {1.f, 1.f, 1.f});   // a multiplier, not a size
            if (std::fabs(sc.x - 1.f) + std::fabs(sc.y - 1.f) + std::fabs(sc.z - 1.f) > 0.02f) return true;
        } else if (k == "color") {
            if (v.is_array() && v.size() >= 3 &&
                (v[0].get<int>() != s.color.r || v[1].get<int>() != s.color.g ||
                 v[2].get<int>() != s.color.b)) return true;
        } else if (k == "hidden") {
            if (v.get<bool>() != s.hidden) return true;
        } else if (k == "eyes") {
            if (v.get<bool>() != s.eyes) return true;
        } else if (k == "stopped") {
            if (v.get<bool>() != s.stopped) return true;
        } else if (k == "backwards") {
            if (v.get<bool>() != s.backwards) return true;
        } else if (k == "fly") {
            if (v.get<bool>() != s.fly) return true;
        } else if (k == "art") {
            if (v.is_string() && v.get<std::string>() != s.art) return true;
        } else {
            return true;   // spawn / unknown keys always change the room
        }
    }
    return false;
}

// a mutation that changes where something IS (pos/rot) must never play out in
// the player's sightline or while they sit at the desk - furniture sliding
// around under a stare reads as a glitch, not a haunting
static bool MutMoves(const json& d) {
    if (!d.contains("mutation") || d["mutation"].contains("spawn")) return false;
    const json& m = d["mutation"];
    return m.contains("translate") || m.contains("pos_set") || m.contains("rot_set");
}

bool AnomalyDirector::TargetWatched(const json& d, Office& office) const {
    if (!seenFn || !MutMoves(d)) return false;
    std::string tgt = JS(d, "target");
    const Furni* f = tgt.empty() ? nullptr : office.FindC(tgt);
    return f && seenFn({f->pos.x, f->pos.y + f->size.y * 0.5f, f->pos.z});
}

// an anomaly the player can never find is worse than none: it drains the room
// and can only be brute-guessed. A def is unspawnable when its target already
// carries a live anomaly (the second change hides inside the first - a dark
// tint on an already-hidden poster, "stopped" on a frozen clock), when the
// target is missing/hidden, or when the mutation is a no-op against the
// target's current state.
bool AnomalyDirector::Undetectable(const json& d, Office& office,
                                     const VisitorSystem* vs) const {
    std::string tgt = JS(d, "target");
    const Furni* f = tgt.empty() ? nullptr : office.FindC(tgt);
    const json& mut = d.value("mutation", json::object());
    const json* spw = mut.contains("spawn") ? &mut["spawn"] : nullptr;
    std::string kind = JS(d, "kind");
    bool flaggy = kFlagKinds.count(kind) > 0;
    // one live anomaly per target / per spawned prop - a second change hides
    // inside the first (a dark tint on an already-hidden poster, "stopped"
    // on a frozen clock)
    for (auto* l : {&active, &pending}) for (auto& a : *l) {
        if (!tgt.empty() && a.hadTarget && a.snapshot.id == tgt) return true;
        if (spw && !a.spawnedId.empty() && a.spawnedId == JS(*spw, "id")) return true;
    }
    if (f && f->hidden) return true;
    // state-dependent kinds: nothing to mutate/evacuate/darken means nothing
    // to see. The waiting room starts empty and refills slowly, so an early
    // empty_room or mutated_waiting roll would otherwise drain in silence;
    // lights_office while the player already killed the lights is the same.
    if (vs) {
        if (kind == "empty_room" && (vs->ambient.empty() || vs->waitingEmpty)) return true;
        if (kind == "mutated_waiting") {
            bool anyStill = false;
            for (auto& a : vs->ambient) if (!a.walking && !a.leaving) anyStill = true;
            if (!anyStill || vs->waitingEmpty) return true;
        }
    }
    if (kind == "lights_office" && office.lightsManuallyOff) return true;
    // room flags are booleans: a live same-kind entry already owns the door
    // figure / banging / lights-out state, so a second pure-flag def adds
    // nothing. Combo defs (flag + furni delta) still count through their
    // mutation half.
    bool kindLive = false;
    if (flaggy)
        for (auto* l : {&active, &pending}) for (auto& a : *l)
            if (a.kind == kind) kindLive = true;
    bool furniDelta = spw || (f && MutationVisible(mut, *f));
    if (flaggy) return kindLive && !furniDelta;
    return !furniDelta;
}

void AnomalyDirector::SpawnOffice(Office& office) {
    // severity ceiling per night: night 1 learns the loop on quiet oddities,
    // the loud/violent kinds join later (they also drain harder, so an early
    // heavy spawn is a difficulty spike AND a fairness problem)
    float sevCap = NightScale("max_instab_per_spawn", 99.f);
    // weighted pool: authored defs (w default 1) + generic defs (effects are
    // rolled per spawn - the catalog doesn't need one def per object)
    std::vector<const json*> pool; std::vector<float> wts; float tot = 0;
    auto push = [&](const json& d, float w) {
        pool.push_back(&d); wts.push_back(w); tot += w;
    };
    for (auto& d : defs["office"]) {
        std::string id = JS(d, "id");
        bool used = std::find(exhausted.begin(), exhausted.end(), id) != exhausted.end();
        bool live = false;
        for (auto& a : active) if (a.id == id) live = true;
        if (!used && !live && !TargetWatched(d, office) && !Undetectable(d, office) &&
            JF(d, "instab", 1.f) <= sevCap)
            push(d, JF(d, "w", 1.f));
    }
    if (defs.contains("generic"))
        for (auto& d : defs["generic"]) {
            std::string id = JS(d, "id");
            bool live = false;
            for (auto& a : active) if (a.id == id) live = true;
            // generics re-roll per spawn so "used" doesn't exhaust them - a
            // slid mug and a floated chair share the def but not the moment
            if (!live && night >= JI(d, "min_night", 1) &&
                JF(d, "instab", 1.f) <= sevCap)
                push(d, JF(d, "w", 1.f) * 0.7f);
        }
    if (pool.empty()) return;
    float roll = Rand01() * tot; int pi = 0;
    for (int i = 0; i < (int)pool.size(); i++) { roll -= wts[i]; if (roll <= 0) { pi = i; break; } }
    const json& d = *pool[pi];
    if (d.contains("effects")) {
        if (!SpawnGeneric(d, office))
            nextSpawnIn = std::min(nextSpawnIn, 5.f);   // no valid target - retry soon
        return;
    }
    ActiveAnomaly a;
    a.id = JS(d, "id");
    a.channel = "office";
    lastSpawnChannel = "office";
    a.kind = JS(d, "kind");
    a.desc = JS(d, "desc");
    for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
    a.t0 = elapsed;
    a.instab = JF(d, "instab", 1.f);
    std::string tgt = JS(d, "target");
    if (!tgt.empty()) {
        Furni* f = office.Find(tgt);
        if (f) { a.hadTarget = true; a.snapshot = *f; }
    }
    office.ApplyMutationAnimated(tgt, d["mutation"]);   // moves scrape into place
    if (d["mutation"].contains("spawn"))
        a.spawnedId = JS(d["mutation"]["spawn"], "id");
    active.push_back(a);
    TraceLog(LOG_INFO, "[anomaly] spawned %s kind=%s channel=office target=%s",
             a.id.c_str(), a.kind.empty() ? "-" : a.kind.c_str(), tgt.empty() ? "-" : tgt.c_str());
}

// ---------- generic mutations: nearly anything in the room can misbehave ----------
// A "generic" def carries an "effects" whitelist instead of a fixed
// target+mutation; each spawn rolls a random valid target and builds the
// mutation on the fly. All moves are collision-aware (AABB vs the room's
// furniture + solids), all deltas are big enough to actually read, and
// positional changes never play out while the player is looking at them.

namespace {
// room plumbing never moves: walls, door, window gear, lights, the desk
// itself, the chair the visitor occupies, and the PC tower (hide pocket
// stays honest). Notes are spawned props - they don't re-anomalize.
bool GenOk(const Furni& f) {
    if (f.hidden) return false;
    switch (f.ft) {
        case FT::Desk: case FT::PcTower: case FT::Poster: case FT::WallClock:
        case FT::Blinds: case FT::Outlet: case FT::Switch: case FT::Door:
        case FT::CeilingLight: case FT::Note: case FT::Monitor:
        case FT::VisitorChair: case FT::Rug:
            return false;
        default: return true;
    }
}
bool GenRotOk(FT t) {   // a rotation must be VISIBLE - symmetric props can't "turn"
    switch (t) {
        case FT::OfficeChair: case FT::FilingCabinet: case FT::Bookshelf:
        case FT::Printer: case FT::Couch: case FT::Bench: case FT::Plant:
        case FT::WaterCooler: case FT::DeskLamp: case FT::Vase: case FT::Bin:
            return true;
        default: return false;
    }
}
AABB GenBox(Vector3 pos, Vector3 size, float rotY) {
    float c = std::cos(rotY * DEG2RAD), s = std::sin(rotY * DEG2RAD);
    float hx = (std::fabs(size.x * c) + std::fabs(size.z * s)) * 0.5f;
    float hz = (std::fabs(size.x * s) + std::fabs(size.z * c)) * 0.5f;
    return {{pos.x - hx, pos.y, pos.z - hz}, {pos.x + hx, pos.y + size.y, pos.z + hz}};
}
bool BoxesHit(const AABB& a, const AABB& b, float pad) {
    return a.mn.x < b.mx.x + pad && a.mx.x > b.mn.x - pad &&
           a.mn.y < b.mx.y + pad && a.mx.y > b.mn.y - pad &&
           a.mn.z < b.mx.z + pad && a.mx.z > b.mn.z - pad;
}
}  // namespace

bool AnomalyDirector::SpawnGeneric(const json& def, Office& office) {
    // clean targets only: nothing already anomalous, no spawned props
    auto clean = [&](const Furni& f) {
        for (auto* l : {&active, &pending}) for (auto& a : *l) {
            if (a.hadTarget && a.snapshot.id == f.id) return false;
            if (a.hadTarget2 && a.snapshot2.id == f.id) return false;
            if (!a.spawnedId.empty() && a.spawnedId == f.id) return false;
            for (auto& s : a.extraSnaps) if (s.id == f.id) return false;
        }
        return true;
    };
    auto watched = [&](const Furni& f) {
        return seenFn && seenFn({f.pos.x, f.pos.y + f.size.y * 0.5f, f.pos.z});
    };
    // a spot is free when its box hits nothing else and stays inside the room
    auto freeAt = [&](const Furni& f, Vector3 p, float rotY, float pad) {
        AABB box = GenBox(p, f.size, rotY);
        const AABB& rb = (f.room == "waiting") ? office.waitingBounds : office.bounds;
        if (box.mn.x < rb.mn.x + pad || box.mx.x > rb.mx.x - pad ||
            box.mn.z < rb.mn.z + pad || box.mx.z > rb.mx.z - pad) return false;
        for (auto& o : office.items) {
            if (o.id == f.id || o.hidden) continue;
            if (o.room != f.room) continue;
            if (BoxesHit(box, GenBox(o.pos, o.size, o.rotY), pad)) return false;
        }
        return true;
    };

    std::vector<const Furni*> flr, top;
    auto hasRiders = [&](const Furni& f) {
        for (auto& r : office.items) if (office.RestsOn(r, f)) return true;
        return false;
    };
    for (auto& f : office.items)
        if (f.room == "office" && GenOk(f) && clean(f))
            (f.pos.y > 0.3f ? top : flr).push_back(&f);
    if (flr.empty() && top.empty()) return false;

    std::vector<std::string> effs;
    for (auto& e : def["effects"]) effs.push_back(e.get<std::string>());
    for (int i = (int)effs.size() - 1; i > 0; i--)
        std::swap(effs[i], effs[RandInt(0, i)]);

    ActiveAnomaly a;
    a.id = JS(def, "id");
    a.channel = "office"; lastSpawnChannel = "office";
    a.kind = JS(def, "kind");
    a.t0 = elapsed;
    a.instab = JF(def, "instab", 1.f);
    for (auto& k : def["keywords"]) a.keywords.push_back(k.get<std::string>());

    std::string done;
    auto apply = [&](const std::string& tid, const json& m, float dur = 0.6f) {
        office.ApplyMutationAnimated(tid, m, dur);
        a.genMuts.push_back({{"id", tid}, {"mut", m}});   // for save/load re-apply
    };
    for (auto& ef : effs) {
        if (ef == "slide") {
            // floor items relocate across the room; desk-top items slide to a
            // new spot ON their host surface - a mug can't end up mid-air
            std::vector<const Furni*> cand = flr;
            for (auto* f : top) cand.push_back(f);
            for (int tries = 0; tries < 14 && done.empty(); tries++) {
                const Furni* f = cand[RandInt(0, (int)cand.size() - 1)];
                if (watched(*f)) continue;
                if (f->pos.y > 0.3f) {
                    // top slide: stay on the host's surface
                    const Furni* host = nullptr;
                    for (auto& h : office.items) if (office.RestsOn(*f, h)) { host = &h; break; }
                    if (!host) continue;
                    for (int k = 0; k < 10 && done.empty(); k++) {
                        float mx = (host->size.x - f->size.x) * 0.5f - 0.05f;
                        float mz = (host->size.z - f->size.z) * 0.5f - 0.05f;
                        if (mx <= 0.02f || mz <= 0.02f) break;
                        Vector3 np{host->pos.x + RandRange(-mx, mx), f->pos.y,
                                   host->pos.z + RandRange(-mz, mz)};
                        if (Vector3Distance({np.x, 0, np.z}, {f->pos.x, 0, f->pos.z}) < 0.45f) continue;
                        bool hit = false;
                        for (auto& o : office.items)
                            if (o.id != f->id && office.RestsOn(o, *host) &&
                                BoxesHit(GenBox(np, f->size, f->rotY),
                                         GenBox(o.pos, o.size, o.rotY), 0.02f)) hit = true;
                        if (hit) continue;
                        a.snapshot = *f; a.hadTarget = true;
                        json m; m["pos_set"] = {np.x, np.y, np.z};
                        apply(f->id, m);
                        done = f->id;
                    }
                } else {
                    // floor slide: somewhere else in the room, >=0.8m away
                    const AABB& rb = office.bounds;
                    for (int k = 0; k < 12 && done.empty(); k++) {
                        Vector3 np{RandRange(rb.mn.x + f->size.x * 0.6f, rb.mx.x - f->size.x * 0.6f),
                                   0.f,
                                   RandRange(rb.mn.z + f->size.z * 0.6f, rb.mx.z - f->size.z * 0.6f)};
                        if (Vector3Distance({np.x, 0, np.z}, {f->pos.x, 0, f->pos.z}) < 0.8f) continue;
                        if (!freeAt(*f, np, f->rotY, 0.10f)) continue;
                        a.snapshot = *f; a.hadTarget = true;
                        json m; m["pos_set"] = {np.x, np.y, np.z};
                        apply(f->id, m);
                        done = f->id;
                    }
                }
            }
        } else if (ef == "float") {
            // a floor piece rises and hangs - pos.y lifts it off its support,
            // the per-frame `floating` flag gives it the wobble
            for (int tries = 0; tries < 14 && done.empty(); tries++) {
                const Furni* f = flr.empty() ? nullptr : flr[RandInt(0, (int)flr.size() - 1)];
                if (!f || watched(*f)) continue;
                float lift = RandRange(0.75f, 1.7f);
                Vector3 np{f->pos.x, f->pos.y + lift, f->pos.z};
                if (np.y + f->size.y > 2.75f) continue;              // ceiling clearance
                if (!freeAt(*f, np, f->rotY, 0.10f)) continue;
                a.snapshot = *f; a.hadTarget = true;
                json m; m["pos_set"] = {np.x, np.y, np.z};
                apply(f->id, m, 1.4f);        // slow rise
                done = f->id;
            }
        } else if (ef == "turn") {
            for (int tries = 0; tries < 14 && done.empty(); tries++) {
                const Furni* f = flr.empty() ? nullptr : flr[RandInt(0, (int)flr.size() - 1)];
                if (!f || !GenRotOk(f->ft) || watched(*f)) continue;
                float nr = f->rotY + (Rand01() < 0.5f ? -1.f : 1.f) * RandRange(60.f, 240.f);
                if (!freeAt(*f, f->pos, nr, 0.04f)) continue;
                a.snapshot = *f; a.hadTarget = true;
                json m; m["rot_set"] = nr;
                apply(f->id, m);
                done = f->id;
            }
        } else if (ef == "swap") {
            // two floor pieces trade places - sizes must each fit the other's spot
            for (int tries = 0; tries < 16 && done.empty(); tries++) {
                const Furni* f1 = flr[RandInt(0, (int)flr.size() - 1)];
                const Furni* f2 = flr[RandInt(0, (int)flr.size() - 1)];
                if (f1 == f2 || watched(*f1) || watched(*f2)) continue;
                if (Vector3Distance(f1->pos, f2->pos) < 1.4f) continue;
                // swap must fit both ways
                AABB b1 = GenBox(f2->pos, f1->size, f1->rotY);
                AABB b2 = GenBox(f1->pos, f2->size, f2->rotY);
                bool ok = true;
                for (auto& o : office.items) {
                    if (o.id == f1->id || o.id == f2->id || o.hidden || o.room != "office") continue;
                    AABB ob = GenBox(o.pos, o.size, o.rotY);
                    if (BoxesHit(b1, ob, 0.08f) || BoxesHit(b2, ob, 0.08f)) ok = false;
                }
                if (!ok) continue;
                a.snapshot = *f1; a.hadTarget = true;
                a.snapshot2 = *f2; a.hadTarget2 = true;
                json m1; m1["pos_set"] = {f2->pos.x, f2->pos.y, f2->pos.z};
                apply(f1->id, m1, 0.9f);
                json m2; m2["pos_set"] = {f1->pos.x, f1->pos.y, f1->pos.z};
                apply(f2->id, m2, 0.9f);
                done = f1->id + "+" + f2->id;
            }
        } else if (ef == "scale") {
            for (int tries = 0; tries < 14 && done.empty(); tries++) {
                const Furni* f = flr.empty() ? nullptr : flr[RandInt(0, (int)flr.size() - 1)];
                if (!f) continue;
                // small/mid props only - a 2m bookshelf doubling breaks the room
                if (f->size.x > 0.9f || f->size.y > 1.1f || f->size.z > 0.9f) continue;
                float s = Rand01() < 0.5f ? RandRange(0.5f, 0.68f) : RandRange(1.45f, 1.8f);
                Vector3 ns{f->size.x * s, f->size.y * s, f->size.z * s};
                if (s > 1.f) {   // growth must not clip through neighbours
                    Furni probe = *f; probe.size = ns;
                    if (!freeAt(probe, f->pos, f->rotY, 0.03f)) continue;
                }
                a.snapshot = *f; a.hadTarget = true;
                json m; m["scale"] = {s, s, s};
                apply(f->id, m);
                done = f->id;
            }
        } else if (ef == "tint") {
            for (int tries = 0; tries < 14 && done.empty(); tries++) {
                const Furni* f = flr.empty() ? nullptr : flr[RandInt(0, (int)flr.size() - 1)];
                if (!f) continue;
                // bled out: cold grey-rot wash, obviously not the same colour
                Color nc{(unsigned char)(f->color.r * 0.42f + 30),
                         (unsigned char)(f->color.g * 0.42f + 26),
                         (unsigned char)(f->color.b * 0.42f + 34), 255};
                if (std::abs((int)nc.r - f->color.r) < 25 &&
                    std::abs((int)nc.g - f->color.g) < 25 &&
                    std::abs((int)nc.b - f->color.b) < 25)
                    nc = Color{34, 30, 40, 255};   // already-dark item -> near-black wrong
                a.snapshot = *f; a.hadTarget = true;
                json m; m["color"] = {nc.r, nc.g, nc.b, nc.a};
                apply(f->id, m);
                done = f->id;
            }
        } else if (ef == "rearrange") {
            // set-piece: three floor pieces rotate positions and the room
            // comes back wrong. each landing must be free once the other two
            // movers are ignored (they're all in transit simultaneously)
            std::vector<const Furni*> trio;
            for (int tries = 0; tries < 30 && (int)trio.size() < 3; tries++) {
                const Furni* f = flr.empty() ? nullptr : flr[RandInt(0, (int)flr.size() - 1)];
                if (!f || watched(*f) || hasRiders(*f)) continue;   // riders would be left mid-air
                bool dup = false;
                for (auto* t : trio) if (t == f) dup = true;
                if (!dup) trio.push_back(f);
            }
            if ((int)trio.size() < 3) break;
            bool ok = true;
            for (int i = 0; i < 3 && ok; i++) {
                AABB land = GenBox(trio[(i + 1) % 3]->pos, trio[i]->size, trio[i]->rotY);
                for (auto& o : office.items) {
                    if (o.hidden || o.room != "office") continue;
                    bool participant = false;
                    for (auto* t : trio) if (o.id == t->id) participant = true;
                    if (participant) continue;
                    if (BoxesHit(land, GenBox(o.pos, o.size, o.rotY), 0.08f)) { ok = false; break; }
                }
            }
            if (!ok) break;
            for (int i = 0; i < 3; i++) {
                a.extraSnaps.push_back(*trio[i]);
                a.keywords.push_back(trio[i]->id);
            }
            for (int i = 0; i < 3; i++) {
                Vector3 dest = trio[(i + 1) % 3]->pos;
                json m; m["pos_set"] = {dest.x, dest.y, dest.z};
                m["rot_set"] = trio[i]->rotY + RandRange(-60.f, 60.f);
                apply(trio[i]->id, m, 1.1f);
            }
            done = "room";
        } else if (ef == "gravity") {
            // set-piece: up to four smallish props rise and hang mid-air.
            // floor pieces lift straight up; desk-top props lift off their
            // host - everything gets the `floating` wobble once airborne
            std::vector<const Furni*> lifts;
            for (int tries = 0; tries < 40 && (int)lifts.size() < 4; tries++) {
                bool pickTop = !top.empty() && (flr.empty() || Rand01() < 0.4f);
                auto& src = pickTop ? top : flr;
                if (src.empty()) continue;
                const Furni* f = src[RandInt(0, (int)src.size() - 1)];
                if (f->size.x > 1.3f || watched(*f) || hasRiders(*f)) continue;
                bool dup = false;
                for (auto* t : lifts) if (t == f) dup = true;
                if (dup) continue;
                float lift = RandRange(0.9f, 1.9f);
                Vector3 np{f->pos.x, f->pos.y + lift, f->pos.z};
                if (np.y + f->size.y > 2.75f) continue;
                if (!freeAt(*f, np, f->rotY, 0.10f)) continue;
                lifts.push_back(f);
                a.extraSnaps.push_back(*f);
                a.keywords.push_back(f->id);
                json m; m["pos_set"] = {np.x, np.y, np.z};
                apply(f->id, m, 1.6f);   // slow wrong rise
            }
            if (lifts.empty()) break;
            done = "airborne";
        } else if (ef == "spawn") {
            // a new prop the room didn't have: note on the floor or a small
            // vase/cup - always somewhere free, always findable
            static const char* kSpawnTypes[] = {"note", "vase", "mug", "bin"};
            for (int tries = 0; tries < 14 && done.empty(); tries++) {
                const char* ty = kSpawnTypes[RandInt(0, 3)];
                Furni probe;
                probe.ft = TypeOf(ty); probe.room = "office";
                probe.size = (std::string(ty) == "note") ? Vector3{0.22f, 0.01f, 0.3f}
                           : (std::string(ty) == "vase") ? Vector3{0.18f, 0.42f, 0.18f}
                           : (std::string(ty) == "mug")  ? Vector3{0.11f, 0.12f, 0.11f}
                           :                              Vector3{0.32f, 0.42f, 0.32f};
                const AABB& rb = office.bounds;
                Vector3 np{RandRange(rb.mn.x + 0.4f, rb.mx.x - 0.4f), 0.f,
                           RandRange(rb.mn.z + 0.4f, rb.mx.z - 0.4f)};
                if (!freeAt(probe, np, 0.f, 0.12f)) continue;
                std::string nid = "gen_" + std::to_string(RandInt(0, 99999));
                json m; m["spawn"] = {{"id", nid}, {"type", ty},
                    {"pos", {np.x, np.y, np.z}},
                    {"size", {probe.size.x, probe.size.y, probe.size.z}},
                    {"rot", RandRange(0.f, 360.f)},
                    {"color", {150, 148, 150}}, {"room", "office"}};
                if (std::string(ty) == "note") m["spawn"]["interact"] = "note";
                office.ApplyMutationAnimated("", m);
                a.genSpawn = m;            // save/load respawns the same prop
                a.spawnedId = nid;
                done = nid;
            }
        }
    }
    if (done.empty()) return false;

    // reports should name the thing that changed - splice the actual target
    // id/type into the keyword net so "the chair moved" matches, not just "moved"
    a.desc = JS(def, "desc");
    if (a.hadTarget) {
        a.keywords.push_back(a.snapshot.id);
        a.keywords.push_back(a.snapshot.type);
        a.desc += " (" + a.snapshot.type + ")";
    }
    if (a.hadTarget2) a.keywords.push_back(a.snapshot2.id);
    for (auto& s : a.extraSnaps) a.keywords.push_back(s.type);
    active.push_back(a);
    TraceLog(LOG_INFO, "[anomaly] spawned %s kind=%s channel=office target=%s (generic)",
             a.id.c_str(), a.kind.empty() ? "-" : a.kind.c_str(), done.c_str());
    return true;
}

void AnomalyDirector::SpawnExterior(Office& office, VisitorSystem& vs) {
    float sevCap = NightScale("max_instab_per_spawn", 99.f);
    std::vector<const json*> pool;
    for (auto& d : defs["exterior"]) {
        if (d.value("breach_only", false)) continue;   // only spawned by a breach
        std::string id = JS(d, "id");
        bool used = std::find(exhausted.begin(), exhausted.end(), id) != exhausted.end();
        bool live = false;
        for (auto& a : active) if (a.id == id) live = true;
        if (!used && !live && !TargetWatched(d, office) && !Undetectable(d, office, &vs) &&
            JF(d, "instab", 1.f) <= sevCap)
            pool.push_back(&d);
    }
    if (pool.empty()) { SpawnOffice(office); return; }
    const json& d = *pool[RandInt(0, (int)pool.size() - 1)];
    ActiveAnomaly a;
    a.id = JS(d, "id");
    a.channel = "exterior";
    lastSpawnChannel = "exterior";
    a.kind = JS(d, "kind");
    a.desc = JS(d, "desc");
    for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
    a.t0 = elapsed;
    a.instab = JF(d, "instab", 1.f);
    // waiting-room furniture can mutate just like office furniture (benches
    // sliding, clocks stopping) - the crew channel for these is Facilities
    std::string tgt = JS(d, "target");
    if (!tgt.empty())
        if (Furni* f = office.Find(tgt)) { a.hadTarget = true; a.snapshot = *f; }
    if (d.contains("mutation")) {
        office.ApplyMutationAnimated(tgt, d["mutation"]);
        if (d["mutation"].contains("spawn")) a.spawnedId = JS(d["mutation"]["spawn"], "id");
    }
    active.push_back(a);
    TraceLog(LOG_INFO, "[anomaly] spawned %s kind=%s channel=exterior target=%s",
             a.id.c_str(), a.kind.c_str(), tgt.empty() ? "-" : tgt.c_str());
    ApplyExteriorFlags(office, vs);
}

void AnomalyDirector::SpawnVisitorCorruption(VisitorSystem& vs) {
    if (!vs.VisitorSeated() || !vs.Cur().anomaly.empty()) return;
    // the deeper the night runs, the worse a "normal" appointment can get:
    // soft deceptions early, the impossible once things go wrong, and on deep
    // hostile nights the person in the chair can stop being one entirely.
    // Soft kinds are data-driven - any visitor_anomalies entry without a
    // lethal gate joins the pool automatically
    std::vector<std::string> kinds;
    for (auto& [k, v] : defs["visitor_anomalies"].items())
        if (k != "impossible_visitor" && k != "hostile") kinds.push_back(k);
    if (instab >= 58.f) kinds.push_back("impossible_visitor");
    if (instab >= 82.f && night >= 3) kinds.push_back("hostile");
    if (kinds.empty()) return;
    std::string k = kinds[RandInt(0, (int)kinds.size() - 1)];
    vs.CurMut().anomaly = k;
    // a corrupted hostile doesn't wait to be discovered - the grace timer
    // starts now, so a slow clerk meets the lunge mid-interview
    if (k == "hostile") vs.MarkHostileRevealed();
    ActiveAnomaly a;
    a.id = "v_" + k;
    a.channel = "visitor";
    a.kind = k;
    json d = defs["visitor_anomalies"][k];
    a.desc = JS(d, "desc");
    for (auto& kw : d["keywords"]) a.keywords.push_back(kw.get<std::string>());
    a.t0 = elapsed;
    a.instab = JF(d, "instab", 1.f);
    active.push_back(a);
    lastSpawnChannel = "visitor";
    TraceLog(LOG_INFO, "[anomaly] spawned %s kind=%s channel=visitor target=visitor",
             a.id.c_str(), k.c_str());
}

void AnomalyDirector::SpawnIntruder(Office& office) {
    if (intruderActive || office.intruderSpawns.empty()) return;
    intruderActive = true;
    // config instability.intruder_grace_sec wins; anomalies.json is the fallback
    intruderT = JF(cfg, "intruder_grace_sec", JF(defs["intruder"], "grace_sec", 22.f));
    intruderPos = office.intruderSpawns[RandInt(0, (int)office.intruderSpawns.size() - 1)];
    ActiveAnomaly a;
    a.id = "intruder";
    a.channel = "intruder";
    a.desc = JS(defs["intruder"], "desc");
    for (auto& k : defs["intruder"]["keywords"]) a.keywords.push_back(k.get<std::string>());
    a.t0 = elapsed;
    a.instab = JF(defs["intruder"], "instab", 1.8f);
    active.push_back(a);
    lastSpawnChannel = "intruder";
    TraceLog(LOG_INFO, "[anomaly] spawned intruder kind=intruder channel=intruder");
}

void AnomalyDirector::ClearIntruder(Office& office) {
    intruderActive = false;
    for (auto it = active.begin(); it != active.end(); ++it)
        if (it->id == "intruder") { active.erase(it); break; }
    (void)office;
}

void AnomalyDirector::ApplyExteriorFlags(Office& office, VisitorSystem& vs) {
    bool figW = false, figD = false, dark = false, empty = false, broken = false, doorBrk = false;
    int wview = 0; bool hl = false, bangD = false, bangW = false, mutW = false;
    // pending entries still hold their world effect until the crew arrives
    for (auto* list : {&active, &pending}) for (auto& a : *list) {
        if (a.kind == "figure_at_window") figW = true;
        if (a.kind == "figure_at_door") figD = true;
        if (a.kind == "lights_out") dark = true;
        if (a.kind == "empty_room") empty = true;
        if (a.kind == "window_flip") wview = 1;
        if (a.kind == "window_void") wview = 2;
        if (a.kind == "hall_long") hl = true;
        if (a.kind == "banging_door") bangD = true;
        if (a.kind == "banging_window") bangW = true;
        if (a.kind == "mutated_waiting") mutW = true;
        if (a.kind == "broken_window") broken = true;
        if (a.kind == "broken_door") doorBrk = true;
    }
    office.windowBroken = broken;
    office.doorBroken = doorBrk;
    office.waitingLightsOut = dark;
    office.windowView = wview;
    office.hallLong = hl;
    vs.waitingEmpty = empty;
    vs.ambientMutated = mutW;
    vs.figureActive = figW || figD;
    if (figW) { vs.exteriorFigure = "figure_at_window"; vs.figurePos = office.figurePositions["at_window"]; }
    else if (figD) { vs.exteriorFigure = "figure_at_door"; vs.figurePos = office.figurePositions["at_door"]; }
    else vs.exteriorFigure.clear();   // it left - a stale id would re-arm the breach check
    // the door figure doesn't just stand there - it pounds the door panel
    // (banger machinery: Knock sfx + swing anim + breach bookkeeping). A
    // real banging_door def still draws its own humanoid on top of this.
    vs.bangerActive = bangD || bangW || figD;
    if (bangD || figD) {
        vs.bangerAt = "door";
        vs.bangerPos = office.figurePositions["at_door"];
        vs.bangerPos.z += 0.60f;   // fists have to REACH the door panel
    } else if (bangW) { vs.bangerAt = "window"; vs.bangerPos = office.figurePositions["at_window"]; }
    else vs.bangerAt.clear();
}

ResolveOutcome AnomalyDirector::BeginResolve(const std::string& channel, const std::string& anomalyId,
                                             Office& office, VisitorSystem& vs, bool deferFx) {
    ResolveOutcome o;
    for (auto it = active.begin(); it != active.end(); ++it) {
        if (it->channel != channel || it->id != anomalyId) continue;
        o.ok = true; o.id = it->id; o.kind = it->kind; o.channel = channel; o.deferred = deferFx;
        o.hadTarget = it->hadTarget; o.snapshot = it->snapshot; o.targetId = it->snapshot.id;
        o.hadTarget2 = it->hadTarget2; o.snapshot2 = it->snapshot2;
        o.extraSnaps = it->extraSnaps;
        o.spawnedId = it->spawnedId;
        // where the crew should stand to fix this
        o.workPos = (channel == "exterior") ? Vector3{0, 0, -8.4f} : Vector3{0.9f, 0, 0.4f};
        if (o.hadTarget) o.workPos = office.Find(o.targetId) ? office.Find(o.targetId)->pos : it->snapshot.pos;
        // riders are whoever sits on the host's top RIGHT NOW (they rode the
        // mutation over). Captured once by id - re-testing RestsOn against a
        // moving host every frame lets it grab passers-by mid-sweep and drag
        // them to the destination (the mouse "carrying" the keyboard bug)
        if (o.hadTarget)
            if (Furni* hf = office.Find(o.targetId))
                for (auto& it2 : office.items)
                    if (office.RestsOn(it2, *hf)) o.riderIds.push_back(it2.id);
        else if (!o.extraSnaps.empty()) o.workPos = o.extraSnaps[0].pos;
        else if (!o.spawnedId.empty()) { if (Furni* f = office.Find(o.spawnedId)) o.workPos = f->pos; }
        else if (o.kind.rfind("figure", 0) == 0) o.workPos = vs.figurePos;
        else if (o.kind == "banging_door" || o.kind == "banging_window") o.workPos = vs.bangerPos;
        else if (o.kind == "window_flip" || o.kind == "window_void" || o.kind == "broken_window") {
            Vector3 w = office.WindowCenter(); o.workPos = {w.x, 0, w.z + 1.2f};
        }
        else if (o.kind == "mutated_waiting" || o.kind == "empty_room" || o.kind == "lights_out") {
            bool found = false;
            for (auto& f : office.items)
                if (f.room == "waiting" && f.ft == FT::CeilingLight) { o.workPos = f.pos; found = true; break; }
            if (!found) o.workPos = {0, 0, -7.8f};
        }
        else if (o.kind == "hall_long") o.workPos = {5.6f, 0, -9.2f};
        else if (o.kind == "broken_door") o.workPos = {0.f, 0.f, -6.2f};   // re-hang it from the hall side
        else if (o.kind == "phone_call") {
            if (Furni* f = office.Find("desk_phone")) o.workPos = f->pos;
        }
        else if (o.kind == "lights_office") {
            for (auto& f : office.items)
                if (f.room == "office" && f.ft == FT::CeilingLight) { o.workPos = f.pos; break; }
        }
        // the shelf is holding the door shut - crew can't get through it, so
        // they shove it back from the waiting-room side of the doorway
        if (o.id == "bookshelf_door") o.workPos = {0.f, 0.f, -6.35f};
        if (channel == "intruder") {
            if (deferFx) intruderT = 1e9f;      // strike paused while security is en route
            else intruderActive = false;
        }
        // keep flag-driven anomalies alive in the world until the crew finishes
        if (deferFx && (channel == "exterior" || !o.kind.empty())) pending.push_back(*it);
        exhausted.push_back(it->id);
        active.erase(it);
        // breathing room after a catch: at hot tiers the raw interval can be
        // ~30s, which chains scares back-to-back. A guaranteed beat of quiet
        // makes each anomaly land and keeps long nights survivable.
        nextSpawnIn = std::max(nextSpawnIn, NightScale("resolve_calm_sec", 0.f));
        AddInstability(-JF(cfg, "success_relief", 4.f));
        ApplyExteriorFlags(office, vs);
        return o;
    }
    return o;
}

void AnomalyDirector::ApplyResolveFx(const ResolveOutcome& o, Office& office) {
    if (o.hadTarget)
        if (Furni* f = office.Find(o.targetId)) {
            // riders carried along by a translate/pos_set (levitating desk's
            // monitor/phone/papers) come back with the host - shift exactly
            // the ids captured at BeginResolve, by the restore delta
            Vector3 d{o.snapshot.pos.x - f->pos.x, o.snapshot.pos.y - f->pos.y,
                      o.snapshot.pos.z - f->pos.z};
            for (auto& rid : o.riderIds)
                if (Furni* r = office.Find(rid)) r->pos = Vector3Add(r->pos, d);
            *f = o.snapshot;
        }
    if (o.hadTarget2)
        if (Furni* f2 = office.Find(o.snapshot2.id)) *f2 = o.snapshot2;
    for (auto& s : o.extraSnaps)
        if (Furni* f = office.Find(s.id)) *f = s;
    // deferred spawned furniture is carried out by the crew instead of removed here
    if (!o.spawnedId.empty() && !o.deferred) office.RemoveSpawned(o.spawnedId);
    std::erase_if(pending, [&](const ActiveAnomaly& a) { return a.id == o.id; });
}

bool AnomalyDirector::TryResolve(const std::string& channel, const std::string& anomalyId,
                                 Office& office, VisitorSystem& vs) {
    auto o = BeginResolve(channel, anomalyId, office, vs, false);
    if (o.ok) ApplyResolveFx(o, office);
    return o.ok;
}

std::vector<std::pair<std::string, std::vector<std::string>>>
AnomalyDirector::Candidates(const std::string& channel) const {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (auto& a : active) if (a.channel == channel) out.push_back({a.id, a.keywords});
    return out;
}
std::string AnomalyDirector::ActiveDescriptions(const std::string& channel,
                                                const std::string& skipKindPrefix) const {
    std::string s;
    for (auto& a : active)
        if (a.channel == channel && (skipKindPrefix.empty() || a.kind.rfind(skipKindPrefix, 0) != 0))
            s += "- [" + a.id + "] " + a.desc + "\n";
    return s.empty() ? "(none)" : s;
}
std::string AnomalyDirector::KindOf(const std::string& id) const {
    for (auto& a : active) if (a.id == id) return a.kind;
    return "";
}
std::vector<std::string> AnomalyDirector::ActiveKeywords(const std::string& kindPrefix) const {
    std::vector<std::string> out;
    for (auto& a : active)
        if (a.kind.rfind(kindPrefix, 0) == 0)
            for (auto& k : a.keywords) out.push_back(k);
    return out;
}
bool AnomalyDirector::Has(const std::string& channel) const {
    for (auto* l : {&active, &pending}) for (auto& a : *l) if (a.channel == channel) return true;
    return false;
}
bool AnomalyDirector::HasKind(const std::string& kind) const {
    for (auto* l : {&active, &pending}) for (auto& a : *l) if (a.kind == kind) return true;
    return false;
}
bool AnomalyDirector::IdLive(const std::string& id) const {
    for (auto* l : {&active, &pending}) for (auto& a : *l) if (a.id == id) return true;
    return false;
}

void AnomalyDirector::EnsureVisitor(const std::string& kind) {
    std::string id = "v_" + kind;
    for (auto& a : active) if (a.id == id) return;
    json d = defs["visitor_anomalies"][kind];
    ActiveAnomaly a;
    a.id = id; a.channel = "visitor"; a.kind = kind;
    a.desc = JS(d, "desc");
    if (d.contains("keywords")) for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
    a.t0 = elapsed;
    a.instab = JF(d, "instab", 1.f);
    active.push_back(a);
    lastSpawnChannel = "visitor";
}

void AnomalyDirector::ClearChannel(const std::string& channel) {
    std::erase_if(active, [&](const ActiveAnomaly& a) { return a.channel == channel; });
}

void AnomalyDirector::DropId(const std::string& id) {
    std::erase_if(active, [&](const ActiveAnomaly& a) { return a.id == id; });
}
void AnomalyDirector::DropKind(const std::string& kind) {
    std::erase_if(active, [&](const ActiveAnomaly& a) { return a.kind == kind; });
}

// spawn a def by KIND (figure_at_door, lights_office, banging_window...) -
// used by phone-call outcome effects and dev hooks
bool AnomalyDirector::DebugForceKind(const std::string& kind, Office& office,
                                     VisitorSystem* vs) {
    for (auto& a : active) if (a.kind == kind) return false;   // already live
    auto tryList = [&](const json& list, const char* ch) -> bool {
        for (auto& d : list) {
            if (JS(d, "kind") != kind) continue;
            ActiveAnomaly a;
            a.id = JS(d, "id"); a.channel = ch; a.kind = kind; a.desc = JS(d, "desc");
            a.instab = JF(d, "instab", 1.f);
            if (d.contains("keywords"))
                for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
            std::string tgt = JS(d, "target");
            if (!tgt.empty()) if (Furni* f = office.Find(tgt)) { a.hadTarget = true; a.snapshot = *f; }
            if (d.contains("mutation")) {
                office.ApplyMutationAnimated(tgt, d["mutation"]);
                if (d["mutation"].contains("spawn")) a.spawnedId = JS(d["mutation"]["spawn"], "id");
            }
            a.t0 = elapsed;
            active.push_back(a);
            lastSpawnChannel = ch;
            TraceLog(LOG_INFO, "[anomaly] spawned %s kind=%s channel=%s target=%s",
                     a.id.c_str(), a.kind.c_str(), ch, tgt.empty() ? "-" : tgt.c_str());
            if (vs) ApplyExteriorFlags(office, *vs);
            return true;
        }
        return false;
    };
    if (tryList(defs["exterior"], "exterior")) return true;
    return tryList(defs["office"], "office");
}

void AnomalyDirector::DebugCallEvent(const json& ev) {
    pendingCall = ev;
    ActiveAnomaly a;
    a.id = JS(ev, "id"); a.channel = "office"; a.kind = "phone_call";
    a.desc = JS(ev, "desc"); a.t0 = elapsed;
    a.instab = JF(ev, "instab", 1.2f);
    if (ev.contains("keywords"))
        for (auto& k : ev["keywords"]) a.keywords.push_back(k.get<std::string>());
    active.push_back(a);
    lastSpawnChannel = "call";
    TraceLog(LOG_INFO, "[anomaly] spawned %s kind=phone_call channel=call", a.id.c_str());
}

json AnomalyDirector::TakeCallEvent() {
    json e = pendingCall;
    pendingCall = json();
    return e;
}

// LLM-authored call personas join the call_events pool: required fields are
// enforced, and the effect blocks are clamped to the real effect vocabulary
// so a hallucinated "summon_eight_dogs" can't corrupt TriggerEffect
int AnomalyDirector::InjectCallEvents(const json& arr) {
    if (!arr.is_array() || !defs.contains("call_events")) return 0;
    // a fresh batch replaces last night's generated callers - the pool
    // can't just grow every shift
    auto& ce = defs["call_events"];
    for (int i = (int)ce.size() - 1; i >= 0; i--)
        if (JS(ce[i], "id").rfind("callgen_", 0) == 0) ce.erase(i);
    int added = 0;
    for (auto& e : arr) {
        if (!e.is_object()) continue;
        std::string persona = JS(e, "persona"), sys = JS(e, "system"), greet = JS(e, "greeting");
        if (persona.empty() || sys.empty() || greet.empty()) continue;
        json ev;
        ev["id"] = "callgen_" + std::to_string(added) + "_" + std::to_string(RandInt(0, 9999));
        ev["persona"] = persona;
        ev["system"] = sys;
        ev["greeting"] = greet;
        ev["desc"] = e.contains("desc") ? JS(e, "desc")
                                      : "The desk phone rings - " + persona;
        ev["instab"] = std::clamp(JF(e, "instab", 1.3f), 0.5f, 3.f);
        json kw = e.value("keywords", json::array());
        if (!kw.is_array() || kw.empty()) kw = json::array({"phone", "call", "ringing"});
        ev["keywords"] = kw;
        auto fx = [&](const char* k) {
            json o = json::object();
            if (e.contains(k) && e[k].is_object()) {
                const json& f = e[k];
                if (f.contains("instability")) o["instability"] = std::clamp(JF(f, "instability", 10.f), 2.f, 20.f);
                if (f.contains("relief"))      o["relief"]      = std::clamp(JF(f, "relief", 6.f), 2.f, 12.f);
                std::string fg = JS(f, "figure");  if (fg == "at_door" || fg == "at_window") o["figure"] = fg;
                std::string lt = JS(f, "lights");  if (lt == "office" || lt == "out")        o["lights"] = lt;
                std::string bg = JS(f, "banging"); if (bg == "door" || bg == "window")       o["banging"] = bg;
            }
            return o;
        };
        ev["on_calm"] = fx("on_calm");     if (ev["on_calm"].empty())    ev["on_calm"] = {{"relief", 6}};
        ev["on_hostile"] = fx("on_hostile"); if (ev["on_hostile"].empty()) ev["on_hostile"] = {{"instability", 12}};
        ev["on_ignore"] = fx("on_ignore");   if (ev["on_ignore"].empty())  ev["on_ignore"] = {{"instability", 9}};
        defs["call_events"].push_back(ev);
        added++;
    }
    return added;
}

// a call outcome can ripple back into the world: instability, a figure
// outside, lights dying somewhere, or somebody new banging on the glass
void AnomalyDirector::TriggerEffect(const json& fx, Office& office, VisitorSystem& vs) {
    if (fx.is_null() || !fx.is_object()) return;
    if (fx.contains("instability")) AddInstability(JF(fx, "instability", 10));
    if (fx.contains("relief")) AddInstability(-JF(fx, "relief", 6));
    if (fx.contains("figure")) {
        std::string w = JS(fx, "figure");
        DebugForceKind("figure_" + w, office, &vs);
    }
    if (fx.contains("lights")) {
        std::string w = JS(fx, "lights");
        DebugForceKind(w == "office" ? "lights_office" : "lights_out", office, &vs);
    }
    if (fx.contains("banging")) {
        std::string w = JS(fx, "banging");
        DebugForceKind(w == "window" ? "banging_window" : "banging_door", office, &vs);
    }
    ApplyExteriorFlags(office, vs);
}

// -------- LLM remix anomalies: validated against the parts catalog --------

std::string AnomalyDirector::RemixPrompt() const {
    if (parts.empty()) return "";
    std::string s = "You design one anomaly for a horror office game by REMIXING existing parts only.\n"
        "Reply with ONLY a JSON object: {\"id\":\"rx_<name>\",\"channel\":\"office\",\"desc\":\"<what is wrong>\","
        "\"keywords\":[\"...\"],\"target\":\"<existing id>\"|null,\"mutation\":{...}|\"spawn\":{...}}.\n"
        "Rules: either 'target'+mutation alters an existing object, or 'spawn' creates a new object.\n"
        "Allowed spawn types: ";
    for (auto& t : parts["spawnable_types"]) s += t.get<std::string>() + " ";
    s += "\nAllowed mutation fields: ";
    for (auto& f : parts["mutate_fields"]) s += f.get<std::string>() + " ";
    s += "\nExisting object ids you may target: ";
    for (auto& t : parts["existing_targets"]) s += t.get<std::string>() + " ";
    s += "\nGood spawn positions by area (pick one): ";
    for (auto& [k, arr] : parts["spawn_anchors"].items())
        s += k + "=" + arr[0].dump() + " ";
    s += "\nKeep size under " + parts["limits"]["size_max"].dump() +
         ", scale under " + parts["limits"]["scale_max"].dump() +
         ". Make it eerie but explainable in the desc. keywords: object names a player would type.";
    return s;
}

static bool VecInBounds(const json& v, const json& lo, const json& hi) {
    if (!v.is_array() || v.size() < 3) return false;
    for (int i = 0; i < 3; i++)
        if (v[i].get<float>() < lo[i].get<float>() || v[i].get<float>() > hi[i].get<float>()) return false;
    return true;
}
static bool VecUnder(const json& v, const json& mx) {
    if (!v.is_array() || v.size() < 3) return false;
    for (int i = 0; i < 3; i++) if (std::fabs(v[i].get<float>()) > mx[i].get<float>()) return false;
    return true;
}

bool AnomalyDirector::ApplyRemixJson(const std::string& text, Office& office, std::string& why) {
    auto a = text.find('{'), b = text.rfind('}');
    if (a == std::string::npos || b <= a) { why = "no json"; return false; }
    json r;
    try { r = json::parse(text.substr(a, b - a + 1)); }
    catch (...) { why = "parse"; return false; }

    std::string id = "rx_" + std::to_string(RandInt(0, 99999));
    std::string desc = JS(r, "desc");
    if (desc.empty() || desc.size() > JI(parts["limits"], "desc_max_len", 160)) { why = "desc"; return false; }
    for (auto* x : {&active, &pending}) for (auto& e : *x)
        if (e.id == id) { why = "dupe"; return false; }

    json mut = json::object();
    std::string target;
    // allowed mutation fields only - anything else is dropped
    static const char* fields[] = {"translate","pos_set","rot_set","scale","color","hidden","eyes","stopped"};
    auto allowed = [&](const std::string& k) {
        for (auto* f : fields) if (k == f) return true; return false;
    };
    if (r.contains("spawn") && r["spawn"].is_object()) {
        json s = r["spawn"];
        std::string type = JS(s, "type");
        bool okType = false;
        for (auto& t : parts["spawnable_types"]) if (t.get<std::string>() == type) okType = true;
        if (!okType) { why = "type"; return false; }
        // spawn needs a sane pos + size inside a room
        std::string room = JS(s, "room", "office");
        const json& lo = room == "waiting" ? parts["limits"]["waiting_min"] : parts["limits"]["office_min"];
        const json& hi = room == "waiting" ? parts["limits"]["waiting_max"] : parts["limits"]["office_max"];
        if (!s.contains("pos") || !VecInBounds(s["pos"], lo, hi)) { why = "pos"; return false; }
        if (s.contains("size") && !VecUnder(s["size"], parts["limits"]["size_max"])) { why = "size"; return false; }
        json sp = {{"id", id + "_prop"}, {"type", type}, {"pos", s["pos"]},
                   {"size", s.value("size", json{0.3f, 0.3f, 0.3f})},
                   {"rot", JF(s, "rot", 0)}, {"color", s.value("color", json{80, 80, 88})},
                   {"solid", JB(s, "solid", false)}, {"room", room}};
        // static-state spawn fields a remix may legitimately ask for
        // (a stopped clock, an eyed prop, a different poster print)
        if (s.contains("art"))       sp["art"] = JS(s, "art");
        if (s.contains("eyes"))      sp["eyes"] = JB(s, "eyes", false);
        if (s.contains("stopped"))   sp["stopped"] = JB(s, "stopped", false);
        if (s.contains("backwards")) sp["backwards"] = JB(s, "backwards", false);
        mut["spawn"] = sp;
    } else if (r.contains("target") && r["target"].is_string()) {
        target = r["target"].get<std::string>();
        bool okT = false;
        for (auto& t : parts["existing_targets"]) if (t.get<std::string>() == target) okT = true;
        if (!okT || !office.Find(target)) { why = "target"; return false; }
        if (!r.contains("mutation") || !r["mutation"].is_object()) { why = "mutation"; return false; }
        for (auto& [k, v] : r["mutation"].items()) {
            if (!allowed(k)) continue;
            if ((k == "scale" || k == "translate" || k == "pos_set") && !v.is_array()) continue;
            if (k == "scale" && !VecUnder(v, parts["limits"]["scale_max"])) { why = "scale"; return false; }
            mut[k] = v;
        }
        if (mut.empty()) { why = "empty mutation"; return false; }
    } else { why = "no spawn/target"; return false; }

    ActiveAnomaly an;
    an.id = id; an.channel = "office"; an.kind = "remix";
    an.desc = desc; an.t0 = elapsed;
    an.instab = std::clamp(JF(r, "instab", 1.2f), 0.2f, 2.5f);
    if (r.contains("keywords"))
        for (auto& k : r["keywords"]) {
            if (an.keywords.size() >= (size_t)JI(parts["limits"], "keyword_max", 10)) break;
            an.keywords.push_back(k.get<std::string>());
        }
    if (an.keywords.empty()) an.keywords.push_back("wrong");
    if (!target.empty())
        if (Furni* f = office.Find(target)) { an.hadTarget = true; an.snapshot = *f; }
    office.ApplyMutationAnimated(target, mut);
    if (mut.contains("spawn")) an.spawnedId = JS(mut["spawn"], "id");
    active.push_back(an);
    lastSpawnChannel = "office";
    return true;
}

void AnomalyDirector::DebugForceOffice(const std::string& id, Office& office) {
    // generic defs roll their own target+effect per spawn
    if (defs.contains("generic"))
        for (auto& d : defs["generic"])
            if (JS(d, "id") == id) { SpawnGeneric(d, office); return; }
    for (auto& d : defs["office"]) {
        if (JS(d, "id") != id) continue;
        ActiveAnomaly a;
        a.id = id; a.channel = "office"; a.kind = JS(d, "kind"); a.desc = JS(d, "desc");
        a.instab = JF(d, "instab", 1.f);
        for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
        std::string tgt = JS(d, "target");
        if (!tgt.empty()) if (Furni* f = office.Find(tgt)) { a.hadTarget = true; a.snapshot = *f; }
        office.ApplyMutationAnimated(tgt, d["mutation"]);
        if (d["mutation"].contains("spawn")) a.spawnedId = JS(d["mutation"]["spawn"], "id");
        a.t0 = elapsed;
        active.push_back(a);
        lastSpawnChannel = "office";
        TraceLog(LOG_INFO, "[anomaly] spawned %s kind=%s channel=office target=%s",
                 a.id.c_str(), a.kind.empty() ? "-" : a.kind.c_str(), tgt.empty() ? "-" : tgt.c_str());
        return;
    }
}

json AnomalyDirector::Serialize() const {
    json j;
    j["instability"] = instab;
    j["exhausted"] = exhausted;
    j["elapsed"] = elapsed;
    json arr = json::array();
    for (auto& a : active)
        arr.push_back({{"id", a.id}, {"channel", a.channel}, {"kind", a.kind},
                       {"target", a.hadTarget ? a.snapshot.id : ""}, {"spawned", a.spawnedId},
                       {"genMuts", a.genMuts}, {"genSpawn", a.genSpawn}});
    j["active"] = arr;
    j["intruder"] = intruderActive;
    if (intruderActive) {
        j["intruderT"] = intruderT;
        j["intruderPos"] = {intruderPos.x, intruderPos.y, intruderPos.z};
    }
    return j;
}

void AnomalyDirector::Deserialize(const json& j, Office& office, VisitorSystem& vs) {
    instab = j.value("instability", 12.f);
    exhausted = j.value("exhausted", std::vector<std::string>{});
    elapsed = j.value("elapsed", 0.f);
    intruderActive = j.value("intruder", false);
    intruderT = j.value("intruderT", 20.f);
    if (j.contains("intruderPos")) intruderPos = JV3(j["intruderPos"]);
    active.clear();
    pending.clear();   // unresolved crew jobs don't survive a load
    for (auto& e : j["active"]) {
        std::string ch = e.value("channel", std::string());
        if (ch == "office") {
            // re-apply the office mutation by id
            for (auto& d : defs["office"]) {
                if (JS(d, "id") == e.value("id", std::string())) {
                    ActiveAnomaly a;
                    a.id = JS(d, "id"); a.channel = "office"; a.kind = JS(d, "kind"); a.desc = JS(d, "desc");
                    a.instab = JF(d, "instab", 1.f);
                    for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
                    std::string tgt = JS(d, "target");
                    if (!tgt.empty()) if (Furni* f = office.Find(tgt)) { a.hadTarget = true; a.snapshot = *f; }
                    office.ApplyMutation(tgt, d["mutation"]);
                    if (d["mutation"].contains("spawn")) a.spawnedId = JS(d["mutation"]["spawn"], "id");
                    active.push_back(a);
                }
            }
            // generic defs rolled their world effect at spawn - the stored
            // mutations re-apply the exact same deltas over the fresh office
            for (auto& d : defs["generic"]) {
                if (JS(d, "id") != e.value("id", std::string())) continue;
                ActiveAnomaly a;
                a.id = JS(d, "id"); a.channel = "office"; a.kind = JS(d, "kind");
                a.desc = JS(d, "desc"); a.instab = JF(d, "instab", 1.f);
                for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
                int ti = 0;
                for (auto& tm : e.value("genMuts", json::array())) {
                    std::string tid = JS(tm, "id");
                    Furni* f = office.Find(tid); if (!f) continue;
                    if (ti == 0) { a.hadTarget = true; a.snapshot = *f; }
                    else if (ti == 1) { a.hadTarget2 = true; a.snapshot2 = *f; }
                    else a.extraSnaps.push_back(*f);
                    office.ApplyMutation(tid, tm["mut"]);
                    ti++;
                }
                if (e.contains("genSpawn") && e["genSpawn"].is_object()) {
                    office.ApplyMutation("", e["genSpawn"]);
                    a.spawnedId = JS(e["genSpawn"]["spawn"], "id");
                }
                active.push_back(a);
            }
        } else {
            ActiveAnomaly a;
            a.id = e.value("id", ""); a.channel = ch; a.kind = e.value("kind", "");
            // find desc/keywords
            json src = (ch == "exterior") ? defs["exterior"] : (ch == "intruder" ? defs["intruder"] : defs["visitor_anomalies"]);
            for (auto& d : src) {
                std::string id = JS(d, "id");
                if (id == a.id || (ch == "visitor" && id == "v_" + a.kind) || JS(d, "kind") == a.kind) {
                    a.desc = JS(d, "desc");
                    a.instab = JF(d, "instab", 1.f);
                    if (d.contains("keywords")) for (auto& k : d["keywords"]) a.keywords.push_back(k.get<std::string>());
                }
            }
            if (ch == "intruder") a.instab = JF(defs["intruder"], "instab", 1.8f);
            if (ch == "visitor" && a.kind.empty() && a.id.rfind("v_", 0) == 0) a.kind = a.id.substr(2);
            // exterior defs that carry a target+mutation (waiting-room
            // furniture) must re-apply their world effect on load too
            if (ch == "exterior")
                for (auto& d : defs["exterior"]) {
                    if (JS(d, "id") != a.id || !d.contains("mutation")) continue;
                    std::string t2 = JS(d, "target");
                    if (!t2.empty()) if (Furni* f = office.Find(t2)) { a.hadTarget = true; a.snapshot = *f; }
                    office.ApplyMutation(t2, d["mutation"]);
                    if (d["mutation"].contains("spawn")) a.spawnedId = JS(d["mutation"]["spawn"], "id");
                }
            active.push_back(a);
        }
    }
    ApplyExteriorFlags(office, vs);
}
