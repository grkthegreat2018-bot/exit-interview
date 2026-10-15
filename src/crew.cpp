#include "game.h"
#include "hud.h"
#include <rlgl.h>
#include <cstdlib>
#include <cstring>


// ---------- crew dispatch ----------
// A resolved anomaly gets a physical response: workers/guards walk the hall
// path into the room, go to the actual anomaly spot, and act on it - ladder
// work on ceiling lights, replacement parts for missing items, toolkits for
// moved/changed furniture, movers hauling out spawned objects, guards
// sweeping figures/escorting visitors. World effect reverts when they finish.

static void DrawLadder(Vector3 base, float yawDeg, float tiltDeg) {
    rlPushMatrix();
    rlTranslatef(base.x, base.y, base.z);
    rlRotatef(yawDeg, 0, 1, 0);
    rlRotatef(tiltDeg, 1, 0, 0);
    Color al{170, 176, 186, 255}, dk{118, 124, 134, 255};
    DrawCubeV({-0.21f, 0.95f, 0}, {0.05f, 1.9f, 0.055f}, al);
    DrawCubeV({ 0.21f, 0.95f, 0}, {0.05f, 1.9f, 0.055f}, al);
    for (int i = 0; i < 5; i++)
        DrawCubeV({0, 0.28f + i * 0.32f, 0}, {0.44f, 0.05f, 0.045f}, dk);
    rlPopMatrix();
}


// shared walkability test: inside a room, not in the dividing wall (except the
// door gap), and not inside floor furniture. Used by stand-spot pick + A*.
bool Game::PointBlocked(float x, float z, float mgn) {
    // room membership - the z=-5 side is NOT inset; the wall band check below
    // handles it so the door gap stays walkable
    bool inOffice = x > office.bounds.mn.x + mgn && x < office.bounds.mx.x - mgn &&
                    z > office.doorZ - 0.05f && z < office.bounds.mx.z - mgn;
    bool inWait = x > office.waitingBounds.mn.x + mgn && x < office.waitingBounds.mx.x - mgn &&
                  z > office.waitingBounds.mn.z + mgn && z < office.doorZ + 0.05f;
    if (!inOffice && !inWait) return true;
    // office/waiting dividing wall at doorZ - only the door opening is passable
    if (std::fabs(z - office.doorZ) < 0.16f + mgn * 0.5f &&
        (x < office.doorX.x + mgn || x > office.doorX.y - mgn)) return true;
    for (auto& f : office.items) {
        if (f.hidden || f.pos.y > 0.6f || f.ft == FT::Door) continue;
        float c = std::cos(f.rotY * DEG2RAD), s = std::sin(f.rotY * DEG2RAD);
        float hx = (std::fabs(f.size.x * c) + std::fabs(f.size.z * s)) / 2 + mgn;
        float hz = (std::fabs(f.size.x * s) + std::fabs(f.size.z * c)) / 2 + mgn;
        if (hx * hz < 0.03f) continue;   // thin wall decor doesn't block feet
        if (x > f.pos.x - hx && x < f.pos.x + hx && z > f.pos.z - hz && z < f.pos.z + hz) return true;
    }
    return false;
}


// A* over a coarse grid spanning both rooms, then line-of-sight smoothing.
std::vector<Vector2> Game::NavPath(Vector2 from, Vector2 to) {
    const float cell = 0.3f;
    const float x0 = office.waitingBounds.mn.x, z0 = office.waitingBounds.mn.z;
    const int W = (int)((office.bounds.mx.x - x0) / cell);
    const int H = (int)((office.bounds.mx.z - z0) / cell);
    auto at = [&](int x, int z) { return Vector2{x0 + (x + 0.5f) * cell, z0 + (z + 0.5f) * cell}; };
    auto cellOf = [&](Vector2 p) {
        int x = std::clamp((int)((p.x - x0) / cell), 0, W - 1);
        int z = std::clamp((int)((p.y - z0) / cell), 0, H - 1);
        return std::pair<int, int>{x, z};
    };
    std::vector<char> blk((size_t)W * H, 0);
    for (int z = 0; z < H; z++) for (int x = 0; x < W; x++) {
        Vector2 p = at(x, z);
        blk[(size_t)z * W + x] = PointBlocked(p.x, p.y, 0.36f) ? 1 : 0;
    }
    auto freeNear = [&](int& cx, int& cz) {
        if (!blk[(size_t)cz * W + cx]) return;
        for (int r = 1; r < 10; r++)
            for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int nx = cx + dx, nz = cz + dz;
                if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                if (!blk[(size_t)nz * W + nx]) { cx = nx; cz = nz; return; }
            }
    };
    auto [sx, sz] = cellOf(from); auto [tx, tz] = cellOf(to);
    freeNear(sx, sz); freeNear(tx, tz);

    // A* - 8 directions, no diagonal corner cutting
    const int N = W * H;
    std::vector<float> g(N, 1e9f);
    std::vector<int> prev(N, -1);
    auto h = [&](int x, int z) { return std::hypotf((float)(x - tx), (float)(z - tz)); };
    std::vector<std::pair<float, int>> open;
    open.push_back({h(sx, sz), sz * W + sx});
    g[sz * W + sx] = 0;
    static const int D[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    int found = -1;
    while (!open.empty()) {
        int bi = 0;
        for (int i = 1; i < (int)open.size(); i++) if (open[i].first < open[bi].first) bi = i;
        int cur = open[bi].second; open[bi] = open.back(); open.pop_back();
        if (cur == tz * W + tx) { found = cur; break; }
        int cx = cur % W, cz = cur / W;
        for (auto& d : D) {
            int nx = cx + d[0], nz = cz + d[1];
            if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
            if (blk[(size_t)nz * W + nx]) continue;
            if (d[0] && d[1] && (blk[(size_t)cz * W + nx] || blk[(size_t)nz * W + cx])) continue;
            float ng = g[cur] + (d[0] && d[1] ? 1.414f : 1.f);
            int ni = nz * W + nx;
            if (ng < g[ni]) { g[ni] = ng; prev[ni] = cur; open.push_back({ng + h(nx, nz), ni}); }
        }
    }
    if (found < 0) return {to};

    std::vector<Vector2> path;
    for (int n = found; n != -1; n = prev[n]) path.push_back(at(n % W, n / W));
    std::reverse(path.begin(), path.end());
    if (path.size() > 1) { path.front() = from; path.back() = to; }

    // greedy line-of-sight smoothing
    auto clear = [&](Vector2 a, Vector2 b) {
        float dx = b.x - a.x, dz = b.y - a.y;
        int n = (int)(std::sqrt(dx * dx + dz * dz) / 0.12f) + 1;
        for (int i = 1; i < n; i++) {
            float t = (float)i / n;
            if (PointBlocked(a.x + dx * t, a.y + dz * t, 0.34f)) return false;
        }
        return true;
    };
    std::vector<Vector2> sm;
    sm.push_back(path[0]);
    int i = 0;
    while (i < (int)path.size() - 1) {
        int j = (int)path.size() - 1;
        while (j > i + 1 && !clear(path[i], path[j])) j--;
        sm.push_back(path[j]);
        i = j;
    }
    return sm;
}


// find a free spot on the floor near the anomaly - never inside furniture,
// and never on the wrong side of the doorZ dividing wall (a spot behind the
// wall looks close but leaves the crew working through a solid barrier)
Vector2 Game::PickStandSpot(Vector3 wp, float prefA) {
    bool officeSide = wp.z > office.doorZ;
    auto ring = [&](bool sameRoom, Vector2& hit) {
        for (float r = 0.8f; r <= 3.0f; r += 0.45f)
            for (int i = 0; i < 12; i++) {
                float a = prefA + (i == 0 ? 0.f : ((i + 1) / 2) * (i % 2 ? 1.f : -1.f) * 0.52f);
                float x = wp.x + std::sin(a) * r, z = wp.z + std::cos(a) * r;
                if (sameRoom && (z > office.doorZ) != officeSide) continue;
                if (!PointBlocked(x, z, 0.34f)) { hit = {x, z}; return true; }
            }
        return false;
    };
    Vector2 hit{wp.x, wp.z};
    ring(true, hit) || ring(false, hit);
    return hit;
}


// what two movers can actually lift and haul out - outlets and switches are
// wired into the wall, rugs/cabinets/couches are too heavy, and a spawned
// second DOOR is part of the building itself
static bool Carryable(FT ft) {
    switch (ft) {
        case FT::VisitorChair: case FT::OfficeChair: case FT::Mug: case FT::Bin:
        case FT::PaperStack: case FT::Phone: case FT::Vase: case FT::Note:
        case FT::Printer: case FT::Mouse: case FT::Keyboard: case FT::DeskLamp:
        case FT::Plant: case FT::Poster: case FT::WallClock:
            return true;
        default:
            return false;
    }
}

void Game::DispatchCrew(std::vector<ResolveOutcome> outs, int men, bool guards, bool escortVisitor) {
    outs.erase(std::remove_if(outs.begin(), outs.end(), [](const ResolveOutcome& o) { return !o.ok; }), outs.end());
    if (outs.empty()) return;
    for (auto& c : crew) if (c.outcomes[0].id == outs[0].id) return;   // already en route
    if (crew.size() >= 2) {
        // room is full - resolve instantly rather than leaving the anomaly
        // stuck in pending (which would hold its world effect on forever)
        for (auto& o : outs) ad.ApplyResolveFx(o, office);
        return;
    }

    CrewEvt e;
    e.outcomes = outs;
    e.workPos = outs[0].workPos;
    e.escortVisitor = escortVisitor;
    for (auto& o : outs) if (!o.spawnedId.empty()) {
        // liftable spawns get carried out; wall-wired and heavy ones get
        // torn down where they sit (toolbox work, then it's gone)
        if (Furni* sf = office.Find(o.spawnedId))
            if (Carryable(sf->ft)) e.carryId = o.spawnedId;
            else                  e.dismantleId = o.spawnedId;
    }

    // pick the right tool for the job
    const ResolveOutcome& lead = outs[0];
    if (escortVisitor || lead.kind.rfind("figure", 0) == 0 || lead.channel == "intruder") {
        e.prop = PROP_NONE; e.workLen = escortVisitor ? 2.0f : 2.2f;
    } else if (lead.kind == "lights_office" || lead.kind == "lights_out") {
        e.prop = PROP_LADDER; e.workLen = 4.2f;
        // pick the fixture with the clearest floor access - a light hanging
        // over the desk has no room for a ladder under it
        const char* room = (lead.channel == "exterior") ? "waiting" : "office";
        float best = 1e9f; Vector2 bestS{0, 0}; Vector3 bestL = e.workPos;
        for (auto& f : office.items) {
            if (f.room != room || f.ft != FT::CeilingLight) continue;
            Vector2 s = PickStandSpot(f.pos, std::atan2(0.f - f.pos.x, -4.4f - f.pos.z));
            float dx = s.x - f.pos.x, dz = s.y - f.pos.z;
            float score = dx * dx * 1.6f + dz * dz;          // favor staying on the light's axis
            if (score < best) { best = score; bestS = s; bestL = f.pos; }
        }
        e.workPos = bestL;
        e.ladderPos = {bestS.x, 0, bestS.y};
    } else if (!e.carryId.empty()) {
        e.workLen = 2.2f;                                   // movers haul it out
    } else if (!e.dismantleId.empty()) {
        e.prop = PROP_TOOLBOX; e.workLen = 3.0f;            // tear it down on site
    } else if (lead.hadTarget) {
        Furni* f = office.Find(lead.targetId);
        if (f && f->hidden) {                             // vanished item -> bring a replacement box
            e.prop = PROP_BOX; e.workLen = 2.6f;
            e.boxPos = {e.workPos.x, lead.snapshot.pos.y + lead.snapshot.size.y, e.workPos.z};
        } else {                                          // moved/changed -> toolkit repair
            e.prop = PROP_TOOLBOX; e.workLen = 3.4f;
            // a two-ton shelf holding a door shut takes real effort to move -
            // the delay IS the point: you're trapped with it for a while
            if (lead.id == "bookshelf_door") e.workLen = 7.5f;
        }
    } else { e.prop = PROP_TOOLBOX; e.workLen = 2.8f; }     // empty room: unjam the door

    const Vector2 HALL{5.6f, -10.55f};                    // corridor door - crew's in/out
    float prefA = std::atan2(0.f - e.workPos.x, -4.4f - e.workPos.z);   // approach from door side
    for (int i = 0; i < men; i++) {
        CrewMan m; m.guard = guards;
        m.app.height = guards ? 1.06f : 1.0f;
        m.app.shirt = guards ? Color{46, 54, 78, 255} : Color{215, 150, 50, 255};
        m.app.hair  = guards ? Color{60, 60, 66, 255} : Color{50, 45, 38, 255};
        m.app.skin = {200, 170, 145, 255};
        m.pos = {11.9f, 0.f, -12.55f};                   // well past the corridor corner
        m.delay = i * 1.1f;                              // file in one at a time

        Vector2 stand;
        if (e.prop == PROP_LADDER)
            stand = (i == 0) ? Vector2{e.ladderPos.x, e.ladderPos.z}
                             : PickStandSpot(e.ladderPos, prefA + 2.4f);
        else
            stand = PickStandSpot(e.workPos, prefA + i * 1.1f);

        // walk the corridor in THROUGH the hall door: corner -> doorway -> A*
        m.pathIn = {{8.9f, -12.55f}, {5.6f, -12.55f}, {5.6f, -11.4f}, HALL};
        auto nav = NavPath(HALL, stand);
        m.pathIn.insert(m.pathIn.end(), nav.begin(), nav.end());
        m.pathOut = NavPath(stand, HALL);
        // ...and back out through the hall door, vanishing past the corner
        m.pathOut.push_back({5.6f, -11.4f});
        m.pathOut.push_back({5.6f, -12.55f});
        m.pathOut.push_back({8.9f, -12.55f});
        m.pathOut.push_back({11.9f, -12.55f});
        e.men.push_back(m);
    }
    crew.push_back(e);
    {
        std::string what;
        for (auto& o : outs) what += (what.empty() ? "" : "+") + o.id;
        TraceLog(LOG_INFO, "[crew] dispatched %s x%d for %s",
                 guards ? "guards" : "workers", men, what.c_str());
    }
    Notify(guards ? "Security detail en route."
         : e.prop == PROP_LADDER ? "Maintenance is bringing a ladder."
         : "Maintenance crew en route.");
}


void Game::UpdateCrew(float dt) {
    if (crew.empty()) return;
    // crew hold the office door open while passing through it
    bool nearDoor = false;
    for (auto& e : crew) for (auto& m : e.men)
        if (std::fabs(m.pos.x) < 1.4f && std::fabs(m.pos.z + 5.75f) < 1.4f) nearDoor = true;
    if (nearDoor && !doorOpen && !doorBlocked) { doorOpen = true; crewOpenedDoor = true; audio.Play(AudioBank::Sfx::Creak, 0.5f); }

    auto walk = [](CrewMan& m, const std::vector<Vector2>& path, float dt, float sp) -> bool {
        if (m.delay > 0.f) { m.delay -= dt; return false; }
        if (m.wp >= (int)path.size()) return true;
        Vector2 t = path[m.wp];
        Vector2 d{t.x - m.pos.x, t.y - m.pos.z};
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        if (dist < 0.12f) { m.wp++; return m.wp >= (int)path.size(); }
        m.pos.x += d.x / dist * sp * dt; m.pos.z += d.y / dist * sp * dt;
        m.yaw = std::atan2(d.x, d.y) * RAD2DEG;
        m.animT += dt * 6.f;
        return false;
    };

    for (auto& e : crew) {
        bool allDone = true;
        int carrying = 0;
        for (auto& m : e.men) if (m.carrying) carrying++;
        for (auto& m : e.men) {
            if (e.phase == 0 || e.phase == 2) {
                // leaving: done workers walk a touch brisker; carriers slow under load
                float sp = e.phase == 2 ? (m.carrying ? 1.5f : 2.05f) : 1.9f;
                if (!walk(m, e.phase == 2 ? m.pathOut : m.pathIn, dt, sp)) allDone = false;
            } else if (e.phase == 1) {
                m.animT += dt * 6.f;   // busy hands - face the thing
                Vector2 d{e.workPos.x - m.pos.x, e.workPos.z - m.pos.z};
                if (d.x * d.x + d.y * d.y > 0.01f) m.yaw = std::atan2(d.x, d.y) * RAD2DEG;
            }
        }
        // ladder work: lead climbs while a second man steadies the base
        if (e.prop == PROP_LADDER && e.phase == 1 && !e.men.empty()) {
            CrewMan& lead = e.men[0];
            lead.lift = std::min(1.f, e.workT * 2.2f) * 1.45f;
            lead.armRaise = std::min(1.f, std::max(0.f, e.workT - 0.5f) * 2.f) * 0.95f;
            lead.pos.x = e.ladderPos.x; lead.pos.z = e.ladderPos.z;
            Vector2 dl{e.workPos.x - lead.pos.x, e.workPos.z - lead.pos.z};
            if (dl.x * dl.x + dl.y * dl.y > 0.01f) lead.yaw = std::atan2(dl.x, dl.y) * RAD2DEG;
        }
        // while the crew works on a wall-mounted carryable (poster, clock),
        // they lift it off the mount first - it slides down to hip height
        if (e.phase == 1 && !e.carryId.empty())
            if (Furni* f = office.Find(e.carryId))
                if (f->pos.y > 0.32f)
                    f->pos.y = std::max(0.32f, f->pos.y - dt * 1.3f);

        // security detail: the detained visitor is pinned between the guards
        // - a beat to cuff them during the work phase, then marched out on
        // the same walk-out path the crew takes
        if (e.escortVisitor && e.phase >= 1 && vs.Cur().detained) {
            Visitor& v = vs.CurMut();
            v.seated = false;
            v.detainWalk = (e.phase == 2);
            Vector3 mid{0, 0, 0}; float yawSum = 0; int n = 0;
            for (auto& m : e.men)
                if (m.delay <= 0.f) { mid.x += m.pos.x; mid.z += m.pos.z; yawSum += m.yaw; n++; }
            if (n) {
                if (e.phase == 2) {
                    float ay = yawSum / n;
                    Vector3 fw{std::sin(ay * DEG2RAD), 0, std::cos(ay * DEG2RAD)};
                    v.pos = {mid.x / n - fw.x * 0.28f, 0.f, mid.z / n - fw.z * 0.28f};
                    v.yaw = ay;
                } else {
                    // cuffing beat: they turn to face the arriving guards
                    Vector2 d{mid.x / n - v.pos.x, mid.z / n - v.pos.z};
                    if (d.x * d.x + d.y * d.y > 0.002f)
                        v.yaw = std::atan2(d.x, d.y) * RAD2DEG;
                }
            }
        }

        // two movers lift the spawned furniture between them
        if (e.phase == 2 && carrying > 0)
            if (Furni* f = office.Find(e.carryId)) {
                Vector3 mid{0, 0.42f, 0};
                for (auto& m : e.men) if (m.carrying) { mid.x += m.pos.x / carrying; mid.z += m.pos.z / carrying; }
                f->pos = mid;
            }
        if (e.phase == 0 && allDone) {
            e.phase = 1; e.workT = 0; e.propOut = true;
            audio.Play(AudioBank::Sfx::Thump, 0.3f);
            TraceLog(LOG_INFO, "[crew] on site - %s", e.outcomes[0].id.c_str());
            if (e.outcomes[0].channel == "intruder") ad.ClearIntruder(office);
            if (e.escortVisitor) vs.SecurityRemove();
            // figures read as "already gone when they got there" - clear on
            // arrival; mutants straighten up and act normal when challenged
            for (auto& o : e.outcomes)
                if (o.kind.rfind("figure", 0) == 0 || o.kind == "mutated_waiting")
                    ad.ApplyResolveFx(o, office);
        }
        if (e.phase == 1) {
            e.workT += dt;
            if ((int)(e.workT * 2.5f) != (int)((e.workT - dt) * 2.5f))   // tool ticks
                audio.Play(AudioBank::Sfx::TypeTick, 0.22f);
            // physically shove moved/turned furniture back while they work -
            // ApplyResolveFx still snaps the remainder when the job ends
            float pk = std::min(1.f, e.workT / std::max(0.1f, e.workLen));
            pk = pk * pk * (3.f - 2.f * pk);
            bool shoved = false;
            for (auto& o : e.outcomes) {
                if (!o.hadTarget) continue;
                Furni* f = office.Find(o.targetId);
                if (!f || f->hidden) continue;          // vanished item: the box swap reads instead
                float dx = f->pos.x - o.snapshot.pos.x, dy = f->pos.y - o.snapshot.pos.y,
                      dz = f->pos.z - o.snapshot.pos.z, dr = f->rotY - o.snapshot.rotY;
                Vector3 np{o.snapshot.pos.x + dx * (1.f - pk),
                           o.snapshot.pos.y + dy * (1.f - pk),
                           o.snapshot.pos.z + dz * (1.f - pk)};
                // riders on the host's top track the same shove delta - only
                // the ids captured at BeginResolve, never re-derived per frame
                // (a passing neighbor would get swept along with the host)
                Vector3 dd{np.x - f->pos.x, np.y - f->pos.y, np.z - f->pos.z};
                for (auto& rid : o.riderIds)
                    if (Furni* r = office.Find(rid)) r->pos = Vector3Add(r->pos, dd);
                f->pos = np;
                f->rotY = o.snapshot.rotY + dr * (1.f - pk);
                f->size = {o.snapshot.size.x + (f->size.x - o.snapshot.size.x) * (1.f - pk),
                           o.snapshot.size.y + (f->size.y - o.snapshot.size.y) * (1.f - pk),
                           o.snapshot.size.z + (f->size.z - o.snapshot.size.z) * (1.f - pk)};
                f->color.r = (unsigned char)(o.snapshot.color.r + (f->color.r - o.snapshot.color.r) * (1.f - pk));
                f->color.g = (unsigned char)(o.snapshot.color.g + (f->color.g - o.snapshot.color.g) * (1.f - pk));
                f->color.b = (unsigned char)(o.snapshot.color.b + (f->color.b - o.snapshot.color.b) * (1.f - pk));
                if (dx * dx + dy * dy + dz * dz > 1e-5f || std::fabs(dr) > 0.4f) shoved = true;
            }
            if (shoved) {
                // closest free worker plants both palms on it - reads as an
                // actual shove, not furniture sliding on its own
                CrewMan* shover = nullptr; float bd = 1e9f;
                for (auto& m : e.men) {
                    if (m.delay > 0.f) continue;
                    float mx = m.pos.x - e.workPos.x, mz = m.pos.z - e.workPos.z;
                    float dd = mx * mx + mz * mz;
                    if (dd < bd) { bd = dd; shover = &m; }
                }
                if (shover) shover->armRaise = std::max(shover->armRaise, 0.55f);
                if ((int)(e.workT * 1.2f) != (int)((e.workT - dt) * 1.2f))
                    audio.Play(AudioBank::Sfx::Creak, 0.2f);   // furniture scrape
            }
            if (e.workT >= e.workLen) {
                for (auto& o : e.outcomes) ad.ApplyResolveFx(o, office);
                // uncarryable spawned items (outlets, doors, heavy furniture)
                // are torn down on site - gone the moment the work ends
                if (!e.dismantleId.empty()) office.RemoveSpawned(e.dismantleId);
                if (e.outcomes[0].kind == "lights_office" || e.outcomes[0].kind == "lights_out")
                    blackoutT = std::max(blackoutT, 0.08f);   // lights surge back
                if (e.outcomes[0].kind == "empty_room") vs.WakeWaitingRoom();
                for (int i = 0; i < (int)e.men.size(); i++) {
                    CrewMan& m = e.men[i];
                    m.wp = 0; m.lift = 0; m.armRaise = 0;
                    m.delay = i * 0.7f;                       // file out one by one
                    m.carrying = !e.carryId.empty();
                }
                if (e.prop == PROP_BOX) e.prop = PROP_NONE;           // the replacement is placed
                e.propOut = false;
                e.phase = 2;
                audio.Play(AudioBank::Sfx::Click, 0.55f);
            }
        }
        if (e.phase == 2 && allDone) {
            e.phase = 3;
            TraceLog(LOG_INFO, "[crew] cleared out - %s", e.outcomes[0].id.c_str());
        }
    }
    std::erase_if(crew, [&](const CrewEvt& e) {
        if (e.phase != 3) return false;
        if (!e.carryId.empty()) office.RemoveSpawned(e.carryId);
        if (e.escortVisitor && vs.Cur().detained) {
            vs.CurMut().state = VState::Gone;   // marched out past the corner
            Notify("Security walked them out.");
        }
        return true;
    });
    if (crew.empty() && crewOpenedDoor) { doorOpen = false; crewOpenedDoor = false; }
}


void Game::DrawCrew() const {
    for (auto& e : crew) {
        // the lead man's prop - carried on the way in/out, planted while working
        if (!e.men.empty() && e.prop != PROP_NONE) {
            const CrewMan& lead = e.men[0];
            float yr = lead.yaw * DEG2RAD;
            Vector3 f{std::sin(yr), 0, std::cos(yr)}, r{std::cos(yr), 0, -std::sin(yr)};
            if (e.prop == PROP_LADDER) {
                if (e.propOut) {
                    float la = std::atan2(e.workPos.x - e.ladderPos.x, e.workPos.z - e.ladderPos.z) * RAD2DEG;
                    DrawLadder({e.ladderPos.x, 0, e.ladderPos.z}, la, 15.f);
                } else
                    DrawLadder({lead.pos.x + r.x * 0.34f, 0.45f, lead.pos.z + r.z * 0.34f}, lead.yaw, 66.f);
            } else if (e.prop == PROP_BOX) {
                if (e.propOut)
                    DrawCubeV({e.boxPos.x, e.boxPos.y + 0.11f, e.boxPos.z}, {0.3f, 0.22f, 0.26f}, Color{150, 118, 72, 255});
                else
                    DrawCubeV({lead.pos.x + f.x * 0.3f, 1.04f, lead.pos.z + f.z * 0.3f}, {0.3f, 0.22f, 0.26f}, Color{150, 118, 72, 255});
            } else if (e.prop == PROP_TOOLBOX) {
                if (e.propOut)
                    DrawCubeV({lead.pos.x + r.x * 0.4f, 0.11f, lead.pos.z + r.z * 0.4f}, {0.36f, 0.22f, 0.2f}, Color{160, 44, 40, 255});
                else
                    DrawCubeV({lead.pos.x + r.x * 0.33f, 0.58f, lead.pos.z + r.z * 0.33f}, {0.36f, 0.22f, 0.2f}, Color{160, 44, 40, 255});
            }
        }
        for (auto& m : e.men) {
            // walking gait while pathing, planted feet while working (armRaise
            // suppresses the gait automatically anyway)
            float wAmt = (e.phase == 1 || m.delay > 0.f) ? 0.f : 1.f;
            DrawHumanoid({m.pos.x, m.pos.y + m.lift, m.pos.z}, m.yaw, m.app,
                         false, m.animT, m.armRaise, 0.f, wAmt);
        }
    }
}
