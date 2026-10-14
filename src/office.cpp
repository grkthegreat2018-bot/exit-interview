#include "office.h"
#include <rlgl.h>

static void Box(Vector3 bottomCenter, Vector3 size, Color c) {
    DrawCubeV({bottomCenter.x, bottomCenter.y + size.y / 2, bottomCenter.z}, size, c);
}
static void CBox(Vector3 center, Vector3 size, Color c) { DrawCubeV(center, size, c); }

// LOD sphere - DrawSphere is 16x16 (~1.6k verts); decor doesn't need that
static void Sph(Vector3 c, float r, Color col) {
    DrawSphereEx(c, r, r > 0.08f ? 9 : 5, r > 0.08f ? 10 : 6, col);
}

// 64x64 radial gradient, alpha smoothstepped to 0 at the rim - built once
static Texture2D& BlobTex() {
    static Texture2D t{};
    if (t.id == 0) {
        const int S = 64;
        Image img = GenImageColor(S, S, BLANK);
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                float dx = (x + 0.5f) / S * 2.f - 1.f, dy = (y + 0.5f) / S * 2.f - 1.f;
                float a = 1.f - std::clamp((std::sqrt(dx * dx + dy * dy) - 0.30f) / 0.70f, 0.f, 1.f);
                a = a * a * (3.f - 2.f * a);
                ImageDrawPixel(&img, x, y, Color{0, 0, 0, (unsigned char)(a * 255.f)});
            }
        t = LoadTextureFromImage(img);
        UnloadImage(img);
        if (t.id) {
            SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
        }
    }
    return t;
}

void DrawBlobShadow(Vector3 center, float rx, float rz, float alpha, float yawDeg) {
    Texture2D t = BlobTex();
    if (t.id == 0) return;
    float yr = yawDeg * DEG2RAD, cs = std::cos(yr), sn = std::sin(yr);
    unsigned char a = (unsigned char)std::clamp(alpha * 255.f, 0.f, 255.f);
    const float px[4] = {-rx, rx, rx, -rx}, pz[4] = {-rz, -rz, rz, rz};
    const float tu[4] = {0, 1, 1, 0}, tv[4] = {0, 0, 1, 1};
    rlDisableDepthMask();   // coplanar blobs never z-fight; they just darken more
    rlBegin(RL_QUADS);
        rlSetTexture(t.id);
        rlColor4ub(255, 255, 255, a);
        rlNormal3f(0.f, 1.f, 0.f);
        for (int i = 0; i < 4; i++) {
            rlTexCoord2f(tu[i], tv[i]);
            rlVertex3f(center.x + px[i] * cs + pz[i] * sn, center.y,
                       center.z - px[i] * sn + pz[i] * cs);
        }
        for (int i = 3; i >= 0; i--) {   // both windings - visible from any side
            rlTexCoord2f(tu[i], tv[i]);
            rlVertex3f(center.x + px[i] * cs + pz[i] * sn, center.y,
                       center.z - px[i] * sn + pz[i] * cs);
        }
    rlEnd();
    rlSetTexture(0);
    rlEnableDepthMask();
}

void Office::Load(const json& j, const json& cfg) {
    items.clear();
    paths.clear();
    waitingSeats.clear();
    figurePositions.clear();
    intruderSpawns.clear();
    blindsOpen = false;
    waitingLightsOut = false;
    windowView = 0;
    windowBroken = false;
    hallLong = false;
    bounds.mn = JV3(j["office_room"]["min"]);
    bounds.mx = JV3(j["office_room"]["max"]);
    waitingBounds.mn = JV3(j["waiting_room"]["min"]);
    waitingBounds.mx = JV3(j["waiting_room"]["max"]);
    doorX = JV2(j["door_opening"]["x"]);
    doorZ = JF(j["door_opening"], "z", -5);
    windowX = JV2(j["window_opening"]["x"]);
    windowY = JV2(j["window_opening"]["y"]);
    windowZ = JF(j["window_opening"], "z", -5);
    seatCam = JV3(j["seat"]["camera_pos"]);
    lookMonitor = JV3(j["seat"]["look_monitor"]);
    lookVisitor = JV3(j["seat"]["look_visitor"]);
    visitorChairPos = JV3(j["visitor_chair_pos"]);
    for (auto& [name, arr] : j["paths"].items()) {
        for (auto& p : arr) paths[name].push_back(JV2(p));
    }
    for (auto& s : j["waiting_npc_seats"])
        waitingSeats.push_back({JV3(s["pos"]), JF(s, "yaw", 0), JF(s, "seat_h", 0.25f)});
    for (auto& [k, v] : j["figure_positions"].items())
        figurePositions[k] = JV3(v);
    for (auto& p : j["intruder_spawns"]) intruderSpawns.push_back(JV3(p));
    for (auto& f : j["furniture"]) {
        Furni it;
        it.id = JS(f, "id");
        it.type = JS(f, "type");
        it.ft = TypeOf(it.type);
        it.pos = JV3(f["pos"]);
        it.size = JV3(f["size"], {0.5f, 0.5f, 0.5f});
        it.rotY = JF(f, "rot", 0);
        it.color = JC(f["color"], WHITE);
        it.solid = JB(f, "solid", false);
        it.interact = JS(f, "interact");
        it.room = JS(f, "room", "office");
        it.eyes = JB(f, "eyes", false);
        it.art = JS(f, "art");
        items.push_back(it);
    }
    if (std::getenv("EI_ITEMS"))
        for (auto& it : items)
            TraceLog(LOG_INFO, "ITEM %s %s pos(%.2f %.2f %.2f) size(%.2f %.2f %.2f) rot %.0f room %s",
                     it.id.c_str(), it.type.c_str(), it.pos.x, it.pos.y, it.pos.z,
                     it.size.x, it.size.y, it.size.z, it.rotY, it.room.c_str());
    if (cfg.contains("colors")) {
        wallC = JC(cfg["colors"]["wall"], wallC);
        floorC = JC(cfg["colors"]["floor"], floorC);
        ceilC = JC(cfg["colors"]["ceiling"], ceilC);
        monitorGlow = JC(cfg["colors"]["monitor_glow"], monitorGlow);
    }
}

void Office::ApplyMutation(const std::string& targetId, const json& m) {
    if (m.contains("spawn")) {
        auto& s = m["spawn"];
        Furni it;
        it.id = JS(s, "id", "anom_" + std::to_string(RandInt(0, 99999)));
        it.type = JS(s, "type");
        it.ft = TypeOf(it.type);
        it.pos = JV3(s["pos"]);
        it.size = JV3(s["size"], {0.5f, 0.5f, 0.5f});
        it.rotY = JF(s, "rot", 0);
        it.color = JC(s["color"], GRAY);
        it.solid = JB(s, "solid", false);
        it.interact = JS(s, "interact");
        it.room = JS(s, "room", "office");
        it.art = JS(s, "art");
        it.eyes = JB(s, "eyes", false);
        it.stopped = JB(s, "stopped", false);
        it.backwards = JB(s, "backwards", false);
        items.push_back(it);
        return;
    }
    Furni* f = Find(targetId);
    if (!f) return;
    if (m.contains("translate")) {
        Vector3 t = JV3(m["translate"]);
        f->pos.x += t.x; f->pos.y += t.y; f->pos.z += t.z;
    }
    if (m.contains("color")) f->color = JC(m["color"], f->color);
    if (m.contains("hidden")) f->hidden = m["hidden"].get<bool>();
    if (m.contains("scale")) {
        Vector3 s = JV3(m["scale"], {1, 1, 1});
        f->size.x *= s.x; f->size.y *= s.y; f->size.z *= s.z;
    }
    if (m.contains("stopped")) f->stopped = m["stopped"].get<bool>();
    if (m.contains("backwards")) f->backwards = m["backwards"].get<bool>();
    if (m.contains("pos_set")) f->pos = JV3(m["pos_set"]);
    if (m.contains("rot_set")) f->rotY = JF(m, "rot_set", f->rotY);
    if (m.contains("eyes")) f->eyes = m["eyes"].get<bool>();
    if (m.contains("art")) f->art = JS(m, "art");            // wall decor swaps its image
}

void Office::ApplyMutationAnimated(const std::string& targetId, const json& m, float dur) {
    Furni* f = Find(targetId);
    if (!f || m.contains("spawn")) { ApplyMutation(targetId, m); return; }
    Vector3 preP = f->pos;
    float preR = f->rotY;
    ApplyMutation(targetId, m);
    // only positional/rotational moves get the slide - color/eyes/etc. are
    // still instant (there's nothing to watch moving)
    bool moved = std::fabs(f->pos.x - preP.x) > 0.01f || std::fabs(f->pos.y - preP.y) > 0.01f ||
                 std::fabs(f->pos.z - preP.z) > 0.01f || std::fabs(f->rotY - preR) > 0.5f;
    if (!moved) return;
    MoveGlide g;
    g.id = targetId; g.fromP = preP; g.toP = f->pos;
    g.fromR = preR; g.toR = f->rotY; g.dur = dur;
    f->pos = preP; f->rotY = preR;   // rewind - Update() slides it into place
    moveGlides.push_back(g);
}

void Office::Update(float dt) {
    tick++;
    for (auto& g : moveGlides) {
        g.t += dt;
        float u = std::clamp(g.t / g.dur, 0.f, 1.f);
        // ease-out-back: fast slide that overshoots ~10% then settles - the
        // "jolted into place" read, paired with the Screech sfx by the Game
        const float c1 = 1.70158f, c3 = c1 + 1.f;
        float w = u - 1.f;
        float k = 1.f + c3 * w * w * w + c1 * w * w;
        if (Furni* f = Find(g.id)) {
            f->pos = {g.fromP.x + (g.toP.x - g.fromP.x) * k,
                      g.fromP.y + (g.toP.y - g.fromP.y) * k,
                      g.fromP.z + (g.toP.z - g.fromP.z) * k};
            f->rotY = g.fromR + (g.toR - g.fromR) * k;
        }
    }
    std::erase_if(moveGlides, [](const MoveGlide& g) { return g.t >= g.dur; });
}

void Office::RemoveSpawned(const std::string& id) {
    items.erase(std::remove_if(items.begin(), items.end(),
                [&](const Furni& f) { return f.id == id; }), items.end());
}

Furni* Office::Find(const std::string& id) {
    for (auto& f : items) if (f.id == id) return &f;
    return nullptr;
}
const Furni* Office::FindC(const std::string& id) const {
    for (auto& f : items) if (f.id == id) return &f;
    return nullptr;
}

const std::vector<AABB>& Office::Solids() const {
    if (solidsTick != tick) {
        solidsTick = tick;
        solidsCache.clear();
        for (auto& f : items) {
            if (!f.solid || f.hidden || f.room != "office") continue;
            // account for rotY in the AABB
            float c = std::cos(f.rotY * DEG2RAD), s = std::sin(f.rotY * DEG2RAD);
            float hx = (std::fabs(f.size.x * c) + std::fabs(f.size.z * s)) / 2;
            float hz = (std::fabs(f.size.x * s) + std::fabs(f.size.z * c)) / 2;
            solidsCache.push_back({{f.pos.x - hx, f.pos.y, f.pos.z - hz},
                                   {f.pos.x + hx, f.pos.y + f.size.y, f.pos.z + hz}});
        }
    }
    return solidsCache;
}

Vector3 Office::MonitorScreenCenter() const {
    const Furni* m = FindC("monitor");
    if (!m) return {0.55f, 1.275f, 1.0f};
    return {m->pos.x, m->pos.y + 0.14f + m->size.y / 2, m->pos.z};
}

// where the visitor's hips actually sit: chair center nudged toward the
// chair's facing so the backrest (local -z) stays behind the torso instead
// of clipping through it
Vector3 Office::VisitorSitPos() const {
    float rot = 0;
    if (const Furni* ch = FindC("visitor_chair")) rot = ch->rotY;
    float yr = rot * DEG2RAD;
    return {visitorChairPos.x + std::sin(yr) * 0.10f, visitorChairPos.y,
            visitorChairPos.z + std::cos(yr) * 0.10f};
}

bool Office::MonitorUV(Vector3 ro, Vector3 rd, Vector2& uv) const {
    const Furni* m = FindC("monitor");
    if (!m) return false;
    float cz = m->pos.z + m->size.z / 2 + 0.013f;
    if (std::fabs(rd.z) < 1e-6f) return false;
    float t = (cz - ro.z) / rd.z;
    if (t <= 0 || t > 6.f) return false;
    float hx = ro.x + rd.x * t, hy = ro.y + rd.y * t;
    float w = (m->size.x - 0.07f) / 2, h = (m->size.y - 0.07f) / 2;
    float cx = m->pos.x, cy = m->pos.y + 0.14f + m->size.y / 2;
    float u = (hx - (cx - w)) / (2 * w), vv = (cy + h - hy) / (2 * h);
    if (u < 0 || u > 1 || vv < 0 || vv > 1) return false;
    uv = {u, vv};
    return true;
}

bool Office::MonitorQuad(Vector3& center, float& w, float& h, float& z) const {
    const Furni* m = FindC("monitor");
    if (!m) return false;
    center = {m->pos.x, m->pos.y + 0.14f + m->size.y / 2, m->pos.z};
    w = (m->size.x - 0.07f) / 2;
    h = (m->size.y - 0.07f) / 2;
    z = m->pos.z + m->size.z / 2 + 0.013f;
    return true;
}

// ---------- drawing ----------

void Office::Draw() const {
    DrawRooms();
    if (DevEnv("EI_NOFURN")) return;
    int i = 0;
    const char* cut = DevEnv("EI_FCUT");
    for (auto& f : items) {
        int idx = i++;
        if (!f.hidden && (!cut || idx < std::atoi(cut))) DrawItem(f);
    }
    DrawCables();   // world-space cords strung between furniture
    // fake contact AO: soft dark pools under anything standing on the floor
    for (auto& f : items) {
        if (f.hidden || f.pos.y > 0.05f) continue;             // floor-standing only
        if (f.ft == FT::Door || f.ft == FT::Rug) continue;     // hinged panel / decal
        float rx = f.size.x / 2 + 0.16f, rz = f.size.z / 2 + 0.16f;
        if (rx < 0.22f || rz < 0.22f) continue;                // thin wall plates
        DrawBlobShadow({f.pos.x, 0.015f, f.pos.z}, rx, rz, 0.40f, f.rotY);
    }
}

// googly eyes grow on the face toward the room: a dark socket, a white
// dome, and a loose pupil dome that rattles and slides TOWARD eyeTarget.
// The pupil's offset is always biased out along the face normal so it
// can never sink through the dome or clip into the furniture behind it.
// UNLIT pass (drawn after LitOff): anomaly eyes stay stark pale in dark
// corners - lit spheres read as black blobs against a dark wall.
void Office::DrawEyes() const {
    for (auto& f : items) {
        if (!f.eyes || f.hidden) continue;
        // outward face: flat/laid-down -> top (+y), thin x -> +-x wall face,
        // thin z -> +-z wall face, else the rotated front
        Vector3 n;
        if (f.size.y < f.size.x * 0.5f && f.size.y < f.size.z * 0.5f)
            n = {0, 1, 0};
        else if (f.size.x <= f.size.z && f.size.x < f.size.y * 0.6f)
            n = {f.pos.x > 0 ? -1.f : 1.f, 0, 0};
        else if (f.size.z <= f.size.x && f.size.z < f.size.y * 0.6f)
            n = {0, 0, f.pos.z > 0 ? -1.f : 1.f};
        else {
            float ry = f.rotY * DEG2RAD;
            n = {std::sin(ry), 0, std::cos(ry)};
        }
        Vector3 rt{n.z, 0, -n.x};                       // lateral axis across the face
        if (std::fabs(n.y) > 0.7f) rt = {1, 0, 0};
        // eye size comes from the FACE dims, never the depth - flat wall items
        // (clock, photo) used to get millimetre eyes invisible past two metres
        float surf = (std::fabs(n.x) * f.size.x + std::fabs(n.y) * f.size.y +
                      std::fabs(n.z) * f.size.z) * 0.5f + 0.004f;
        float faceW = std::fabs(rt.x) * f.size.x + std::fabs(rt.z) * f.size.z;
        float faceV = std::fabs(n.y) > 0.5f ? f.size.z : f.size.y;
        float er = std::clamp(std::min(faceW, faceV) * 0.19f, 0.02f, 0.10f);
        float lat = std::clamp(faceW * 0.20f, er * 0.9f,
                               std::max(faceW * 0.5f - er * 0.7f, er * 0.9f));
        float ey;
        if (std::fabs(n.y) > 0.5f)          ey = f.pos.y + f.size.y + er * 0.35f;
        else if (f.ft == FT::WallClock)    ey = f.pos.y + f.size.y * 0.36f;   // under the hands
        else if (f.ft == FT::Couch)         ey = f.pos.y + f.size.y * 0.46f;   // between cushions
        else if (f.ft == FT::Poster)        ey = f.pos.y + f.size.y * 0.56f;   // where a face would be
        else if (f.ft == FT::Monitor)       ey = f.pos.y + f.size.y + er * 0.25f;  // peering over the top bezel
        else                                ey = f.pos.y + f.size.y * 0.60f;
        float t = (float)GetTime();
        int ei = 0;
        for (int s = -1; s <= 1; s += 2, ei++) {
            Vector3 ep{f.pos.x + n.x * surf + rt.x * lat * s,
                       ey, f.pos.z + n.z * surf + rt.z * lat * s};
            // dark socket: strictly smaller than the dome and set back along
            // -n, so it reads as a shadowed root and can never swallow it
            Sph({ep.x - n.x * er * 0.45f, ep.y, ep.z - n.z * er * 0.45f},
                er * 0.80f, Color{16, 12, 12, 255});
            Sph(ep, er, Color{235, 228, 212, 255});
            // direction toward the look target, split into normal/lateral
            Vector3 look{eyeTarget.x - ep.x, eyeTarget.y - ep.y, eyeTarget.z - ep.z};
            float ll = std::sqrt(look.x * look.x + look.y * look.y + look.z * look.z);
            if (ll > 1e-4f) { look.x /= ll; look.y /= ll; look.z /= ll; }
            float dn = look.x * n.x + look.y * n.y + look.z * n.z;
            Vector3 latv{look.x - n.x * dn, look.y - n.y * dn, look.z - n.z * dn};
            float lv = std::sqrt(latv.x * latv.x + latv.y * latv.y + latv.z * latv.z);
            if (lv > 1e-4f) { latv.x /= lv; latv.y /= lv; latv.z /= lv; }
            // googly rattle: the pupil is loose in its dome - it jiggles on
            // its own and swings harder the closer the target gets
            float w = t * (3.4f + s * 0.9f) + ei * 1.7f;
            Vector3 jx{rt.x * std::sin(w) * 0.09f, std::cos(w * 1.6f) * 0.07f,
                       rt.z * std::sin(w) * 0.09f};
            float chase = std::clamp(1.2f - ll * 0.12f, 0.35f, 1.f);
            Vector3 po{ep.x + n.x * er * 0.55f + (latv.x * chase + jx.x) * er * 0.42f,
                       ep.y + n.y * er * 0.55f + (latv.y * chase + jx.y) * er * 0.42f,
                       ep.z + n.z * er * 0.55f + (latv.z * chase + jx.z) * er * 0.42f};
            Sph(po, er * 0.42f, Color{10, 8, 10, 255});
        }
    }
}

void Office::DrawGlass() const {
    if (!blindsOpen && !windowBroken) return;
    float cx = (windowX.x + windowX.y) / 2, cy = (windowY.x + windowY.y) / 2;
    float ww = windowX.y - windowX.x, wh = windowY.y - windowY.x;
    if (!windowBroken) {
        CBox({cx, cy, windowZ}, {ww, wh, 0.02f}, Color{140, 170, 190, 60});
        return;
    }
    // shattered pane: jagged shards cling to the frame, debris on the floor,
    // and the hole itself is just... open
    Color shard{190, 215, 225, 110};
    auto tri = [&](Vector3 a, Vector3 b, Vector3 c) {
        DrawTriangle3D(a, b, c, shard);
        DrawTriangle3D(c, b, a, shard);   // both windings - thin glass
    };
    float x0 = windowX.x, x1 = windowX.y, y0 = windowY.x, y1 = windowY.y;
    float z = windowZ;
    // shards radiating inward from each frame edge (deterministic jaggedness)
    const float jag[6] = {0.10f, 0.26f, 0.07f, 0.22f, 0.15f, 0.30f};
    for (int i = 0; i < 6; i++) {
        float t0 = i / 6.f, t1 = (i + 0.55f) / 6.f;
        float g = jag[i];
        // top edge: shard triangles hanging down
        tri({x0 + t0 * ww, y1, z}, {x0 + t1 * ww, y1, z},
            {x0 + (t0 + t1) / 2 * ww, y1 - g * wh * 0.5f, z});
        // bottom edge: shards standing up
        tri({x0 + t0 * ww, y0, z}, {x0 + t1 * ww, y0, z},
            {x0 + (t0 + t1) / 2 * ww, y0 + g * wh * 0.45f, z});
    }
    for (int i = 0; i < 4; i++) {
        float t0 = i / 4.f, t1 = (i + 0.6f) / 4.f;
        // left/right edges
        tri({x0, y0 + t0 * wh, z}, {x0, y0 + t1 * wh, z},
            {x0 + jag[i] * ww * 0.5f, y0 + (t0 + t1) / 2 * wh, z});
        tri({x1, y0 + t0 * wh, z}, {x1, y0 + t1 * wh, z},
            {x1 - jag[i] * ww * 0.5f, y0 + (t0 + t1) / 2 * wh, z});
    }
    // glass debris scattered on the floor just inside
    Color debris{170, 195, 205, 150};
    for (int i = 0; i < 10; i++) {
        float dx = ((i * 37 % 100) / 100.f - 0.5f) * ww * 1.3f;
        float dz = 0.15f + ((i * 53 % 100) / 100.f) * 0.8f;
        float s = 0.02f + (i % 3) * 0.012f;
        CBox({cx + dx, s * 0.3f, z + dz}, {s, s * 0.6f, s}, debris);
    }
    // crack lines across the surviving corners
    DrawLine3D({x0, y0, z}, {x0 + ww * 0.35f, y0 + wh * 0.3f, z}, shard);
    DrawLine3D({x1, y1, z}, {x1 - ww * 0.3f, y1 - wh * 0.4f, z}, shard);
}

void Office::DrawRooms() const {
    float h = bounds.mx.y;                 // office height 4
    float wh = waitingBounds.mx.y;         // waiting room height
    float wallT = 0.16f;
    float wz = doorZ;                      // shared wall plane z = -5

    // floors / ceilings
    DrawPlane({0, 0, 0}, {bounds.mx.x - bounds.mn.x, bounds.mx.z - bounds.mn.z}, floorC);
    if (!DevEnv("EI_NOCEIL"))
        DrawPlane({0, h, 0}, {bounds.mx.x - bounds.mn.x, bounds.mx.z - bounds.mn.z}, ceilC);
    float wrCz = (waitingBounds.mn.z + waitingBounds.mx.z) / 2;
    DrawPlane({0, 0.001f, wrCz}, {waitingBounds.mx.x - waitingBounds.mn.x,
              waitingBounds.mx.z - waitingBounds.mn.z}, waitingLightsOut ? Color{16, 15, 18, 255} : Color{58, 55, 58, 255});
    DrawPlane({0, wh, wrCz}, {waitingBounds.mx.x - waitingBounds.mn.x,
              waitingBounds.mx.z - waitingBounds.mn.z}, ceilC);

    // office side walls / south wall
    Box({bounds.mn.x, 0, 0}, {wallT, h, bounds.mx.z - bounds.mn.z}, wallC);
    Box({bounds.mx.x, 0, 0}, {wallT, h, bounds.mx.z - bounds.mn.z}, wallC);
    Box({0, 0, bounds.mx.z}, {bounds.mx.x - bounds.mn.x, h, wallT}, wallC);

    // north wall segments (door + window openings)
    auto wallSeg = [&](float x0, float x1, float y0, float y1) {
        CBox({(x0 + x1) / 2, (y0 + y1) / 2, wz}, {x1 - x0, y1 - y0, wallT}, wallC);
    };
    wallSeg(bounds.mn.x, doorX.x, 0, h);                 // left of door
    wallSeg(doorX.x, doorX.y, 2.5f, h);                  // above door
    wallSeg(doorX.y, windowX.x, 0, h);                   // door..window
    wallSeg(windowX.x, windowX.y, 0, windowY.x);         // below window
    wallSeg(windowX.x, windowX.y, windowY.y, h);         // above window
    wallSeg(windowX.y, bounds.mx.x, 0, h);               // right of window

    // door frame: jambs + header so the opening reads as a doorway
    Color trim{58, 52, 56, 255};
    CBox({doorX.x - 0.045f, 1.25f, wz}, {0.09f, 2.5f, wallT + 0.07f}, trim);
    CBox({doorX.y + 0.045f, 1.25f, wz}, {0.09f, 2.5f, wallT + 0.07f}, trim);
    CBox({(doorX.x + doorX.y) / 2, 2.545f, wz}, {doorX.y - doorX.x + 0.18f, 0.09f, wallT + 0.07f}, trim);

    // window frame: same jamb treatment + head + a sill ledge into the office
    float wcx = (windowX.x + windowX.y) / 2, wcy = (windowY.x + windowY.y) / 2;
    float wwd = windowX.y - windowX.x, whg = windowY.y - windowY.x;
    CBox({windowX.x - 0.045f, wcy, wz}, {0.09f, whg, wallT + 0.07f}, trim);
    CBox({windowX.y + 0.045f, wcy, wz}, {0.09f, whg, wallT + 0.07f}, trim);
    CBox({wcx, windowY.y + 0.045f, wz}, {wwd + 0.18f, 0.09f, wallT + 0.07f}, trim);
    CBox({wcx, windowY.x - 0.02f, wz}, {wwd + 0.18f, 0.09f, wallT + 0.07f}, trim);
    CBox({wcx, windowY.x - 0.01f, wz + 0.10f}, {wwd + 0.2f, 0.05f, 0.24f}, trim);   // sill ledge

    // baseboards + crown molding: thin darker trim at the floor and ceiling
    // lines of every wall, broken at the door opening (crown bridges over it)
    Color base{50, 46, 50, 255};
    float ibx = bounds.mn.x + 0.09f, obx = bounds.mx.x - 0.09f;
    auto bb = [&](float cx, float y, float cz, float w, float d, Color col) {
        CBox({cx, y, cz}, {w, 0.09f, d}, col);
    };
    bb(ibx, 0.045f, 0, 0.02f, bounds.mx.z - bounds.mn.z, base);
    bb(obx, 0.045f, 0, 0.02f, bounds.mx.z - bounds.mn.z, base);
    bb(0, 0.045f, bounds.mx.z - 0.09f, bounds.mx.x - bounds.mn.x, 0.02f, base);
    bb((bounds.mn.x + doorX.x) / 2, 0.045f, wz + 0.09f, doorX.x - bounds.mn.x, 0.02f, base);
    bb((doorX.y + bounds.mx.x) / 2, 0.045f, wz + 0.09f, bounds.mx.x - doorX.y, 0.02f, base);
    bb(ibx, h - 0.045f, 0, 0.02f, bounds.mx.z - bounds.mn.z, base);
    bb(obx, h - 0.045f, 0, 0.02f, bounds.mx.z - bounds.mn.z, base);
    bb(0, h - 0.045f, bounds.mx.z - 0.09f, bounds.mx.x - bounds.mn.x, 0.02f, base);
    bb(0, h - 0.045f, wz + 0.09f, bounds.mx.x - bounds.mn.x, 0.02f, base);

    // (glass drawn separately at end of frame - DrawGlass)

    // waiting room outer walls
    float wnz = waitingBounds.mn.z;
    Color wrW = waitingLightsOut ? Color{26, 25, 29, 255} : Color{78, 76, 82, 255};
    Box({waitingBounds.mn.x, 0, wrCz}, {wallT, wh, waitingBounds.mx.z - waitingBounds.mn.z}, wrW);
    Box({waitingBounds.mx.x, 0, wrCz}, {wallT, wh, waitingBounds.mx.z - waitingBounds.mn.z}, wrW);
    // north wall of waiting room with hall door opening x 5.0..6.2
    CBox({(waitingBounds.mn.x + 5.0f) / 2, wh / 2, wnz}, {5.0f - waitingBounds.mn.x, wh, wallT}, wrW);
    CBox({(6.2f + waitingBounds.mx.x) / 2, wh / 2, wnz}, {waitingBounds.mx.x - 6.2f, wh, wallT}, wrW);
    CBox({5.6f, 2.4f + (wh - 2.4f) / 2, wnz}, {1.2f, wh - 2.4f, wallT}, wrW);

    // waiting room baseboards + crown (south run broken at the hall door)
    Color wtb = waitingLightsOut ? Color{20, 19, 23, 255} : Color{48, 46, 52, 255};
    bb(ibx, 0.045f, wrCz, 0.02f, waitingBounds.mx.z - waitingBounds.mn.z, wtb);
    bb(obx, 0.045f, wrCz, 0.02f, waitingBounds.mx.z - waitingBounds.mn.z, wtb);
    bb((waitingBounds.mn.x + 5.0f) / 2, 0.045f, wnz + 0.09f, 5.0f - waitingBounds.mn.x, 0.02f, wtb);
    bb((6.2f + waitingBounds.mx.x) / 2, 0.045f, wnz + 0.09f, waitingBounds.mx.x - 6.2f, 0.02f, wtb);
    bb((bounds.mn.x + doorX.x) / 2, 0.045f, wz - 0.09f, doorX.x - bounds.mn.x, 0.02f, wtb);
    bb((doorX.y + bounds.mx.x) / 2, 0.045f, wz - 0.09f, bounds.mx.x - doorX.y, 0.02f, wtb);
    bb(ibx, wh - 0.045f, wrCz, 0.02f, waitingBounds.mx.z - waitingBounds.mn.z, wtb);
    bb(obx, wh - 0.045f, wrCz, 0.02f, waitingBounds.mx.z - waitingBounds.mn.z, wtb);
    bb(0, wh - 0.045f, wnz + 0.09f, waitingBounds.mx.x - waitingBounds.mn.x, 0.02f, wtb);
    bb(0, wh - 0.045f, wz - 0.09f, waitingBounds.mx.x - waitingBounds.mn.x, 0.02f, wtb);
    // hall glow beyond the door - or, when the hall_long anomaly is live, a
    // corridor that plainly goes on much too far
    if (hallLong) {
        DrawPlane({5.6f, 0.002f, wnz - 4.f}, {1.2f, 8.f}, Color{30, 28, 33, 255});
        for (int i = 0; i < 6; i++) {
            float fz = wnz - 0.4f - i * 1.5f;
            float dim = 1.f - i * 0.15f;
            Color fc{(unsigned char)(60 * dim), (unsigned char)(55 * dim), (unsigned char)(62 * dim), 255};
            CBox({5.6f, 2.42f, fz}, {1.25f, 0.08f, 0.08f}, fc);
            CBox({5.02f, 1.2f, fz}, {0.08f, 2.4f, 0.08f}, fc);
            CBox({6.18f, 1.2f, fz}, {0.08f, 2.4f, 0.08f}, fc);
        }
    } else {
        // stub hallway behind the door: a short L-turn east so anyone leaving
        // through the hall door walks around a corner before they despawn
        // instead of vanishing in plain sight through the doorway
        Color hc{52, 48, 54, 255}, hf{46, 42, 46, 255};
        if (waitingLightsOut) { hc = {16, 15, 18, 255}; hf = {13, 12, 15, 255}; }
        const float hw = 2.6f;
        // leg A (straight ahead from the door): floor + ceiling
        DrawPlane({5.6f, 0.002f, -11.95f}, {1.2f, 2.0f}, hf);
        DrawPlane({5.6f, hw, -11.95f}, {1.2f, 2.0f}, hc);
        // leg B (the turn): floor + ceiling
        DrawPlane({8.2f, 0.002f, -12.45f}, {4.0f, 1.0f}, hf);
        DrawPlane({8.2f, hw, -12.45f}, {4.0f, 1.0f}, hc);
        // west wall continues the door jamb to the far corner
        CBox({5.0f, hw / 2, -11.9f}, {0.14f, hw, 2.0f}, hc);
        // east wall runs only a metre deep - past that the corridor turns east
        CBox({6.2f, hw / 2, -11.45f}, {0.14f, hw, 1.1f}, hc);
        // far wall behind both legs; south wall encloses leg B's room side
        CBox({7.6f, hw / 2, -12.9f}, {5.2f, hw, 0.14f}, hc);
        CBox({8.2f, hw / 2, -12.0f}, {4.0f, hw, 0.14f}, hc);
        // distant end cap down leg B - reads as the hall carrying on in the dark
        CBox({10.2f, hw / 2, -12.45f}, {0.14f, hw, 1.0f}, hc);
    }

    // window anomaly vistas - drawn on the waiting-room side, covering the
    // opening, so looking through the glass shows the wrong place
    if (windowView != 0) {
        float wx = (windowX.x + windowX.y) / 2, wy = (windowY.x + windowY.y) / 2;
        float ww = windowX.y - windowX.x + 0.6f, whh = windowY.y - windowY.x + 0.6f;
        if (windowView == 1) {
            // the waiting room, upside down: dark backdrop + floor overhead +
            // bench silhouettes hanging off it
            CBox({wx, wy, wz - 2.4f}, {ww, whh, 0.1f}, Color{14, 12, 16, 255});
            DrawPlane({wx, wy + whh / 2 - 0.05f, wz - 1.2f}, {ww, 1.8f},
                      waitingLightsOut ? Color{30, 28, 32, 255} : Color{58, 55, 58, 255});
            for (int i = -1; i <= 1; i++)
                CBox({wx + i * (ww * 0.3f), wy + whh / 2 - 0.5f, wz - 1.1f},
                     {0.5f, 0.9f, 0.4f}, Color{45, 40, 46, 255});
            // upside-down door glow at the top edge
            CBox({wx + ww * 0.35f, wy + whh / 2 - 0.6f, wz - 1.6f}, {0.7f, 1.2f, 0.05f},
                 Color{46, 42, 48, 255});
        } else {
            // void: matte black + faint distant specks + one red slit
            CBox({wx, wy, wz - 2.4f}, {ww, whh, 0.1f}, Color{4, 4, 7, 255});
            for (int i = 0; i < 9; i++) {
                float sx = wx - ww / 2 + 0.15f + (i * 37 % 100) / 100.f * (ww - 0.3f);
                float sy = wy - whh / 2 + 0.12f + (i * 53 % 100) / 100.f * (whh - 0.25f);
                Sph({sx, sy, wz - 2.32f}, 0.008f + (i % 3) * 0.004f, Color{180, 190, 200, 255});
            }
            CBox({wx + ww * 0.18f, wy - whh * 0.12f, wz - 2.33f}, {0.5f, 0.05f, 0.02f},
                 Color{120, 30, 26, 255});
        }
    }
}

void Office::DrawShadowFigure(Vector3 feet, float yawDeg, float hs, float armRaise) {
    DrawBlobShadow({feet.x, 0.015f, feet.z}, 0.50f * hs, 0.50f * hs, 0.45f, yawDeg);
    float yr = yawDeg * DEG2RAD;
    Vector3 f{std::sin(yr), 0, std::cos(yr)};
    Vector3 rt{std::cos(yr), 0, -std::sin(yr)};
    Color body{8, 8, 12, 255};
    // legs: two gaunt tapered stalks so the silhouette stands, not floats
    for (int s = -1; s <= 1; s += 2)
        DrawCylinderEx({feet.x + rt.x * 0.085f * s * hs, feet.y,
                        feet.z + rt.z * 0.085f * s * hs},
                       {feet.x + rt.x * 0.115f * s * hs, feet.y + 0.62f * hs,
                        feet.z + rt.z * 0.115f * s * hs},
                       0.060f * hs, 0.095f * hs, 7, body);
    DrawCylinderEx({feet.x, feet.y + 0.5f * hs, feet.z}, {feet.x, feet.y + 1.35f * hs, feet.z}, 0.16f * hs, 0.14f * hs, 8, body);
    // shoulder caps + long hanging arms with knuckled hand nubs; the right
    // arm is reserved for the banging animation below
    for (int s = -1; s <= 1; s += 2)
        Sph({feet.x + rt.x * 0.185f * s * hs, feet.y + 1.33f * hs,
             feet.z + rt.z * 0.185f * s * hs}, 0.078f * hs, body);
    {
        Vector3 sh{feet.x - rt.x * 0.19f * hs, feet.y + 1.30f * hs, feet.z - rt.z * 0.19f * hs};
        Vector3 hand{feet.x - rt.x * 0.225f * hs + f.x * 0.06f * hs, feet.y + 0.56f * hs,
                     feet.z - rt.z * 0.225f * hs + f.z * 0.06f * hs};
        DrawCylinderEx(sh, hand, 0.042f * hs, 0.034f * hs, 6, body);
        Sph(hand, 0.046f * hs, body);
    }
    if (armRaise <= 0.02f) {   // right arm hangs until it pounds
        Vector3 sh{feet.x + rt.x * 0.19f * hs, feet.y + 1.30f * hs, feet.z + rt.z * 0.19f * hs};
        Vector3 hand{feet.x + rt.x * 0.225f * hs + f.x * 0.05f * hs, feet.y + 0.56f * hs,
                     feet.z + rt.z * 0.225f * hs + f.z * 0.05f * hs};
        DrawCylinderEx(sh, hand, 0.042f * hs, 0.034f * hs, 6, body);
        Sph(hand, 0.046f * hs, body);
    }
    // head sits a touch forward, neck implied - reads as leaning toward you
    Sph({feet.x + f.x * 0.018f * hs, feet.y + 1.5f * hs, feet.z + f.z * 0.018f * hs}, 0.11f * hs, body);
    // faint eyes
    Vector3 e1{feet.x + f.x * 0.1f * hs - f.z * 0.045f, feet.y + 1.53f * hs, feet.z + f.z * 0.1f * hs + f.x * 0.045f};
    Vector3 e2{feet.x + f.x * 0.1f * hs + f.z * 0.045f, feet.y + 1.53f * hs, feet.z + f.z * 0.1f * hs - f.x * 0.045f};
    Sph(e1, 0.013f * hs, Color{220, 220, 210, 200});
    Sph(e2, 0.013f * hs, Color{220, 220, 210, 200});
    // banging arm: pendulums between hanging and a forward overhead slam -
    // paired with BangerStrike()'s knock audio it reads as pounding the door
    if (armRaise > 0.02f) {
        Vector3 sh{feet.x + rt.x * 0.20f * hs, feet.y + 1.30f * hs, feet.z + rt.z * 0.20f * hs};
        Vector3 lo{sh.x + f.x * 0.06f + rt.x * 0.05f, feet.y + 0.55f * hs,
                   sh.z + f.z * 0.06f + rt.z * 0.05f};
        Vector3 hi{sh.x + f.x * 0.58f - rt.x * 0.04f, feet.y + 1.46f * hs,
                   sh.z + f.z * 0.58f - rt.z * 0.04f};
        Vector3 hand{lo.x + (hi.x - lo.x) * armRaise, lo.y + (hi.y - lo.y) * armRaise,
                     lo.z + (hi.z - lo.z) * armRaise};
        rlNormal3f(f.x, 0.3f, f.z);
        DrawCylinderEx(sh, hand, 0.050f * hs, 0.042f * hs, 6, body);
        Sph(hand, 0.052f * hs, body);
    }
}

// cord between two world points, sagging on a parabolic dip - 6 short
// DrawCylinderEx segments keep it cheap. Called from DrawCables only; this is
// WORLD space, not DrawItem local space, so live furni pos stretches cords
static void Cord(Vector3 a, Vector3 b, float sag, Color c) {
    const int N = 6;
    Vector3 prev = a;
    for (int i = 1; i <= N; i++) {
        float t = (float)i / N;
        Vector3 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t};
        p.y -= sag * 4.f * t * (1.f - t);   // deepest mid-run
        DrawCylinderEx(prev, p, 0.007f, 0.007f, 5, c);
        prev = p;
    }
}

void Office::DrawCables() const {
    Color rubber{22, 22, 26, 255};
    const Furni* pc = FindC("pc_tower");
    const Furni* fo = FindC("outlet_floor");
    const Furni* mo = FindC("monitor");
    if (pc && fo && !pc->hidden && !fo->hidden) {
        // power cord: out the tower's left-rear corner, hugging the floor on
        // its way to the receptacle plate - routed so it never crosses the
        // desk legs or the modesty panel
        Cord({pc->pos.x - pc->size.x / 2 - 0.004f, 0.09f,
              pc->pos.z - pc->size.z / 2 + 0.08f},
             {fo->pos.x, fo->pos.y + 0.02f, fo->pos.z}, 0.05f, rubber);
    }
    if (pc && mo && !pc->hidden && !mo->hidden) {
        // data cable: off the tower top, then a long sagging run hugging the
        // slab's underside to a point directly below the monitor, and only a
        // short near-vertical stub up through the desktop (reads as a grommet
        // feed) - never a diagonal that visibly pierces the work surface
        Vector3 up{mo->pos.x - 0.15f, mo->pos.y + 0.005f, mo->pos.z - mo->size.z / 2};
        Vector3 under{up.x, 0.70f, up.z};
        Cord({pc->pos.x + 0.04f, pc->pos.y + pc->size.y - 0.03f, pc->pos.z},
             under, 0.10f, rubber);
        Cord(under, up, 0.006f, rubber);
    }
    // keyboard + mouse tails: snake back across the desktop clear of the
    // monitor footprint, drop through a shared grommet hole behind it, then
    // hug the slab's underside into the tower top - endpoints always land ON
    // hardware, never mid-air
    const Furni* kb = FindC("keyboard");
    const Furni* ms = FindC("mouse");
    // Desktop runs: endpoints sit ~2cm above the slab and sag is capped so
    // the parabolic dip never reaches the work surface (no mid-run clipping)
    if (pc && kb && !pc->hidden && !kb->hidden) {
        float dt = kb->pos.y;
        Vector3 a{kb->pos.x - kb->size.x * 0.30f, dt + kb->size.y + 0.010f,
                  kb->pos.z - kb->size.z / 2};                    // keybed rear-left
        Vector3 w{-0.05f, dt + 0.022f, 1.02f};                    // left of monitor
        Vector3 g{-0.12f, dt + 0.022f, 0.60f};                    // grommet
        Vector3 u{g.x, 0.70f, g.z - 0.02f};                       // under-slab
        Vector3 t{pc->pos.x + 0.05f, pc->pos.y + pc->size.y - 0.06f,
                  pc->pos.z + 0.10f};                             // tower top
        Cord(a, w, 0.010f, rubber);
        Cord(w, g, 0.012f, rubber);
        Cord(g, u, 0.006f, rubber);
        Cord(u, t, 0.09f, rubber);
    }
    if (pc && ms && !pc->hidden && !ms->hidden) {
        float dt = ms->pos.y;
        Vector3 a{ms->pos.x - 0.02f, dt + ms->size.y + 0.008f,
                  ms->pos.z - ms->size.z / 2};                    // mouse rear
        Vector3 w{1.10f, dt + 0.024f, 0.92f};                     // right of monitor
        Vector3 g{-0.08f, dt + 0.024f, 0.57f};                    // same grommet
        Vector3 u{g.x, 0.70f, g.z};
        Vector3 t{pc->pos.x + 0.08f, pc->pos.y + pc->size.y - 0.04f,
                  pc->pos.z + 0.14f};
        Cord(a, w, 0.012f, rubber);
        Cord(w, g, 0.012f, rubber);
        Cord(g, u, 0.006f, rubber);
        Cord(u, t, 0.08f, rubber);
    }
}

void Office::DrawItem(const Furni& f) const {
    Color dim = waitingLightsOut ? Color{110, 110, 115, 255} : WHITE;
    auto tint = [&](Color c) { return Color{(unsigned char)(c.r * dim.r / 255), (unsigned char)(c.g * dim.g / 255), (unsigned char)(c.b * dim.b / 255), c.a}; };
    Color c = tint(f.color);
    Vector3 p = f.pos;
    Vector3 s = f.size;

    rlPushMatrix();
    rlTranslatef(p.x, p.y, p.z);
    if (f.ft == FT::Door) {
        // pivot on the hinge edge, not the panel center
        rlTranslatef(-s.x / 2.f, 0, 0);
        if (f.rotY) rlRotatef(f.rotY, 0, 1, 0);
        rlTranslatef(s.x / 2.f, 0, 0);
    } else if (f.rotY) rlRotatef(f.rotY, 0, 1, 0);
    Vector3 o{0, 0, 0}; // local origin = bottom center

    if (f.ft == FT::Desk) {
        CBox({0, s.y - 0.06f, 0}, {s.x, 0.12f, s.z}, c);
        // pale underside panel - catches the under-desk bounce light so the
        // surface reads while crouched beneath it instead of a black void
        CBox({0, s.y - 0.125f, 0}, {s.x - 0.04f, 0.02f, s.z - 0.04f},
             tint(Color{125, 115, 100, 255}));
        // legs run the FULL depth - a depth inset would leave see-through
        // slivers at the side corners
        CBox({-s.x / 2 + 0.08f, s.y / 2, 0}, {0.16f, s.y, s.z}, tint(Color{c.r, c.g, c.b, 255}));
        CBox({s.x / 2 - 0.08f, s.y / 2, 0}, {0.16f, s.y, s.z}, c);
        // modesty panel on the visitor side (local -z) - matches the collision
        // boxes game.cpp adds while crouched under the desk; the +z edge stays
        // open as the hide entrance. Width reaches the legs' inner faces so
        // no gap sits in the back corners
        CBox({0, (s.y - 0.13f + 0.02f) / 2, -s.z / 2 + 0.04f},
             {s.x - 0.32f, s.y - 0.15f, 0.08f}, c);
        // drawer pedestal hung under the top against the +x leg panel
        float pedX = s.x / 2 - 0.41f;
        CBox({pedX, (s.y - 0.13f + 0.05f) / 2, 0.10f}, {0.5f, s.y - 0.18f, s.z - 0.44f},
             tint(Color{(unsigned char)(c.r - 18), (unsigned char)(c.g - 16), (unsigned char)(c.b - 14), 255}));
        int drawers = s.y > 0.7f ? 2 : 1;
        for (int i = 0; i < drawers; i++) {
            float fy = s.y - 0.24f - i * 0.24f;
            // anomaly: the top drawer slides out toward the chair by drawerT.
            // The whole drawer BOX rides the slide - floor, side walls and
            // back plate - and a dark socket stays behind in the pedestal so
            // the open front isn't just a slab floating in front of the desk.
            float dz = (i == 0) ? drawerT * 0.42f : 0.f;
            float plateZ = s.z / 2 - 0.10f + dz;
            if (dz > 0.004f) {
                // shadowed recess the drawer body vacated
                CBox({pedX, fy, s.z / 2 - 0.13f}, {0.41f, 0.175f, 0.09f},
                     tint(Color{26, 24, 28, 255}));
                rlPushMatrix();
                // the out drawer sags a few degrees off its runners - reads
                // as "pulled itself out", not a pixel-perfect slide
                rlTranslatef(pedX, fy - 0.08f, s.z / 2 - 0.13f);
                rlRotatef(3.5f * drawerT, 1, 0, 0);
                rlTranslatef(-pedX, -(fy - 0.08f), -(s.z / 2 - 0.13f));
                float zBack = plateZ - 0.32f;              // stays parked in the pedestal
                float zC = (plateZ - 0.02f + zBack) / 2.f;
                float depth = plateZ - 0.02f - zBack;      // ~0.30
                Color inner = tint(Color{(unsigned char)(c.r - 24), (unsigned char)(c.g - 22),
                                         (unsigned char)(c.b - 20), 255});
                CBox({pedX, fy - 0.075f, zC}, {0.38f, 0.025f, depth}, inner);      // tray floor
                for (float sx : {-0.189f, 0.189f})
                    CBox({pedX + sx, fy - 0.028f, zC}, {0.022f, 0.115f, depth}, inner);  // side walls
                CBox({pedX, fy - 0.028f, zBack + 0.011f}, {0.38f, 0.115f, 0.022f}, inner); // back wall
                // manila folder stack riding the drawer floor
                CBox({pedX, fy - 0.050f, zC}, {0.33f, 0.045f, depth * 0.72f},
                     tint(Color{142, 126, 94, 255}));
                CBox({pedX + 0.04f, fy - 0.020f, zC + 0.015f}, {0.30f, 0.012f, depth * 0.55f},
                     tint(Color{168, 150, 112, 255}));
                CBox({pedX, fy, plateZ}, {0.44f, 0.19f, 0.025f}, c);               // drawer front
                CBox({pedX, fy + 0.055f, plateZ + 0.018f}, {0.16f, 0.024f, 0.02f},
                     tint(Color{38, 36, 40, 255}));                                // handle bar
                rlPopMatrix();
                continue;
            }
            CBox({pedX, fy, plateZ}, {0.44f, 0.19f, 0.025f}, c);                   // drawer front
            CBox({pedX, fy + 0.055f, plateZ + 0.018f}, {0.16f, 0.024f, 0.02f},
                 tint(Color{38, 36, 40, 255}));                                    // handle bar
        }
        // lore note taped to the underside - only visible from inside the hide
        // pocket: hangs just below the pale rib, scribbles on the downward face
        CBox({-0.55f, s.y - 0.140f, 0.28f}, {0.16f, 0.006f, 0.12f},
             tint(Color{198, 192, 172, 255}));                                    // paper slip
        CBox({-0.55f, s.y - 0.1436f, 0.322f}, {0.07f, 0.003f, 0.026f},
             tint(Color{155, 146, 122, 255}));                                    // tape flap
        for (int i = 0; i < 3; i++)
            CBox({-0.552f, s.y - 0.1436f, 0.248f + i * 0.030f},
                 {0.11f - i * 0.025f, 0.003f, 0.007f}, tint(Color{52, 48, 55, 255}));
    } else if (f.ft == FT::OfficeChair) {
        // task chair: 5-star base + casters, gas column, armrests; seat top ~0.5
        Color base = tint(Color{34, 35, 41, 255});
        for (int i = 0; i < 5; i++) {
            rlPushMatrix();
            rlRotatef(i * 72.f, 0, 1, 0);
            CBox({0.15f, 0.035f, 0}, {0.3f, 0.045f, 0.055f}, base);
            Sph({0.30f, 0.028f, 0}, 0.028f, base);
            rlPopMatrix();
        }
        DrawCylinderEx({0, 0.05f, 0}, {0, 0.42f, 0}, 0.045f, 0.05f, 8, base);
        CBox({0, 0.45f, 0}, {s.x, 0.1f, s.z}, c);                        // seat
        CBox({0, 0.45f + s.y / 2 - 0.1f, -s.z / 2 + 0.06f}, {s.x, s.y - 0.4f, 0.1f}, c); // back
        for (int i = -1; i <= 1; i += 2) {
            CBox({i * (s.x / 2 + 0.015f), 0.57f, 0.01f}, {0.045f, 0.16f, 0.05f}, base);  // post
            CBox({i * (s.x / 2 + 0.015f), 0.67f, 0.0f}, {0.07f, 0.04f, s.z * 0.55f}, c); // pad
        }
    } else if (f.ft == FT::VisitorChair) {
        // office side chair: 4 legs, apron, upholstered seat pad (top stays
        // at ~0.50 like the old seat so VisitorSitPos still lands), back
        // posts + padded back with a slight rearward rake
        Color leg = tint(Color{40, 40, 45, 255});
        Color dk = tint(Color{(unsigned char)std::max(0, c.r - 16),
                              (unsigned char)std::max(0, c.g - 16),
                              (unsigned char)std::max(0, c.b - 14), 255});
        for (int i = -1; i <= 1; i += 2)
            for (int k = -1; k <= 1; k += 2)
                Box({i * (s.x / 2 - 0.055f), 0, k * (s.z / 2 - 0.055f)},
                    {0.05f, 0.43f, 0.05f}, leg);
        Box({0, 0.37f, 0}, {s.x * 0.88f, 0.06f, s.z * 0.88f}, leg);      // apron
        Box({0, 0.40f, 0.005f}, {s.x * 0.97f, 0.10f, s.z * 0.95f}, c);   // seat pad
        CBox({0, 0.452f, 0.006f}, {s.x * 0.90f, 0.012f, s.z * 0.88f}, dk); // seam
        for (int i = -1; i <= 1; i += 2)                                 // back posts
            DrawCylinderEx({i * (s.x / 2 - 0.06f), 0.50f, -s.z / 2 + 0.06f},
                           {i * (s.x / 2 - 0.06f), s.y, -s.z / 2 + 0.015f},
                           0.020f, 0.018f, 6, leg);
        rlPushMatrix();                                                  // back pad
        rlTranslatef(0, 0.50f, -s.z / 2 + 0.045f);
        rlRotatef(-6.f, 1, 0, 0);
        CBox({0, (s.y - 0.52f) * 0.52f, 0},
             {s.x * 0.90f, s.y - 0.48f, 0.075f}, c);
        CBox({0, (s.y - 0.52f) * 0.52f, -0.045f},
             {s.x * 0.80f, s.y - 0.56f, 0.02f}, dk);                     // rear shadow line
        rlPopMatrix();
    } else if (f.ft == FT::Monitor) {
        DrawCylinderEx({0, 0, 0}, {0, 0.12f, 0}, 0.16f, 0.1f, 8, c);
        CBox({0, 0.14f + s.y / 2, 0}, {s.x, s.y, s.z}, c);
        // screen face is textured with the live OS in DrawMonitorScreen()
        CBox({0, 0.14f + s.y / 2, s.z / 2 + 0.004f}, {s.x - 0.07f, s.y - 0.07f, 0.008f}, tint(Color{20, 26, 34, 255}));
        // tiny power LED on the bottom bezel lip, just right of the card
        CBox({s.x / 2 - 0.028f, 0.158f, s.z / 2 + 0.005f}, {0.012f, 0.006f, 0.006f},
             tint(Color{110, 225, 130, 255}));
    } else if (f.ft == FT::Keyboard) {
        // stepped wedge: flat front deck + taller strip under the monitor-side
        // edge, so the keyboard reads propped on its back feet
        Box(o, {s.x, 0.018f, s.z}, c);
        CBox({0, 0.022f, -s.z * 0.28f}, {s.x * 0.94f, 0.012f, s.z * 0.40f},
             tint(Color{(unsigned char)(c.r - 6), (unsigned char)(c.g - 6),
                        (unsigned char)(c.b - 4), 255}));                     // rear riser
        // 4x12 key field - the two rear rows sit up on the riser deck
        Color kc = tint(Color{62, 64, 70, 255});
        for (int r = 0; r < 4; r++) {
            float kz = -0.062f + r * 0.031f;
            float ky = (kz < -0.0144f ? 0.028f : 0.018f) + 0.004f;
            for (int i = 0; i < 12; i++)
                CBox({-s.x / 2 + 0.025f + i * 0.038f, ky, kz},
                     {0.032f, 0.008f, 0.028f}, kc);
        }
        CBox({0, 0.022f, s.z * 0.35f}, {0.16f, 0.008f, 0.028f}, kc);          // spacebar
        // status LEDs top-right, standing on the riser
        Color lc[3] = {tint(Color{90, 200, 110, 255}), tint(Color{200, 170, 80, 255}),
                       tint(Color{60, 60, 64, 255})};
        for (int i = 0; i < 3; i++)
            CBox({s.x * 0.36f + i * 0.02f, 0.0295f, -s.z * 0.40f},
                 {0.010f, 0.003f, 0.010f}, lc[i]);
        // cable off the back edge, snaking toward the monitor stand
        Color wc = tint(Color{28, 28, 32, 255});
        DrawCylinderEx({0.05f, 0.010f, -s.z / 2}, {0.02f, 0.007f, -s.z / 2 - 0.09f},
                       0.006f, 0.006f, 5, wc);
        DrawCylinderEx({0.02f, 0.007f, -s.z / 2 - 0.09f},
                       {-0.1f, 0.006f, -s.z / 2 - 0.18f}, 0.006f, 0.006f, 5, wc);
    } else if (f.ft == FT::Mouse) {
        // low body + palm hump; buttons/scroll wheel at the front (-z) nose,
        // cable trailing out the nose toward the monitor stand
        CBox({0, s.y * 0.30f, 0}, {s.x, s.y * 0.6f, s.z}, c);                 // body
        Sph({0, s.y * 0.52f, s.z * 0.10f}, s.x * 0.34f, c);                   // palm hump
        CBox({0, s.y * 0.62f, -s.z * 0.34f}, {0.004f, 0.005f, s.z * 0.30f},
             tint(Color{18, 19, 23, 255}));                                   // button seam
        CBox({0, s.y * 0.62f, -s.z * 0.40f}, {0.013f, 0.010f, 0.016f},
             tint(Color{60, 62, 68, 255}));                                   // scroll wheel
        Color wc = tint(Color{28, 28, 32, 255});
        DrawCylinderEx({0.005f, s.y * 0.45f, -s.z / 2},
                       {-0.04f, 0.008f, -s.z / 2 - 0.13f}, 0.004f, 0.004f, 5, wc);
        DrawCylinderEx({-0.04f, 0.008f, -s.z / 2 - 0.13f},
                       {-0.11f, 0.007f, -s.z / 2 - 0.24f}, 0.004f, 0.004f, 5, wc);
    } else if (f.ft == FT::PcTower) {
        Box(o, s, c);                                                         // case
        float fz = s.z / 2;                                                   // front face = +z
        // slightly lighter front panel a hair proud of the case wall
        CBox({0, s.y * 0.5f, fz + 0.001f}, {s.x * 0.9f, s.y * 0.95f, 0.008f},
             tint(Color{42, 44, 52, 255}));
        Sph({0, s.y * 0.80f, fz + 0.008f}, 0.014f,
            tint(Color{16, 17, 21, 255}));                                    // power button
        Sph({0.035f, s.y * 0.80f, fz + 0.008f}, 0.006f,
            tint(Color{150, 220, 235, 255}));                                 // LED - it's ON
        Color dk = tint(Color{14, 15, 18, 255});
        for (int i = 0; i < 5; i++)                                           // vent slits
            CBox({0, s.y * 0.12f + i * 0.032f, fz + 0.006f},
                 {s.x * 0.68f, 0.007f, 0.005f}, dk);
        for (int i = -1; i <= 1; i += 2)                                      // USB pair
            CBox({i * 0.025f, s.y * 0.55f, fz + 0.006f},
                 {0.02f, 0.008f, 0.005f}, dk);
        CBox({0, s.y - 0.005f, 0}, {s.x * 0.97f, 0.012f, s.z * 0.97f},
             tint(Color{(unsigned char)(c.r - 8), (unsigned char)(c.g - 8),
                        (unsigned char)(c.b - 6), 255}));                     // top lip
    } else if (f.ft == FT::Outlet) {
        if (f.art == "floor") {
            // flush floor receptacle: plate, darker inset, two socket blocks
            CBox({0, 0.011f, 0}, {s.x, 0.012f, s.z}, c);
            CBox({0, 0.017f, 0}, {s.x * 0.78f, 0.006f, s.z * 0.68f},
                 tint(Color{150, 148, 140, 255}));
            for (int i = -1; i <= 1; i += 2)
                CBox({i * s.x * 0.22f, 0.021f, 0}, {0.028f, 0.006f, 0.034f},
                     tint(Color{45, 44, 48, 255}));
        } else {
            // wall plate - thin axis faces the room like the poster branch
            bool thinX = s.x <= s.z;
            float face = thinX ? (f.pos.x > 0 ? -1.f : 1.f)
                               : (f.pos.z > 0 ? -1.f : 1.f);
            float lat = (thinX ? s.z : s.x) / 2, th = thinX ? s.x : s.z;
            auto OBox = [&](float u, float v, float along, float w, float h,
                            float t, Color col) {
                if (thinX) CBox({along, v, u}, {t, h, w}, col);
                else       CBox({u, v, along}, {w, h, t}, col);
            };
            OBox(0, s.y * 0.5f, 0, lat * 2, s.y, th, c);                      // plate
            OBox(0, s.y * 0.5f, face * (th / 2 + 0.002f), lat * 1.5f, s.y * 0.72f,
                 0.005f, tint(Color{168, 166, 158, 255}));                    // inner bezel
            for (int i = -1; i <= 1; i += 2) {                                // duplex pair
                float dy = s.y * 0.5f + i * s.y * 0.20f;
                OBox(0, dy, face * (th / 2 + 0.006f), 0.030f, 0.045f, 0.010f,
                     tint(Color{45, 44, 48, 255}));
                OBox(0, dy, face * (th / 2 + 0.012f), 0.014f, 0.020f, 0.003f,
                     tint(Color{20, 19, 22, 255}));                           // slot holes
            }
        }
    } else if (f.ft == FT::PaperStack) {
        // real stacked sheets: jittered layers, the top one skewed a few
        // degrees and ruled like a half-finished form
        int sheets = 7;
        for (int i = 0; i < sheets; i++) {
            float jx = ((i * 37 + 11) % 7 - 3) * 0.0026f;
            float jz = ((i * 23 + 5) % 5 - 2) * 0.0034f;
            CBox({jx, s.y * 0.10f + i * (s.y * 0.78f / sheets) + 0.004f, jz},
                 {s.x, 0.006f, s.z}, tint(c));
        }
        rlPushMatrix();
        rlTranslatef(0.007f, s.y * 0.96f, -0.005f);
        rlRotatef(4.5f, 0, 1, 0);
        Color top{(unsigned char)std::min(255, c.r + 26),
                  (unsigned char)std::min(255, c.g + 26),
                  (unsigned char)std::min(255, c.b + 24), 255};
        CBox({0, 0, 0}, {s.x * 0.97f, 0.005f, s.z * 0.96f}, tint(top));
        Color ln = tint(Color{96, 92, 106, 255});
        CBox({-s.x * 0.10f, 0.0045f, -s.z * 0.34f}, {s.x * 0.52f, 0.0022f, 0.020f}, ln);  // header
        for (int i = 0; i < 4; i++)                                                  // ruled lines
            CBox({0, 0.0045f, -s.z * 0.16f + i * s.z * 0.15f},
                 {s.x * (0.72f - i * 0.05f), 0.0022f, 0.010f}, ln);
        rlPopMatrix();
    } else if (f.ft == FT::Rug) {
        Box(o, s, c);
        // woven border, inner pinstripes, centre medallion, fringe on the
        // short ends - a flat slab read as a paint spill, this reads as a rug
        Color bd{(unsigned char)std::max(0, c.r - 20),
                 (unsigned char)std::max(0, c.g - 18),
                 (unsigned char)std::max(0, c.b - 16), 255};
        Color ac{(unsigned char)std::min(255, c.r + 62),
                 (unsigned char)std::min(255, c.g + 42),
                 (unsigned char)std::min(255, c.b + 26), 255};
        float by = s.y + 0.002f, bw = std::min(s.x, s.z) * 0.085f;
        for (int i = -1; i <= 1; i += 2) {                                       // border band
            CBox({i * (s.x / 2 - bw / 2), by, 0}, {bw, 0.005f, s.z - 0.02f}, tint(bd));
            CBox({0, by, i * (s.z / 2 - bw / 2)}, {s.x - bw * 2, 0.005f, bw}, tint(bd));
        }
        for (int i = -1; i <= 1; i += 2)                                         // pinstripes
            CBox({0, by + 0.001f, i * s.z * 0.27f}, {s.x * 0.66f, 0.004f, 0.030f}, tint(ac));
        rlPushMatrix();                                                          // medallion
        rlTranslatef(0, by + 0.001f, 0);
        rlRotatef(45.f, 0, 1, 0);
        float md = std::min(s.x, s.z) * 0.30f;
        CBox({0, 0, 0}, {md, 0.004f, md}, tint(bd));
        CBox({0, 0.003f, 0}, {md * 0.55f, 0.004f, md * 0.55f}, tint(ac));
        rlPopMatrix();
        Color fr = tint(Color{188, 178, 158, 255});                              // fringe
        int tassels = std::max(6, (int)(s.z / 0.22f));
        for (int i = -1; i <= 1; i += 2)
            for (int t = 0; t < tassels; t++) {
                float tz = -s.z / 2 + 0.05f + (t + 0.5f) * (s.z - 0.1f) / tassels;
                CBox({i * (s.x / 2 + 0.028f), 0.004f, tz},
                     {0.056f, 0.005f, 0.024f}, fr);
            }
    } else if (f.ft == FT::Mug) {
        DrawCylinderEx({0, 0, 0}, {0, s.y, 0}, s.x / 2, s.x / 2, 10, c);
        // coffee surface + a paler rim ring so the cup doesn't read hollow-dark
        DrawCylinderEx({0, s.y * 0.88f, 0}, {0, s.y * 0.895f, 0},
                       s.x / 2 - 0.008f, s.x / 2 - 0.008f, 10, tint(Color{44, 28, 20, 255}));
        DrawCylinderEx({0, s.y * 0.97f, 0}, {0, s.y, 0}, s.x / 2 + 0.003f,
                       s.x / 2 + 0.003f, 10,
                       tint(Color{(unsigned char)std::min(255, c.r + 40),
                                  (unsigned char)std::min(255, c.g + 40),
                                  (unsigned char)std::min(255, c.b + 40), 255}));
        // square-C handle on the +x flank: two stubs + a grip segment
        DrawCylinderEx({s.x / 2 - 0.006f, s.y * 0.72f, 0},
                       {s.x / 2 + 0.016f, s.y * 0.74f, 0}, 0.006f, 0.006f, 5, c);
        DrawCylinderEx({s.x / 2 + 0.016f, s.y * 0.74f, 0},
                       {s.x / 2 + 0.016f, s.y * 0.30f, 0}, 0.006f, 0.006f, 5, c);
        DrawCylinderEx({s.x / 2 + 0.016f, s.y * 0.30f, 0},
                       {s.x / 2 - 0.006f, s.y * 0.28f, 0}, 0.006f, 0.006f, 5, c);
    } else if (f.ft == FT::DeskLamp) {
        // articulated architect lamp: weighted base, two-piece arm, cone
        // shade. game.cpp parks the point light at local ~(0.1, s.y-0.06,
        // -0.06) so the bulb must sit inside the shade mouth right there
        DrawCylinderEx({0, 0, 0}, {0, 0.02f, 0}, 0.13f, 0.13f, 12, c);        // flat disc
        DrawCylinderEx({0, 0.02f, 0}, {0, 0.05f, 0}, 0.09f, 0.065f, 10, c);   // dome weight
        Vector3 elbow{0.02f, s.y * 0.62f, -0.02f};
        DrawCylinderEx({0, 0.05f, 0}, elbow, 0.012f, 0.012f, 6, c);           // lower arm
        Sph(elbow, 0.017f, c);                                                // elbow joint
        Vector3 joint{0.09f, s.y - 0.10f, -0.055f};
        DrawCylinderEx(elbow, joint, 0.010f, 0.010f, 6, c);                   // upper arm
        // shade: narrow top flaring down-forward off the arm mount, so the
        // wide mouth aims at the desk surface with the bulb inside it (the
        // arm tip at `joint` ends up buried in the cone's top-back)
        DrawCylinderEx({0.088f, s.y - 0.050f, -0.046f},
                       {0.112f, s.y - 0.140f, -0.074f}, 0.035f, 0.075f, 10, c);
        Sph({0.088f, s.y - 0.048f, -0.046f}, 0.015f, c);                      // hinge cap
        Sph({0.10f, s.y - 0.085f, -0.06f}, 0.035f,
            tint(Color{255, 214, 150, 255}));                                 // bulb
        // pull switch: stub chain + bead dangling off the shade's rim edge
        DrawCylinderEx({0.146f, s.y - 0.086f, -0.057f},
                       {0.148f, s.y - 0.116f, -0.058f}, 0.003f, 0.003f, 5,
                       tint(Color{40, 40, 44, 255}));
        Sph({0.149f, s.y - 0.122f, -0.058f}, 0.007f, tint(Color{225, 220, 205, 255}));
    } else if (f.ft == FT::Bookshelf) {
        float fz = s.z / 2;
        // open-front carcass: back panel, two sides, top, bottom - interior stays visible
        CBox({0, s.y / 2, -fz + 0.03f}, {s.x, s.y, 0.06f}, c);
        CBox({-s.x / 2 + 0.035f, s.y / 2, -0.01f}, {0.07f, s.y, s.z - 0.04f}, c);
        CBox({ s.x / 2 - 0.035f, s.y / 2, -0.01f}, {0.07f, s.y, s.z - 0.04f}, c);
        CBox({0, s.y - 0.03f, -0.01f}, {s.x, 0.06f, s.z - 0.04f}, c);
        CBox({0, 0.03f, -0.01f}, {s.x, 0.06f, s.z - 0.04f}, c);
        int shelves = 4;
        for (int i = 0; i < shelves; i++) {
            float y = 0.38f + i * ((s.y - 0.92f) / (shelves - 1));  // top shelf keeps book headroom
            CBox({0, y, 0.f}, {s.x - 0.10f, 0.045f, s.z - 0.14f}, tint(c));   // shelf board
            int n = 12;
            float spacing = (s.x - 0.46f) / (n - 1);
            float stackX = s.x / 2 - 0.45f;      // horizontal stack lives on the top shelf
            bool prevLeaned = false;
            for (int b = 0; b < n; b++) {
                if (prevLeaned) { prevLeaned = false; continue; }   // leaner tilts into this slot
                if ((b + i * 3) % 7 == 4) continue;                     // occasional gap
                float bx = -s.x / 2 + 0.22f + b * spacing;
                // top shelf: keep the stack's footprint clear or upright books clip through it
                if (i == shelves - 1 && std::fabs(bx - stackX) < 0.19f) continue;
                float bh = 0.26f + ((b * 41 + i * 17) % 10) * 0.014f;
                float bw = std::min(0.05f + ((b * 23 + i * 11) % 4) * 0.012f, spacing * 0.78f);
                bool lean = ((b + i * 5) % 9 == 6);
                Color bc{(unsigned char)(55 + (b * 37 + i * 13) % 140),
                         (unsigned char)(45 + (b * 53 + i * 7) % 100),
                         (unsigned char)(40 + (b * 71 + i * 19) % 110), 255};
                if (lean) {
                    prevLeaned = true;   // next slot is visually occupied by the tilt
                    // pivot on the bottom-left edge, shallow angle, shorter book:
                    // top swings into the empty slot without touching slot b+2
                    float lh = bh * 0.8f;
                    rlPushMatrix();
                    rlTranslatef(bx - bw * 0.5f, y + 0.02f, 0.06f);
                    rlRotatef(-10.f, 0, 0, 1);
                    CBox({bw * 0.5f, lh / 2, 0}, {bw, lh, 0.22f}, tint(bc));
                    rlPopMatrix();
                } else {
                    CBox({bx, y + 0.02f + bh / 2, 0.06f}, {bw, bh, 0.22f}, tint(bc));
                }
            }
            if (i == shelves - 1) {                                   // horizontal stack, top shelf
                for (int k = 0; k < 3; k++)
                    CBox({stackX, y + 0.07f + k * 0.055f, 0.06f}, {0.30f - k * 0.04f, 0.05f, 0.22f},
                         tint(Color{(unsigned char)(70 + k * 30), (unsigned char)(60 + k * 20), 90, 255}));
            }
        }
    } else if (f.ft == FT::FilingCabinet) {
        bool isPrinter = (f.id == "printer");
        Box(o, s, c);
        for (int i = 0; i < 3; i++) {
            float y = 0.3f + i * 0.45f;
            CBox({0, y, s.z / 2 + 0.012f}, {s.x - 0.08f, 0.36f, 0.025f},
                 tint(Color{(unsigned char)(c.r - 32), (unsigned char)(c.g - 32), (unsigned char)(c.b - 30), 255}));
            CBox({0, y + 0.11f, s.z / 2 + 0.032f}, {0.18f, 0.035f, 0.022f},
                 tint(Color{35, 38, 42, 255}));      // handle
        }
        if (isPrinter) {
            // printer unit parked on the cabinet: dark chassis, lighter
            // scanner lid with a seam gap, output tray extended toward the
            // front lip, feed slot + standing sheet at the back, tiny panel
            Color body = tint(Color{52, 54, 60, 255});
            Color lid  = tint(Color{86, 88, 96, 255});
            float pw = s.x * 0.84f, pd = s.z * 0.80f;
            float py = s.y;                          // cabinet top
            CBox({0, py + 0.11f, -0.01f}, {pw, 0.22f, pd}, body);              // chassis
            CBox({0, py + 0.235f, -0.02f}, {pw * 0.92f, 0.03f, pd * 0.90f}, lid); // scanner lid
            CBox({0, py + 0.222f, -0.02f}, {pw * 0.94f, 0.008f, pd * 0.92f},
                 tint(Color{30, 31, 36, 255}));                                // lid seam shadow
            // output tray: shallow shelf sliding out over the front edge,
            // dark mouth where pages exit + one half-fed sheet
            CBox({0, py + 0.05f, pd * 0.5f + 0.10f}, {pw * 0.62f, 0.018f, 0.24f},
                 tint(Color{38, 40, 46, 255}));
            CBox({0, py + 0.10f, pd * 0.5f - 0.005f}, {pw * 0.5f, 0.05f, 0.012f},
                 tint(Color{18, 19, 24, 255}));                                // exit mouth
            CBox({0.01f, py + 0.062f, pd * 0.5f + 0.08f}, {pw * 0.44f, 0.004f, 0.17f},
                 tint(Color{215, 210, 196, 255}));                             // printed sheet
            for (int i = 0; i < 3; i++)                                        // print lines
                CBox({0.01f, py + 0.065f, pd * 0.5f + 0.03f + i * 0.04f},
                     {pw * 0.34f, 0.002f, 0.012f}, tint(Color{70, 68, 78, 255}));
            // rear feed slot + a standing sheet waiting to go through
            CBox({0, py + 0.24f, -pd * 0.42f}, {pw * 0.66f, 0.02f, 0.05f},
                 tint(Color{24, 25, 30, 255}));
            rlPushMatrix();
            rlTranslatef(0, py + 0.255f, -pd * 0.40f);
            rlRotatef(-14.f, 1, 0, 0);
            CBox({0, 0.10f, 0}, {pw * 0.5f, 0.20f, 0.006f},
                 tint(Color{220, 215, 200, 255}));                             // loaded paper
            rlPopMatrix();
            // control panel: slanted strip, green status LED, two buttons
            rlPushMatrix();
            rlTranslatef(pw * 0.28f, py + 0.225f, pd * 0.34f);
            rlRotatef(28.f, 1, 0, 0);
            CBox({0, 0, 0}, {pw * 0.34f, 0.014f, 0.07f}, tint(Color{34, 36, 42, 255}));
            CBox({-pw * 0.11f, 0.009f, 0}, {0.016f, 0.006f, 0.016f},
                 tint(Color{120, 230, 140, 255}));                             // ready LED
            for (int i = 0; i < 2; i++)
                CBox({-pw * 0.02f + i * 0.05f, 0.008f, 0}, {0.030f, 0.005f, 0.020f},
                     tint(Color{70, 74, 84, 255}));                            // buttons
            rlPopMatrix();
            // power cord droops off the rear toward the floor
            DrawCylinderEx({pw * 0.30f, py + 0.10f, -pd * 0.5f},
                           {pw * 0.34f, py * 0.4f, -pd * 0.5f - 0.02f},
                           0.006f, 0.006f, 5, tint(Color{24, 24, 28, 255}));
        }
    } else if (f.ft == FT::Printer) {
        // standalone printer (anomaly-spawned): same unit as the one parked
        // on the filing cabinet, but bottom-anchored and sized by `size`
        Color body = tint(Color{52, 54, 60, 255});
        Color lid  = tint(Color{86, 88, 96, 255});
        float pw = s.x, pd = s.z, py = 0.f;
        CBox({0, py + s.y * 0.42f, -0.01f}, {pw, s.y * 0.84f, pd}, body);        // chassis
        CBox({0, py + s.y * 0.90f, -0.02f}, {pw * 0.92f, s.y * 0.12f, pd * 0.90f}, lid); // scanner lid
        CBox({0, py + s.y * 0.845f, -0.02f}, {pw * 0.94f, 0.008f, pd * 0.92f},
             tint(Color{30, 31, 36, 255}));                                    // lid seam
        // output tray + exit mouth + a half-fed sheet
        CBox({0, py + s.y * 0.20f, pd * 0.5f + 0.10f}, {pw * 0.62f, 0.018f, 0.24f},
             tint(Color{38, 40, 46, 255}));
        CBox({0, py + s.y * 0.40f, pd * 0.5f - 0.005f}, {pw * 0.5f, 0.05f, 0.012f},
             tint(Color{18, 19, 24, 255}));
        CBox({0.01f, py + s.y * 0.24f, pd * 0.5f + 0.08f}, {pw * 0.44f, 0.004f, 0.17f},
             tint(Color{215, 210, 196, 255}));
        for (int i = 0; i < 3; i++)
            CBox({0.01f, py + s.y * 0.25f, pd * 0.5f + 0.03f + i * 0.04f},
                 {pw * 0.34f, 0.002f, 0.012f}, tint(Color{70, 68, 78, 255}));
        // rear feed slot + standing sheet
        CBox({0, py + s.y * 0.92f, -pd * 0.42f}, {pw * 0.66f, 0.02f, 0.05f},
             tint(Color{24, 25, 30, 255}));
        rlPushMatrix();
        rlTranslatef(0, py + s.y * 0.97f, -pd * 0.40f);
        rlRotatef(-14.f, 1, 0, 0);
        CBox({0, 0.10f, 0}, {pw * 0.5f, 0.20f, 0.006f}, tint(Color{220, 215, 200, 255}));
        rlPopMatrix();
        // control panel: slanted strip, status LED, two buttons
        rlPushMatrix();
        rlTranslatef(pw * 0.28f, py + s.y * 0.86f, pd * 0.34f);
        rlRotatef(28.f, 1, 0, 0);
        CBox({0, 0, 0}, {pw * 0.34f, 0.014f, 0.07f}, tint(Color{34, 36, 42, 255}));
        CBox({-pw * 0.11f, 0.009f, 0}, {0.016f, 0.006f, 0.016f},
             tint(Color{120, 230, 140, 255}));
        for (int i = 0; i < 2; i++)
            CBox({-pw * 0.02f + i * 0.05f, 0.008f, 0}, {0.030f, 0.005f, 0.020f},
                 tint(Color{70, 74, 84, 255}));
        rlPopMatrix();
        DrawCylinderEx({pw * 0.30f, py + s.y * 0.4f, -pd * 0.5f},
                       {pw * 0.34f, 0.02f, -pd * 0.5f - 0.02f},
                       0.006f, 0.006f, 5, tint(Color{24, 24, 28, 255}));
    } else if (f.ft == FT::Plant) {
        Color bark{82, 58, 40, 255};
        DrawCylinderEx({0, 0, 0}, {0, 0.36f, 0}, 0.24f, 0.19f, 10, tint(Color{110, 70, 50, 255}));  // pot
        // trunk + two branches so the foliage isn't floating
        DrawCylinderEx({0, 0.34f, 0}, {0.02f, 0.80f, 0.02f}, 0.05f, 0.028f, 7, tint(bark));
        DrawCylinderEx({0.01f, 0.60f, 0.01f}, {0.13f, 0.74f, 0.08f}, 0.024f, 0.012f, 6, tint(bark));
        DrawCylinderEx({0.015f, 0.68f, 0.015f}, {-0.10f, 0.84f, -0.06f}, 0.022f, 0.011f, 6, tint(bark));
        Sph({0.02f, 0.92f, 0.02f}, s.x * 0.40f, c);
        Sph({0.15f, 0.78f, 0.09f}, s.x * 0.30f, c);
        Sph({-0.12f, 0.88f, -0.07f}, s.x * 0.32f, tint(Color{(unsigned char)(c.r - 12), (unsigned char)(c.g - 15), (unsigned char)(c.b - 12), 255}));
        Sph({0, s.y * 0.96f, 0}, s.x * 0.26f, c);
    } else if (f.ft == FT::Vase) {
        // tapered ceramic body, a few stems with muted blooms
        DrawCylinderEx({0, 0, 0}, {0, s.y * 0.52f, 0}, s.x * 0.26f, s.x * 0.45f, 10, c);
        DrawCylinderEx({0, s.y * 0.52f, 0}, {0, s.y, 0}, s.x * 0.45f, s.x * 0.26f, 10, c);
        Color stem = tint(Color{52, 84, 52, 255});
        DrawCylinderEx({0, s.y * 0.7f, 0}, {-0.022f, s.y + 0.12f, 0.016f}, 0.005f, 0.004f, 5, stem);
        DrawCylinderEx({0.006f, s.y * 0.7f, 0.004f}, {0.032f, s.y + 0.17f, -0.02f}, 0.005f, 0.004f, 5, stem);
        DrawCylinderEx({-0.005f, s.y * 0.7f, -0.004f}, {0.006f, s.y + 0.08f, 0.036f}, 0.005f, 0.004f, 5, stem);
        // leaf slivers: thin tilted boxes partway up two stems
        Color leaf = tint(Color{46, 78, 48, 255});
        rlPushMatrix();
        rlTranslatef(-0.014f, s.y + 0.02f, 0.010f);
        rlRotatef(20.f, 0, 1, 0); rlRotatef(38.f, 0, 0, 1);
        CBox({-0.032f, 0, 0}, {0.085f, 0.007f, 0.028f}, leaf);
        rlPopMatrix();
        rlPushMatrix();
        rlTranslatef(0.020f, s.y + 0.07f, -0.008f);
        rlRotatef(-15.f, 0, 1, 0); rlRotatef(-42.f, 0, 0, 1);
        CBox({0.032f, 0, 0}, {0.08f, 0.007f, 0.026f}, leaf);
        rlPopMatrix();
        // blooms: a seed centre ringed by 5 tiny flattened petal spheres
        // (4x5 LOD on the petals keeps the whole cluster ~180 verts)
        auto bloom = [&](Vector3 bc, float br, Color pet) {
            DrawSphereEx(bc, br * 0.45f, 4, 5, tint(Color{140, 105, 60, 255}));
            for (int i = 0; i < 5; i++) {
                float a = i * 1.25664f + 0.35f;
                DrawSphereEx({bc.x + std::cos(a) * br * 0.78f,
                              bc.y + (i % 2 ? 0.005f : -0.003f),
                              bc.z + std::sin(a) * br * 0.78f},
                             br * 0.46f, 4, 5, tint(pet));
            }
        };
        bloom({-0.022f, s.y + 0.13f, 0.016f}, 0.030f, Color{185, 125, 125, 255});   // dusty rose
        bloom({0.032f, s.y + 0.18f, -0.02f}, 0.026f, Color{200, 180, 110, 255});    // pale yellow
        bloom({0.006f, s.y + 0.09f, 0.038f}, 0.024f, Color{195, 150, 140, 255});
    } else if (f.ft == FT::Poster) {
        // framed plate: oversized dark backing, then the card, then thin lip
        // bars proud of the face - procedural art recessed inside the margins.
        // face = thin axis pointing toward the room centre; (u,v) = lateral,height.
        bool thinX = s.x <= s.z;
        float face = thinX ? (f.pos.x > 0 ? -1.f : 1.f) : (f.pos.z > 0 ? -1.f : 1.f);
        float lat = (thinX ? s.z : s.x) / 2, vh = s.y;
        float th = thinX ? s.x : s.z;
        float dep = th / 2 + 0.004f;
        auto PBox = [&](float u, float v, float along, float w, float h, float t, Color col) {
            if (thinX) CBox({along, v, u}, {t, h, w}, tint(col));
            else       CBox({u, v, along}, {w, h, t}, tint(col));
        };
        // backing sits BEHIND the card face: at -face*0.014 its front lands
        // ~4mm under the card surface so only the dark margin ring shows and
        // nothing is coplanar with the card's own front plane
        PBox(0, 0, -face * 0.014f, lat * 2 + 0.07f, vh + 0.07f, th + 0.02f, Color{46, 42, 47, 255}); // backing
        PBox(0, 0, 0, lat * 2, vh, th, c);                                    // the card
        Color fr{56, 46, 38, 255};                                             // wood lip
        float fw = std::min(0.045f, lat * 0.3f);
        PBox(0,  vh / 2 + fw * 0.4f, face * (dep - 0.002f), lat * 2 + fw * 1.6f, fw, 0.018f, fr);
        PBox(0, -vh / 2 - fw * 0.4f, face * (dep - 0.002f), lat * 2 + fw * 1.6f, fw, 0.018f, fr);
        PBox(-lat - fw * 0.4f, 0, face * (dep - 0.002f), fw, vh + fw * 0.8f, 0.018f, fr);
        PBox( lat + fw * 0.4f, 0, face * (dep - 0.002f), fw, vh + fw * 0.8f, 0.018f, fr);
        // art layers get a progressive ~1.2mm lift so no two faces are ever
        // coplanar with the card or each other - same draw order as before
        float lift = 0.0016f;   // starts just off the card face too
        auto ABox = [&](float u, float v, float w, float h, Color col) {
            PBox(u, v, face * (dep + lift), w, h, 0.008f, col);
            lift += 0.0012f;
        };
        float al = lat * 0.86f;              // art field inside a margin
        if (f.art == "landscape" || f.art == "figure") {
            // sunset landscape: banded sky, dark ridge, sun, ground + horizon line
            ABox(0, vh * 0.30f, al * 2, vh * 0.28f, Color{110, 90, 105, 255});        // dusk band
            ABox(0, vh * 0.10f, al * 2, vh * 0.14f, Color{70, 62, 72, 255});          // low sky
            ABox(0, -vh * 0.20f, al * 2, vh * 0.40f, Color{42, 44, 50, 255});         // ground
            ABox(-lat * 0.15f, -vh * 0.02f, al * 1.2f, vh * 0.20f, Color{30, 32, 38, 255}); // ridge
            ABox(0, -vh * 0.01f, al * 2, vh * 0.02f, Color{180, 130, 80, 255});       // horizon glow
            float sr = std::min(lat * 0.28f, vh * 0.15f);
            // sun disc built from two crossed slabs so it reads round from the side
            ABox(lat * 0.32f, vh * 0.16f, sr * 2, sr * 0.7f, Color{215, 130, 70, 255});
            ABox(lat * 0.32f, vh * 0.16f, sr * 1.4f, sr * 1.4f, Color{215, 130, 70, 255});
            if (f.art == "figure") {
                // anomaly: somebody is standing in the field - a dark
                // silhouette with a pale head where no one was painted
                ABox(lat * 0.16f, -vh * 0.11f, lat * 0.24f, vh * 0.30f, Color{16, 15, 20, 255});
                ABox(lat * 0.16f, vh * 0.09f, lat * 0.12f, vh * 0.11f, Color{180, 170, 155, 255});
            }
        } else if (f.art == "portrait" || f.art == "face") {
            // ID photo: dark backdrop, pale head, shoulders, inner matte margin
            ABox(0, 0, al * 2, vh * 0.9f, Color{70, 66, 60, 255});                    // matte
            ABox(0, 0, al * 1.8f, vh * 0.82f, Color{38, 40, 46, 255});                // backdrop
            ABox(0, -vh * 0.28f, lat * 1.1f, vh * 0.36f, Color{70, 72, 80, 255});     // shoulders
            if (f.art == "face") {
                // anomaly: the face is a dark hollow with two pale eyes in it
                ABox(0, vh * 0.06f, lat * 0.48f, vh * 0.40f, Color{14, 13, 17, 255});
                ABox(-lat * 0.12f, vh * 0.10f, lat * 0.07f, vh * 0.05f, Color{225, 218, 200, 255});
                ABox( lat * 0.12f, vh * 0.10f, lat * 0.07f, vh * 0.05f, Color{225, 218, 200, 255});
                ABox(0, vh * 0.23f, lat * 0.54f, vh * 0.14f, Color{30, 26, 24, 255}); // hair
            } else {
                ABox(0, vh * 0.06f, lat * 0.48f, vh * 0.40f, Color{185, 160, 135, 255});  // face
                ABox(0, vh * 0.23f, lat * 0.54f, vh * 0.14f, Color{55, 45, 40, 255});     // hair
            }
        } else if (f.art == "scrawl") {
            // anomaly: the print is scratched over - frantic irregular
            // strokes, a torn corner, a black pin dragged through it
            ABox(0, 0, lat * 1.8f, vh * 0.92f, Color{196, 188, 168, 255});
            for (int i = 0; i < 7; i++) {
                float yo = vh * (0.30f - i * 0.10f);
                float uo = std::sin(i * 2.3f) * lat * 0.22f;
                ABox(uo - lat * 0.06f, yo, lat * (0.9f + 0.5f * std::fabs(std::sin(i * 1.7f))),
                     vh * 0.04f, Color{52, 48, 52, 255});
            }
            // two hard gouges across the sheet
            ABox(-lat * 0.3f, 0, lat * 0.05f, vh * 0.8f, Color{40, 36, 40, 255});
            ABox(lat * 0.25f, -vh * 0.05f, lat * 0.05f, vh * 0.7f, Color{40, 36, 40, 255});
            ABox(0, vh * 0.42f, 0.05f, 0.05f, Color{30, 26, 30, 255});                // pin gone black
        } else if (f.art == "certificate") {
            // parchment card, double-ruled border, title bar, text lines, seal
            ABox(0, 0, al * 2, vh * 0.94f, Color{205, 195, 165, 255});                // parchment
            ABox(0, 0, al * 1.92f, vh * 0.88f, Color{120, 90, 50, 255});              // outer rule
            ABox(0, 0, al * 1.82f, vh * 0.82f, Color{205, 195, 165, 255});
            ABox(0, 0, al * 1.72f, vh * 0.76f, Color{150, 120, 75, 255});             // inner rule
            ABox(0, 0, al * 1.64f, vh * 0.70f, Color{205, 195, 165, 255});
            ABox(0, vh * 0.22f, al * 0.8f, vh * 0.06f, Color{70, 60, 55, 255});       // title
            for (int i = 0; i < 3; i++)                                              // lines
                ABox(-lat * 0.05f, vh * 0.04f - i * vh * 0.12f, al * (1.15f - i * 0.18f),
                     vh * 0.045f, Color{95, 85, 75, 255});
            float sr = vh * 0.08f;                                                   // seal + ribbon
            ABox(lat * 0.5f, -vh * 0.24f, sr * 2, sr * 0.8f, Color{150, 60, 50, 255});
            ABox(lat * 0.5f, -vh * 0.24f, sr * 1.5f, sr * 1.5f, Color{150, 60, 50, 255});
            ABox(lat * 0.5f, -vh * 0.37f, sr * 0.5f, vh * 0.13f, Color{120, 45, 40, 255});
        } else if (f.art == "sign") {
            // bold wayfinding band: dark field, pale band, block glyphs
            ABox(0, 0, lat * 2, vh, Color{30, 38, 46, 255});
            ABox(0, 0, lat * 1.9f, vh * 0.58f, Color{205, 200, 185, 255});
            int ng = 7;                                                              // "WAITING"
            float gw = lat * 1.7f / ng;
            for (int i = 0; i < ng; i++)
                ABox(-lat * 0.85f + gw * (i + 0.5f), 0, gw * 0.55f, vh * 0.30f,
                     Color{45, 50, 58, 255});
        } else if (f.art == "notice") {
            // pinned memo: lines of "text" + a red pin
            ABox(0, 0, lat * 1.8f, vh * 0.92f, Color{215, 208, 190, 255});
            for (int i = 0; i < 5; i++)
                ABox(-lat * 0.1f, vh * 0.22f - i * vh * 0.16f, lat * 1.4f, vh * 0.045f,
                     Color{70, 70, 78, 255});
            ABox(0, vh * 0.42f, 0.05f, 0.05f, Color{170, 60, 55, 255});
        } else {
            // abstract bars over a muted wash
            ABox(0, 0, al * 2, vh * 0.9f, Color{54, 52, 60, 255});
            Color cols[3] = {{150, 80, 70, 255}, {80, 110, 140, 255}, {190, 170, 90, 255}};
            for (int i = 0; i < 3; i++)
                ABox(-lat * 0.45f + i * lat * 0.45f, -vh * 0.05f + i * vh * 0.12f,
                     lat * 0.26f, vh * (0.8f - i * 0.18f), cols[i]);
            ABox(0, -vh * 0.34f, al * 1.6f, vh * 0.03f, Color{200, 195, 180, 255});   // baseline
        }
    } else if (f.ft == FT::Phone) {
        Box(o, {s.x, s.y * 0.6f, s.z}, c);                                    // base
        CBox({0, s.y * 0.78f, -s.z * 0.18f}, {s.x * 0.92f, s.y * 0.36f, s.z * 0.34f},
             tint(Color{(unsigned char)(c.r - 12), (unsigned char)(c.g - 12), (unsigned char)(c.b - 10), 255})); // handset
        for (int i = 0; i < 6; i++)                                            // keypad dots
            CBox({-s.x * 0.2f + (i % 3) * s.x * 0.2f, s.y * 0.62f, s.z * 0.2f + (i / 3) * s.z * 0.18f},
                 {0.02f, 0.012f, 0.02f}, tint(Color{70, 74, 82, 255}));
        // small LCD strip above the keypad + a cord drooping off the handset
        CBox({0, s.y * 0.625f, s.z * 0.04f}, {s.x * 0.40f, 0.012f, s.z * 0.12f},
             tint(Color{74, 88, 80, 255}));
        Color cord = tint(Color{20, 20, 24, 255});
        Vector3 c0{s.x * 0.42f, s.y * 0.82f, -s.z * 0.16f};
        Vector3 c1{s.x * 0.58f, s.y * 0.40f, s.z * 0.10f};
        Vector3 c2{s.x * 0.40f, s.y * 0.14f, s.z * 0.42f};
        DrawCylinderEx(c0, c1, 0.006f, 0.006f, 5, cord);
        DrawCylinderEx(c1, c2, 0.006f, 0.006f, 5, cord);
    } else if (f.ft == FT::WallClock) {
        // dark rim bezel a touch wider than the dial - it must stop SHORT of
        // the face cap or its front lid covers the whole dial; ending 2mm
        // back leaves only the annular bezel ring showing
        DrawCylinderEx({0, 0, -0.006f}, {0, 0, s.z - 0.002f}, s.x / 2 + 0.02f,
                       s.x / 2 + 0.02f, 16, tint(Color{52, 48, 44, 255}));
        DrawCylinderEx({0, 0, 0}, {0, 0, s.z}, s.x / 2, s.x / 2, 16, c);
        Color hc = tint(Color{30, 30, 35, 255});
        // hour ticks around the dial
        for (int i = 0; i < 12; i++) {
            rlPushMatrix(); rlTranslatef(0, 0, s.z + 0.006f); rlRotatef(i * 30.f, 0, 0, 1);
            CBox({0, s.x * 0.42f, 0}, {0.014f, s.x * 0.075f, 0.006f}, hc);
            rlPopMatrix();
        }
        // anomaly: hands freeze on "stopped", sweep counter-clockwise on
        // "backwards" (negated hour mirrors every hand's motion)
        float hr = f.stopped ? 3.55f : (f.backwards ? -clockHour : clockHour);
        float mAng = -(hr - std::floor(hr)) * 360.f;
        float hAng = -(std::fmod(hr, 12.f) / 12.f) * 360.f;
        float sAng = -(hr * 60.f - std::floor(hr * 60.f)) * 360.f;   // sweeps once a minute
        rlPushMatrix(); rlTranslatef(0, 0, s.z + 0.012f); rlRotatef(hAng, 0, 0, 1);
        CBox({0, s.x * 0.13f, 0}, {0.028f, s.x * 0.26f, 0.014f}, hc);
        rlPopMatrix();
        rlPushMatrix(); rlTranslatef(0, 0, s.z + 0.014f); rlRotatef(mAng, 0, 0, 1);
        CBox({0, s.x * 0.19f, 0}, {0.02f, s.x * 0.38f, 0.014f}, hc);
        rlPopMatrix();
        // thin second hand - freezes with the rest when the anomaly stops it
        rlPushMatrix(); rlTranslatef(0, 0, s.z + 0.016f); rlRotatef(sAng, 0, 0, 1);
        CBox({0, s.x * 0.20f, 0}, {0.008f, s.x * 0.40f, 0.008f}, tint(Color{140, 44, 38, 255}));
        rlPopMatrix();
        Sph({0, 0, s.z + 0.019f}, 0.014f, hc);          // centre pin
    } else if (f.ft == FT::Couch) {
        // recliner armchair: low base, seat cushion, fat padded arms, a tall
        // slightly-reclined back, and a footrest jutting out front (local +z)
        Color dk = tint(Color{(unsigned char)(c.r - 14), (unsigned char)(c.g - 14), (unsigned char)(c.b - 12), 255});
        Box(o, {s.x, s.y * 0.38f, s.z}, c);                                       // base
        CBox({0, s.y * 0.375f, 0.04f}, {s.x - 0.40f, 0.015f, s.z - 0.32f}, dk);   // seat seam
        CBox({0, s.y * 0.46f, 0.04f}, {s.x - 0.42f, 0.14f, s.z - 0.34f}, c);      // seat cushion
        for (int i = -1; i <= 1; i += 2) {                                        // fat arms + pads
            Box({i * (s.x / 2 - 0.14f), 0, -0.02f}, {0.28f, s.y * 0.60f, s.z - 0.04f}, c);
            CBox({i * (s.x / 2 - 0.14f), s.y * 0.64f, -0.02f}, {0.30f, 0.10f, s.z - 0.02f}, dk);
            CBox({i * (s.x / 2 - 0.295f), s.y * 0.47f, 0.04f}, {0.02f, 0.15f, s.z - 0.36f}, dk); // seam
        }
        rlPushMatrix();                                   // back hinged at the seat
        rlTranslatef(0, s.y * 0.40f, -s.z / 2 + 0.15f);
        rlRotatef(-8.f, 1, 0, 0);                          // reclined ~8 deg
        CBox({0, s.y * 0.33f, 0}, {s.x - 0.36f, s.y * 0.66f, 0.22f}, c);
        CBox({0, s.y * 0.60f, 0.06f}, {s.x - 0.52f, s.y * 0.15f, 0.15f}, dk);     // headrest
        rlPopMatrix();
        CBox({0, s.y * 0.10f, s.z / 2 + 0.14f}, {s.x - 0.46f, 0.06f, 0.34f}, dk); // footrest plate
        CBox({0, s.y * 0.16f, s.z / 2 + 0.15f}, {s.x - 0.42f, 0.08f, 0.38f}, c);  // footrest pad
    } else if (f.ft == FT::Bench) {
        // waiting-room bench: 4 legs, seat frame, segmented cushions (gap
        // lines between), low back rail. Seat cushion tops out ~s.y*0.93.
        Color leg = tint(Color{42, 40, 46, 255});
        for (int i = -1; i <= 1; i += 2)
            for (int k = -1; k <= 1; k += 2)
                Box({i * (s.x / 2 - 0.09f), 0, k * (s.z / 2 - 0.06f)},
                    {0.07f, s.y * 0.68f, 0.07f}, leg);
        CBox({0, s.y * 0.72f, 0}, {s.x, s.y * 0.12f, s.z}, c);                  // seat frame
        int seg = s.x > 3.f ? 3 : 2;
        float cw = (s.x - 0.06f) / seg;
        for (int i = 0; i < seg; i++)
            CBox({-s.x / 2 + 0.03f + cw * (i + 0.5f), s.y * 0.86f, 0},
                 {cw - 0.035f, s.y * 0.14f, s.z - 0.03f}, c);                   // cushions
        for (int i = -1; i <= 1; i += 2)                                        // back posts
            Box({i * (s.x / 2 - 0.09f), s.y * 0.78f, -s.z / 2 + 0.045f},
                {0.06f, s.y * 0.55f, 0.06f}, leg);
        CBox({0, s.y * 1.18f, -s.z / 2 + 0.05f}, {s.x - 0.10f, s.y * 0.28f, 0.05f}, c); // back rail
    } else if (f.ft == FT::WaterCooler) {
        // office water cooler: cabinet base, tap cluster over a drip tray,
        // inverted 5-gal bottle on top with neck, cap and a visible waterline
        float fz = s.z * 0.425f;                                     // cabinet front face
        Box(o, {s.x * 0.85f, s.y * 0.58f, s.z * 0.85f}, c);          // base cabinet
        CBox({0, s.y * 0.565f, 0}, {s.x * 0.9f, 0.035f, s.z * 0.9f},
             tint(Color{(unsigned char)(c.r - 22), (unsigned char)(c.g - 20),
                        (unsigned char)(c.b - 18), 255}));           // top collar the bottle sits in
        // recessed tap alcove + drips catch underneath
        CBox({0, s.y * 0.42f, fz - 0.012f}, {s.x * 0.62f, s.y * 0.20f, 0.028f},
             tint(Color{42, 44, 50, 255}));                          // alcove shadowbox
        CBox({0, s.y * 0.315f, fz + 0.035f}, {s.x * 0.60f, 0.02f, 0.13f},
             tint(Color{50, 52, 58, 255}));                          // drip tray shelf
        for (int i = -2; i <= 2; i++)                                // tray grille
            CBox({i * s.x * 0.10f, s.y * 0.327f, fz + 0.035f},
                 {0.008f, 0.006f, 0.11f}, tint(Color{26, 27, 32, 255}));
        // hot/cold taps: valve body + colored press lever + downward nozzle
        for (int i = -1; i <= 1; i += 2) {
            float tx = i * s.x * 0.145f;
            CBox({tx, s.y * 0.42f, fz + 0.028f}, {0.052f, 0.062f, 0.045f},
                 tint(Color{70, 72, 78, 255}));                      // valve body
            CBox({tx, s.y * 0.385f, fz + 0.052f}, {0.018f, 0.030f, 0.018f},
                 tint(Color{30, 31, 36, 255}));                      // nozzle
            CBox({tx, s.y * 0.462f, fz + 0.052f}, {0.040f, 0.012f, 0.045f},
                 i < 0 ? tint(Color{150, 52, 44, 255})
                       : tint(Color{64, 110, 170, 255}));            // red hot / blue cold
        }
        // translucent bottle seated in the collar, tapering shoulder + neck
        Color glass = tint(Color{165, 195, 215, 150});
        DrawCylinderEx({0, s.y * 0.56f, 0}, {0, s.y * 0.82f, 0}, s.x * 0.34f, s.x * 0.34f, 12, glass);
        DrawCylinderEx({0, s.y * 0.82f, 0}, {0, s.y * 0.90f, 0}, s.x * 0.34f, s.x * 0.13f, 12, glass);
        DrawCylinderEx({0, s.y * 0.90f, 0}, {0, s.y * 0.97f, 0}, s.x * 0.11f, s.x * 0.11f, 10, glass);
        CBox({0, s.y * 0.985f, 0}, {s.x * 0.24f, 0.022f, s.x * 0.24f},
             tint(Color{180, 70, 56, 255}));                         // red cap seal
        // waterline: darker disc inside the bottle + a lazy bubble or two
        DrawCylinderEx({0, s.y * 0.70f, 0}, {0, s.y * 0.712f, 0}, s.x * 0.31f, s.x * 0.31f, 12,
                       tint(Color{120, 160, 195, 170}));
        Sph({s.x * 0.10f, s.y * 0.76f, s.z * 0.06f}, 0.016f, tint(Color{200, 225, 240, 160}));
        Sph({-s.x * 0.07f, s.y * 0.63f, -s.z * 0.10f}, 0.011f, tint(Color{200, 225, 240, 160}));
        // cone-cup dispenser tube clipped to the cabinet's left flank
        DrawCylinderEx({-s.x * 0.47f, s.y * 0.30f, 0.02f}, {-s.x * 0.47f, s.y * 0.52f, 0.02f},
                       0.030f, 0.030f, 8, tint(Color{210, 208, 198, 255}));
        DrawCylinderEx({-s.x * 0.47f, s.y * 0.28f, 0.02f}, {-s.x * 0.47f, s.y * 0.315f, 0.02f},
                       0.024f, 0.008f, 8, tint(Color{230, 228, 216, 255}));   // cone cup tip
    } else if (f.ft == FT::Blinds) {
        CBox({0, s.y, 0}, {s.x, 0.12f, s.z + 0.02f}, tint(Color{150, 145, 135, 255}));
        Color dc = tint(Color{118, 112, 102, 255});
        if (blindsOpen) {
            // slats stacked at the very top, nearly out of view
            for (int i = 0; i < 3; i++)
                CBox({0, s.y - 0.14f - i * 0.045f, 0}, {s.x, 0.05f, s.z}, c);
            CBox({0, s.y - 0.30f, 0}, {s.x, 0.055f, s.z + 0.012f}, tint(Color{150, 144, 132, 255})); // bottom rail
        } else {
            // overlapping slats: actually opaque
            int slats = 14;
            float step = (s.y - 0.3f) / slats;
            for (int i = 0; i < slats; i++)
                CBox({0, s.y - 0.18f - i * step, 0}, {s.x, step * 1.35f, s.z}, c);
            CBox({0, s.y - 0.18f - slats * step, 0}, {s.x, 0.055f, s.z + 0.012f},
                 tint(Color{150, 144, 132, 255}));                         // bottom rail
        }
        // tilt wand on the right, a pair of lift cords converging on the left
        DrawCylinderEx({s.x * 0.42f, s.y - 0.05f, 0.025f}, {s.x * 0.42f, s.y - 0.62f, 0.04f},
                       0.008f, 0.006f, 5, dc);
        DrawCylinderEx({-s.x * 0.40f, s.y - 0.05f, 0.02f}, {-s.x * 0.415f, s.y - 0.85f, 0.03f},
                       0.004f, 0.004f, 5, dc);
        DrawCylinderEx({-s.x * 0.44f, s.y - 0.05f, 0.02f}, {-s.x * 0.415f, s.y - 0.85f, 0.03f},
                       0.004f, 0.004f, 5, dc);
        Sph({-s.x * 0.415f, s.y - 0.87f, 0.03f}, 0.010f, dc);                 // cord pull bead
    } else if (f.ft == FT::Door) {
        Box(o, s, c);
        // panelled timber door: two recessed panels + trim per face, a kick
        // plate, hinge knuckles on the pivot edge, knob + backplate both
        // sides, and a small engraved name plaque at eye height
        Color dk = tint(Color{(unsigned char)std::max(0, c.r - 24),
                              (unsigned char)std::max(0, c.g - 20),
                              (unsigned char)std::max(0, c.b - 16), 255});
        Color lt = tint(Color{(unsigned char)std::min(255, c.r + 16),
                              (unsigned char)std::min(255, c.g + 14),
                              (unsigned char)std::min(255, c.b + 12), 255});
        Color brs = tint(Color{150, 132, 92, 255});
        for (int zi = -1; zi <= 1; zi += 2) {
            float fz = zi * (s.z / 2);
            struct { float y, h; } panels[] = {{s.y * 0.685f, s.y * 0.40f},
                                               {s.y * 0.225f, s.y * 0.26f}};
            for (auto& p : panels) {
                CBox({0, p.y, fz + zi * 0.004f}, {s.x * 0.58f, p.h, 0.010f}, dk);
                float fw = 0.022f, fz2 = fz + zi * 0.012f;
                CBox({0, p.y + p.h / 2 + fw / 2, fz2}, {s.x * 0.62f, fw, 0.008f}, lt);
                CBox({0, p.y - p.h / 2 - fw / 2, fz2}, {s.x * 0.62f, fw, 0.008f}, lt);
                for (int xi = -1; xi <= 1; xi += 2)
                    CBox({xi * (s.x * 0.31f + fw / 2), p.y, fz2},
                         {fw, p.h + fw * 2, 0.008f}, lt);
            }
            CBox({0, 0.10f, fz + zi * 0.005f}, {s.x * 0.84f, 0.16f, 0.012f},
                 tint(Color{64, 58, 54, 255}));                             // kick plate
            float kx = s.x / 2 - 0.14f;                                      // knob gear
            CBox({kx, 1.05f, fz + zi * 0.006f}, {0.075f, 0.19f, 0.012f},
                 tint(Color{120, 106, 74, 255}));                            // backplate
            DrawCylinderEx({kx, 1.05f, fz}, {kx, 1.05f, fz + zi * 0.055f},
                           0.018f, 0.014f, 8, brs);                          // shaft
            Sph({kx, 1.05f, fz + zi * 0.062f}, 0.045f, brs);                  // knob
        }
        for (int i = 0; i < 3; i++) {                                        // hinge knuckles
            float hy = s.y * (0.18f + i * 0.32f);
            DrawCylinderEx({-s.x / 2 - 0.006f, hy - 0.045f, 0},
                           {-s.x / 2 - 0.006f, hy + 0.045f, 0},
                           0.014f, 0.014f, 6, tint(Color{96, 86, 64, 255}));
        }
        // engraved plaque: brass plate + two dark letter rules
        CBox({0, s.y * 0.72f, s.z / 2 + 0.008f}, {0.34f, 0.10f, 0.010f}, brs);
        CBox({0, s.y * 0.72f + 0.016f, s.z / 2 + 0.014f}, {0.22f, 0.014f, 0.004f},
             tint(Color{50, 44, 34, 255}));
        CBox({0, s.y * 0.72f - 0.014f, s.z / 2 + 0.014f}, {0.26f, 0.010f, 0.004f},
             tint(Color{50, 44, 34, 255}));
    } else if (f.ft == FT::Bin) {
        DrawCylinderEx({0, 0, 0}, {0, s.y, 0}, s.x / 2, s.x / 2.4f, 10, c);
        // wire-mesh hint: two thin rim bands hugging the tapered body - the
        // band radius tracks the body's own taper (r = s.x/2 -> s.x/2.4)
        Color rb = tint(Color{(unsigned char)(c.r - 20), (unsigned char)(c.g - 20),
                              (unsigned char)(c.b - 18), 255});
        auto ring = [&](float t) {
            float rr = s.x / 2 + (s.x / 2.4f - s.x / 2) * t + 0.004f;
            DrawCylinderEx({0, s.y * t, 0}, {0, s.y * t + 0.012f, 0}, rr, rr, 10, rb);
        };
        ring(0.38f);
        ring(0.72f);
    } else if (f.ft == FT::Switch) {
        // wall plate on a dark frame lip + a rocker paddle that TILTS with
        // the lamp state; a thin red stripe on the paddle's lower half only
        // shows when the lights are OFF
        bool on = lampDim > 0.5f;
        CBox({0, s.y * 0.5f, -0.011f}, {s.x + 0.03f, s.y + 0.03f, 0.024f},
             tint(Color{54, 50, 48, 255}));                                   // frame lip
        CBox({0, s.y * 0.5f, -0.002f}, {s.x, s.y, 0.012f}, c);                  // wall plate
        rlPushMatrix();
        rlTranslatef(0, s.y * 0.5f, 0.004f);                                  // hinge at plate face
        rlRotatef(on ? -11.f : 11.f, 1, 0, 0);                                // rocker: top-in = ON
        CBox({0, 0, 0.003f}, {s.x * 0.55f, s.y * 0.60f, 0.012f},
             tint(Color{(unsigned char)(c.r - 24), (unsigned char)(c.g - 22), (unsigned char)(c.b - 20), 255}));
        CBox({0, -s.y * 0.17f, 0.010f}, {s.x * 0.42f, 0.016f, 0.004f},
             on ? tint(Color{72, 44, 40, 255}) : Color{168, 50, 40, 255});    // off-stripe
        rlPopMatrix();
    } else if (f.ft == FT::CeilingLight) {
        // troffer: shallow metal bezel ring around a glowing diffuser.
        // fixtures dim with flicker (office) or waiting-room blackout
        float dl = (f.room == "waiting") ? (waitingLightsOut ? 0.12f : 1.f) : lampDim;
        Color hs = tint(Color{58, 58, 64, 255});
        CBox({0, 0.045f, 0}, {s.x + 0.1f, 0.05f, s.z + 0.1f}, hs);              // bezel plate
        CBox({0, 0, 0}, {s.x, s.y, s.z},
             Color{(unsigned char)(c.r * dl), (unsigned char)(c.g * dl), (unsigned char)(c.b * dl), c.a});
        CBox({0, -0.01f, s.z / 2 + 0.03f}, {s.x + 0.1f, 0.05f, 0.05f}, hs);     // rim bars
        CBox({0, -0.01f, -s.z / 2 - 0.03f}, {s.x + 0.1f, 0.05f, 0.05f}, hs);
        CBox({s.x / 2 + 0.03f, -0.01f, 0}, {0.05f, 0.05f, s.z + 0.02f}, hs);
        CBox({-s.x / 2 - 0.03f, -0.01f, 0}, {0.05f, 0.05f, s.z + 0.02f}, hs);
    } else if (f.ft == FT::Note) {
        // loose sheet of paper: flat slab, one corner kicked up, scrawl lines
        CBox({0, 0.004f, 0}, {s.x, 0.008f, s.z}, c);
        rlPushMatrix();
        rlTranslatef(s.x * 0.36f, 0.009f, -s.z * 0.30f);
        rlRotatef(26.f, 1, 0, 0);                       // corner peeling upward
        CBox({0, 0.004f, -s.z * 0.10f}, {s.x * 0.26f, 0.004f, s.z * 0.36f}, tint(c));
        rlPopMatrix();
        for (int i = 0; i < 3; i++)                     // scrawl lines on top
            CBox({-s.x * 0.05f, 0.0105f, -s.z * 0.22f + i * s.z * 0.18f},
                 {s.x * (0.68f - i * 0.14f), 0.004f, 0.008f},
                 tint(Color{58, 55, 64, 255}));
    } else {
        Box(o, s, c);
    }
    rlPopMatrix();
}
