#include "visitor.h"
#include <rlgl.h>

HumanoidApp AppFromJson(const json& j) {
    HumanoidApp a;
    const json* app = j.contains("appearance") ? &j["appearance"] : nullptr;
    if (app) {
        auto& ap = *app;
        a.height = JF(ap, "height", 1.f);
        a.skin = JC(ap["skin"], a.skin);
        a.shirt = JC(ap["shirt"], a.shirt);
        a.hair = JC(ap["hair"], a.hair);
        a.eyes = JS(ap, "eyes", "normal");
        a.pants = JC(ap["pants"], a.pants);
        if (ap.contains("eye_color")) a.eyeCol = JC(ap["eye_color"], a.eyeCol);
        std::string g = ToLower(JS(ap, "gender", JS(ap, "sex", "")));
        a.female = (g == "female" || g == "f" || g == "woman");
        // hair style accepts a name or a raw index
        if (ap.contains("hair_style")) {
            if (ap["hair_style"].is_number()) a.hairStyle = ap["hair_style"].get<int>();
            else {
                std::string hs = ToLower(JS(ap, "hair_style"));
                static const char* names[] = {"cap", "buzz", "long", "ponytail", "bun", "balding", "bald"};
                for (int i = 0; i < 7; i++) if (hs == names[i]) a.hairStyle = i;
            }
        }
        // accessories: glasses accepts bool or [r,g,b] frame tint; beard a
        // bool/int/style-name; tie a bool or [r,g,b]; badge a bool
        if (ap.contains("glasses")) {
            const json& g = ap["glasses"];
            if (g.is_array()) { a.glasses = true; a.glassesCol = JC(g, a.glassesCol); }
            else if (g.is_boolean()) a.glasses = g.get<bool>();
        }
        if (ap.contains("beard")) {
            const json& b = ap["beard"];
            if (b.is_number()) a.beard = b.get<int>();
            else if (b.is_boolean()) a.beard = b.get<bool>() ? 1 : 0;
            else {
                std::string bs = ToLower(JS(ap, "beard"));
                a.beard = (bs == "full") ? 2 : (bs == "stubble" || bs == "goatee") ? 1 : 0;
            }
        }
        if (ap.contains("tie")) {
            const json& t = ap["tie"];
            if (t.is_array()) { a.tie = true; a.tieCol = JC(t, a.tieCol); }
            else if (t.is_boolean()) a.tie = t.get<bool>();
        }
        if (ap.contains("badge") && ap["badge"].is_boolean()) a.badge = ap["badge"].get<bool>();
    }
    // anything the generator didn't specify still varies per person: seed
    // off the identity string so the same roster entry always looks the
    // same, but two coworkers never arrive as clones
    {
        std::string seed = JS(j, "id") + "|" + JS(j, "name");
        unsigned h = 2166136261u;
        for (char c : seed) { h ^= (unsigned char)c; h *= 16777619u; }
        if (!app || !app->contains("hair_style")) {
            // weighted pick: bald/balding exist but shouldn't be 2/7 of the
            // cast - rolls 7-9 fold back onto the fuller styles
            int pick = (int)((h >> 3) % 10);
            a.hairStyle = pick < 7 ? pick : pick - 7;
        }
        if (!app || (!app->contains("gender") && !app->contains("sex")))
            a.female = ((h >> 9) & 1) != 0;
        if (!app || !app->contains("eye_color")) {
            static const Color iris[] = {
                {74, 52, 34, 255},   // brown
                {96, 74, 40, 255},   // hazel
                {60, 88, 120, 255},  // blue
                {70, 100, 78, 255},  // green
                {78, 78, 84, 255},   // grey
                {46, 38, 34, 255}};  // near-black
            a.eyeCol = iris[(h >> 13) % 6];
        }
        if (!app || !app->contains("pants")) {
            static const Color slacks[] = {
                {35, 35, 42, 255},   // charcoal
                {38, 44, 62, 255},   // navy
                {58, 50, 40, 255},   // brown
                {48, 48, 50, 255},   // grey
                {30, 36, 34, 255}};  // dark olive
            a.pants = slacks[(h >> 6) % 5];
        }
        // accessories seed - office people come in glasses, beards, ties and
        // staff badges often enough that a full cast never looks stamped
        if (!app || !app->contains("glasses")) {
            a.glasses = ((h >> 17) % 10) < 2;
            static const Color frames[] = {{28, 28, 34, 255}, {62, 42, 30, 255},
                                           {120, 118, 112, 255}, {40, 50, 66, 255}};
            a.glassesCol = frames[(h >> 11) % 4];
        }
        if (!app || !app->contains("beard"))
            a.beard = a.female ? 0 : (((h >> 21) % 10) < 2 ? (((h >> 5) & 1) ? 2 : 1) : 0);
        if (!app || !app->contains("tie")) {
            a.tie = !a.female && ((h >> 25) % 10) < 3;
            static const Color ties[] = {{90, 40, 45, 255}, {40, 55, 90, 255},
                                         {55, 70, 50, 255}, {70, 55, 80, 255},
                                         {95, 75, 40, 255}};
            a.tieCol = ties[(h >> 7) % 5];
        }
        if (!app || !app->contains("badge")) a.badge = ((h >> 28) % 10) < 6;
    }
    return a;
}

// LOD spheres: DrawSphere is 16x16 (~1.6k verts) - silly for eyes/buttons.
// Three tiers: heads/caps get the most, mid-size parts (bun/ponytail knots,
// jaw, ears - big enough to read as jagged lumps at 5x6) get the middle,
// tiny bits stay cheap. Vertex count stays far under the 32k rlgl batch
// line where a mid-draw flush corrupts frame state.
static void Sph(Vector3 c, float r, Color col) {
    if (r > 0.08f)       DrawSphereEx(c, r, 10, 12, col);
    else if (r > 0.035f) DrawSphereEx(c, r, 7, 9, col);
    else                 DrawSphereEx(c, r, 5, 6, col);
}

// eyes defer to the unlit pass: under the office's dim point-light shader a
// lit sclera renders as a dark hole on the face. DrawHumanoid queues them
// here; DrawHumanoidEyes() draws them unlit after LitOff - bright, readable
struct EyeDraw { Vector3 s, ir, pu; float sr, irR, puR; Color sc, ic; bool iris; };
static std::vector<EyeDraw> g_eyeDraws;

void DrawHumanoidEyes() {
    for (const EyeDraw& e : g_eyeDraws) {
        Sph(e.s, e.sr, e.sc);
        if (e.iris) {
            Sph(e.ir, e.irR, e.ic);
            Sph(e.pu, e.puR, Color{14, 13, 13, 255});
        }
    }
    g_eyeDraws.clear();
}

void DrawHumanoid(Vector3 feet, float yawDeg, const HumanoidApp& a, bool seated, float animT, float armRaise, float seatH, float walkAmt, float detain, Vector3 look) {
    // grounding blob on the floor (y fixed: feet.y can ride a ladder lift)
    DrawBlobShadow({feet.x, 0.015f, feet.z}, 0.40f * a.height, 0.40f * a.height, 0.40f);
    float h = 1.72f * a.height;
    float yr = yawDeg * DEG2RAD;
    Vector3 f{std::sin(yr), 0, std::cos(yr)};
    Vector3 r{std::cos(yr), 0, -std::sin(yr)};
    float sway = std::sin(animT * 1.3f) * 0.012f;
    // gait blend: fades the walk swing in/out; armRaise (ladder work,
    // banging) always wins over it
    float wA = walkAmt * (1.f - std::clamp(armRaise, 0.f, 1.f));
    // idle breath: a slow swell on the chest radius while standing still
    float breathe = 1.f + std::sin(animT * 0.55f) * 0.02f * (1.f - wA);
    // hip rock during the stride - weight shifts onto the planted leg
    float hipRock = std::sin(animT) * 0.025f * wA;

    // seated hips rest on the actual seat surface (chair ~0.5, bench ~0.25);
    // standing heights are relative to feet.y so a worker can stand elevated
    float hipY = seated ? feet.y + seatH + 0.04f : feet.y + 0.95f * a.height;
    float shoulderY = hipY + 0.52f * a.height;
    float headY = shoulderY + 0.19f * a.height;
    // gait bob: the head dips twice per stride cycle
    headY += std::fabs(std::sin(animT)) * -0.014f * wA;
    Vector3 base{feet.x + f.x * sway * 4.f + r.x * hipRock, feet.y,
                 feet.z + f.z * sway * 4.f + r.z * hipRock};

    // build: women carry a slightly narrower frame than the default block.
    // waist < chest < hips is what sells "person" over "traffic cone"
    float shoulderW = (a.female ? 0.14f : 0.158f) * a.height;
    // male hips barely exceed the waist; female figures keep the oval flare
    float hipW = (a.female ? 0.165f : 0.145f) * a.height;
    float waistW = shoulderW * (a.female ? 0.80f : 0.85f);
    // spine leans a touch: seated people hunch, detainees bow, nobody
    // stands at literal parade rest
    float lean = (seated ? 0.05f : 0.012f) + detain * 0.035f;
    Vector3 top{base.x + f.x * lean * a.height, shoulderY,
                base.z + f.z * lean * a.height};
    // torso - two overlapping pieces: trousers rise over the pelvis, the
    // shirt tapers chest->waist. DrawCylinderEx emits no normals; set a
    // forward-facing one so limbs don't inherit a stale normal
    rlNormal3f(f.x, 0.25f, f.z);
    // pants: a waistband piece hugging the waist, then the thighs merge at
    // the centre to form the hip mass. A one-piece pelvis skirt was tried
    // and always read as a skirt - two overlapping thighs don't
    DrawCylinderEx({base.x, hipY - 0.03f * a.height, base.z},
                   {base.x, hipY + 0.15f * a.height, base.z},
                   waistW * 0.97f, waistW * 1.0f, 12, a.pants);
    // chest - 12-slice cross-section aligns its facets with the waistband
    DrawCylinderEx({base.x, hipY + 0.10f * a.height, base.z}, top,
                   waistW * 0.98f, shoulderW * breathe, 12, a.shirt);
    if (!a.female) {
        // belt band + buckle break up the shirt/pants seam
        rlNormal3f(f.x, 0.25f, f.z);
        DrawCylinderEx({base.x, hipY + 0.135f * a.height, base.z},
                       {base.x, hipY + 0.175f * a.height, base.z},
                       waistW * 1.02f, waistW * 1.02f, 12, Color{38, 34, 38, 255});
        rlPushMatrix();
        rlTranslatef(base.x + f.x * (waistW + 0.008f), hipY + 0.155f * a.height,
                     base.z + f.z * (waistW + 0.008f));
        rlRotatef(yawDeg, 0, 1, 0);
        DrawCubeV({0, 0, 0}, {0.034f * a.height, 0.026f * a.height, 0.010f},
                  Color{150, 142, 128, 255});
        rlPopMatrix();
    }
    if (a.female) {
        // A-line skirt in the pants fabric, hem mid-thigh - intentional
        // here: it's the female silhouette. Seated it drapes forward over
        // the thighs instead of hanging plumb
        Vector3 hem = seated
            ? Vector3{base.x + f.x * 0.22f * a.height, hipY - 0.12f * a.height,
                      base.z + f.z * 0.22f * a.height}
            : Vector3{base.x, hipY - 0.24f * a.height, base.z};
        rlNormal3f(f.x, 0.25f, f.z);
        DrawCylinderEx({base.x, hipY + 0.14f * a.height, base.z}, hem,
                       waistW * 0.98f, hipW * (seated ? 1.0f : 1.30f), 12, a.pants);
    }
    // button placket on the chest - tilted to match the shirt's taper and
    // kept well clear of the waist so it never touches the pants edge
    {
        Color plk{(unsigned char)std::max(0, (int)a.shirt.r - 26),
                  (unsigned char)std::max(0, (int)a.shirt.g - 26),
                  (unsigned char)std::max(0, (int)a.shirt.b - 26), 255};
        float rise = shoulderY - (hipY + 0.10f * a.height);
        float taper = atan2f(shoulderW * breathe - waistW * 0.98f, rise);
        float midY = hipY + 0.10f * a.height + rise * 0.64f;
        float midR = waistW * 0.98f + (shoulderW - waistW * 0.98f) * 0.64f;
        rlPushMatrix();
        rlTranslatef(base.x + f.x * lean * 0.6f * a.height, midY,
                     base.z + f.z * lean * 0.6f * a.height);
        rlRotatef(yawDeg, 0, 1, 0);
        rlRotatef(taper * RAD2DEG, 1, 0, 0);   // top leans out with the chest
        DrawCubeV({0, 0, midR + 0.004f},
                  {0.042f * a.height, rise * 0.60f, 0.014f}, plk);
        if (a.tie) {
            // knot tucked under the collar, blade riding the placket's taper
            DrawCubeV({0, rise * 0.30f, midR + 0.011f},
                      {0.034f * a.height, 0.030f * a.height, 0.016f}, a.tieCol);
            DrawCubeV({0, rise * 0.30f - 0.13f * a.height, midR + 0.010f},
                      {0.042f * a.height, 0.22f * a.height, 0.012f}, a.tieCol);
            DrawCubeV({0, rise * 0.30f - 0.26f * a.height, midR + 0.012f},
                      {0.026f * a.height, 0.06f * a.height, 0.011f}, a.tieCol);
        }
        if (a.badge) {
            // clip-on staff card on the wearer's left chest - the thing they
            // are here to surrender
            float bx = -0.062f * a.height;
            DrawCubeV({bx, 0.02f * a.height, midR + 0.010f},
                      {0.050f * a.height, 0.066f * a.height, 0.009f},
                      Color{212, 208, 198, 255});
            DrawCubeV({bx, 0.045f * a.height, midR + 0.015f},
                      {0.030f * a.height, 0.026f * a.height, 0.005f},
                      Color{120, 135, 150, 255});                     // photo block
            DrawCubeV({bx, -0.006f * a.height, midR + 0.015f},
                      {0.036f * a.height, 0.008f * a.height, 0.005f},
                      Color{96, 94, 100, 255});                       // name line
            DrawCubeV({bx, 0.058f * a.height, midR + 0.013f},
                      {0.014f * a.height, 0.008f * a.height, 0.006f},
                      Color{52, 50, 56, 255});                        // clip
        }
        {   // collar wings: two flat flaps angled into a V over the tie knot -
            // sells the button-up under everything from across the desk
            Color cc{(unsigned char)std::min(255, (int)a.shirt.r + 22),
                     (unsigned char)std::min(255, (int)a.shirt.g + 22),
                     (unsigned char)std::min(255, (int)a.shirt.b + 22), 255};
            for (int s = -1; s <= 1; s += 2) {
                rlPushMatrix();
                rlTranslatef(s * 0.033f * a.height, rise * 0.345f, midR + 0.009f);
                rlRotatef(s * 24.f, 0, 0, 1);        // inner edge dips to the V
                DrawCubeV({0, 0, 0}, {0.052f * a.height, 0.018f * a.height, 0.014f}, cc);
                rlPopMatrix();
            }
        }
        rlPopMatrix();
    }
    // detain bow: the whole head assembly (head, hair, eyes) tips forward
    // and down off a single anchor instead of dangling a lowered head alone
    Vector3 hb{top.x + f.x * 0.05f * detain, headY - 0.06f * a.height * detain,
               top.z + f.z * 0.05f * detain};
    // trapezius slope + neck - shoulders flow into the neck instead of a
    // ball perched on a stick
    rlNormal3f(f.x, 0.4f, f.z);
    DrawCylinderEx({top.x, shoulderY - 0.035f * a.height, top.z},
                   {hb.x, hb.y - 0.045f * a.height, hb.z},
                   0.075f * a.height, 0.042f * a.height, 8, a.skin);
    // head: skull sphere plus a smaller jaw wedge below-forward so the
    // silhouette has a chin instead of reading as a billiard ball
    Sph({hb.x + f.x * 0.012f, hb.y - 0.05f * a.height, hb.z + f.z * 0.012f},
        0.085f * a.height, a.skin);
    Sph(hb, 0.109f * a.height, a.skin);
    // ears - cheap, but the side silhouette is instantly "person"
    for (int s = -1; s <= 1; s += 2)
        Sph({hb.x + r.x * 0.099f * s * a.height, hb.y - 0.005f * a.height,
             hb.z + r.z * 0.099f * s * a.height}, 0.021f * a.height, a.skin);
    // nose tip - a small bridge + nub proud of the face so the profile
    // doesn't read as a featureless mask
    {
        Color ns{(unsigned char)std::max(0, (int)a.skin.r - 16),
                 (unsigned char)std::max(0, (int)a.skin.g - 14),
                 (unsigned char)std::max(0, (int)a.skin.b - 12), 255};
        Sph({hb.x + f.x * 0.104f * a.height, hb.y - 0.016f * a.height,
             hb.z + f.z * 0.104f * a.height}, 0.016f * a.height, ns);
        Sph({hb.x + f.x * 0.108f * a.height, hb.y - 0.034f * a.height,
             hb.z + f.z * 0.108f * a.height}, 0.012f * a.height, a.skin);
    }
    // hair - the cap is the default cut; everything else reads as a
    // different silhouette from across the room. Every cap sphere MUST be
    // bigger than the 0.109 head sphere - a smaller cap intersects the
    // skull and its exposed sliver reads as a floating ring halo
    switch (a.hairStyle) {
        case 1:   // buzz: tight to the skull
            Sph({hb.x, hb.y + 0.042f * a.height, hb.z - f.z * 0.008f}, 0.112f * a.height, a.hair);
            break;
        case 2:   // long: cap plus a fall of hair down the back
            Sph({hb.x - f.x * 0.02f, hb.y + 0.028f * a.height, hb.z - f.z * 0.02f}, 0.115f * a.height, a.hair);
            rlNormal3f(-f.x, -0.3f, -f.z);
            DrawCylinderEx({hb.x - f.x * 0.08f * a.height, hb.y + 0.01f, hb.z - f.z * 0.08f * a.height},
                           {hb.x - f.x * 0.10f * a.height, hb.y - 0.20f * a.height, hb.z - f.z * 0.10f * a.height},
                           0.055f * a.height, 0.045f * a.height, 6, a.hair);
            break;
        case 3:   // ponytail: cap, a tied knot, and a short tail
            Sph({hb.x - f.x * 0.02f, hb.y + 0.028f * a.height, hb.z - f.z * 0.02f}, 0.115f * a.height, a.hair);
            Sph({hb.x - f.x * 0.11f * a.height, hb.y + 0.075f * a.height, hb.z - f.z * 0.11f * a.height},
                0.045f * a.height, a.hair);
            rlNormal3f(-f.x, -0.4f, -f.z);
            DrawCylinderEx({hb.x - f.x * 0.10f * a.height, hb.y + 0.06f, hb.z - f.z * 0.10f * a.height},
                           {hb.x - f.x * 0.11f * a.height, hb.y - 0.10f * a.height, hb.z - f.z * 0.11f * a.height},
                           0.038f * a.height, 0.030f * a.height, 6, a.hair);
            break;
        case 4:   // bun: cap plus a knot at the back of the skull
            Sph({hb.x - f.x * 0.02f, hb.y + 0.028f * a.height, hb.z - f.z * 0.02f}, 0.115f * a.height, a.hair);
            Sph({hb.x - f.x * 0.115f * a.height, hb.y + 0.05f * a.height, hb.z - f.z * 0.115f * a.height},
                0.055f * a.height, a.hair);
            break;
        case 5:   // balding: wraps the skull but pushed back/low - bare crown
            Sph({hb.x - f.x * 0.05f, hb.y - 0.012f * a.height, hb.z - f.z * 0.05f}, 0.113f * a.height, a.hair);
            break;
        case 6:   // bald: nothing
            break;
        default:  // cap
            Sph({hb.x - f.x * 0.02f, hb.y + 0.028f * a.height, hb.z - f.z * 0.02f}, 0.115f * a.height, a.hair);
            break;
    }
    // face - sclera/iris/pupil are queued for the unlit pass (see
    // DrawHumanoidEyes); in the lit pass they'd read as dark holes
    if (a.eyes != "none") {
        Color ec = (a.eyes == "black") ? Color{5, 5, 8, 255} : Color{222, 220, 214, 255};
        float er = (a.eyes == "black") ? 0.026f : 0.0185f;
        for (int s = -1; s <= 1; s += 2) {
            Vector3 ep{hb.x + f.x * 0.099f * a.height + r.x * 0.040f * s * a.height,
                       hb.y + 0.006f * a.height,
                       hb.z + f.z * 0.099f * a.height + r.z * 0.040f * s * a.height};
            EyeDraw e{ep, {}, {}, er * a.height, 0, 0, ec, {}, false};
            if (a.eyes == "normal") {
                // iris+pupil slide toward `look` - eyes track whoever you're
                // talking to. Offset is clamped small so the iris never leaves
                // the sclera disc; normal eyes only, mutants don't glance
                float gx = 0.f, gy = 0.f;
                if (look.x != 0.f || look.y != 0.f || look.z != 0.f) {
                    Vector3 g{look.x - ep.x, look.y - ep.y, look.z - ep.z};
                    float gl = std::sqrt(g.x*g.x + g.y*g.y + g.z*g.z);
                    if (gl > 0.001f) {
                        g.x /= gl; g.y /= gl; g.z /= gl;
                        gx = std::clamp(g.x * r.x + g.z * r.z, -1.f, 1.f) * 0.006f * a.height;
                        gy = std::clamp(g.y, -1.f, 1.f) * 0.004f * a.height;
                    }
                }
                e.iris = true;
                e.ir = {ep.x + f.x * 0.016f * a.height + r.x * gx,
                        ep.y + gy,
                        ep.z + f.z * 0.016f * a.height + r.z * gx};
                e.irR = 0.0102f * a.height;
                e.ic = a.eyeCol;
                e.pu = {ep.x + f.x * 0.021f * a.height + r.x * gx * 1.4f,
                        ep.y + gy * 1.4f,
                        ep.z + f.z * 0.021f * a.height + r.z * gx * 1.4f};
                e.puR = 0.0048f * a.height;
            }
            g_eyeDraws.push_back(e);
        }
        // eyelid + mouth - bare sclera spheres read as glowing orbs; a skin
        // strip shading the top edge and a thin lip line ground the face
        if (a.eyes != "black") {
            Color lid{(unsigned char)std::max(0, (int)a.skin.r - 24),
                      (unsigned char)std::max(0, (int)a.skin.g - 20),
                      (unsigned char)std::max(0, (int)a.skin.b - 18), 255};
            for (int s = -1; s <= 1; s += 2) {
                rlPushMatrix();
                rlTranslatef(hb.x + f.x * 0.114f * a.height + r.x * 0.040f * s * a.height,
                             hb.y + 0.020f * a.height,
                             hb.z + f.z * 0.114f * a.height + r.z * 0.040f * s * a.height);
                rlRotatef(yawDeg, 0, 1, 0);
                DrawCubeV({0, 0, 0}, {0.030f * a.height, 0.009f * a.height, 0.008f}, lid);
                rlPopMatrix();
            }
            rlPushMatrix();
            rlTranslatef(hb.x + f.x * 0.103f * a.height, hb.y - 0.048f * a.height,
                         hb.z + f.z * 0.103f * a.height);
            rlRotatef(yawDeg, 0, 1, 0);
            DrawCubeV({0, 0, 0}, {0.028f * a.height, 0.0045f * a.height, 0.008f},
                      Color{(unsigned char)(a.skin.r / 2 + 60), (unsigned char)(a.skin.g / 3 + 30),
                            (unsigned char)(a.skin.b / 3 + 30), 255});
            rlPopMatrix();
        }
        // eyebrows - a bare pale forehead with two eye-dots reads as a
        // bowling ball; thin hair-colored bars give the face structure
        if (a.eyes != "black") {
            for (int s = -1; s <= 1; s += 2) {
                Vector3 bp{hb.x + f.x * 0.096f * a.height + r.x * 0.040f * s * a.height,
                           hb.y + 0.038f * a.height,
                           hb.z + f.z * 0.096f * a.height + r.z * 0.040f * s * a.height};
                rlPushMatrix();
                rlTranslatef(bp.x, bp.y, bp.z);
                rlRotatef(yawDeg, 0, 1, 0);
                DrawCubeV({0, 0, 0}, {0.032f * a.height, 0.008f * a.height, 0.012f}, a.hair);
                rlPopMatrix();
            }
        }
        // glasses: two square wire rims proud of the face, a bridge over the
        // nose line, and temple arms running back to the ears
        if (a.glasses) {
            float hh = a.height;
            rlPushMatrix();
            rlTranslatef(hb.x + f.x * 0.1045f * hh, hb.y + 0.006f * hh,
                       hb.z + f.z * 0.1045f * hh);
            rlRotatef(yawDeg, 0, 1, 0);
            Color gc = a.glassesCol;
            for (int s = -1; s <= 1; s += 2) {
                float lx = s * 0.040f * hh;                        // lens centre
                DrawCubeV({lx, 0.031f * hh, 0}, {0.064f * hh, 0.007f * hh, 0.007f}, gc);   // top rim
                DrawCubeV({lx, -0.031f * hh, 0}, {0.064f * hh, 0.007f * hh, 0.007f}, gc);  // bottom rim
                DrawCubeV({lx + s * 0.032f * hh, 0, 0}, {0.007f * hh, 0.062f * hh, 0.007f}, gc); // outer
                DrawCubeV({lx - s * 0.032f * hh, 0, 0}, {0.007f * hh, 0.062f * hh, 0.007f}, gc); // inner
                // temple arm: outer rim back toward the ear
                DrawCubeV({lx + s * 0.040f * hh, 0.002f, -0.052f * hh},
                          {0.006f * hh, 0.006f * hh, 0.104f * hh}, gc);
            }
            DrawCubeV({0, 0.012f * hh, 0}, {0.026f * hh, 0.006f * hh, 0.006f}, gc);        // bridge
            rlPopMatrix();
        }
    }
    // beard: chin patch + jaw straps in darkened hair; a full beard adds the
    // moustache bar and a longer chin mass
    if (a.beard > 0) {
        float hh = a.height;
        Color bc{(unsigned char)std::max(0, (int)a.hair.r - 8),
                 (unsigned char)std::max(0, (int)a.hair.g - 8),
                 (unsigned char)std::max(0, (int)a.hair.b - 8), 255};
        rlPushMatrix();
        rlTranslatef(hb.x, hb.y, hb.z);
        rlRotatef(yawDeg, 0, 1, 0);
        DrawCubeV({0, -0.062f * hh, 0.078f * hh}, {0.092f * hh, 0.052f * hh, 0.032f * hh}, bc); // chin
        for (int s = -1; s <= 1; s += 2)
            DrawCubeV({s * 0.080f * hh, -0.028f * hh, 0.038f * hh},
                      {0.030f * hh, 0.072f * hh, 0.052f * hh}, bc);                            // jaw strap
        if (a.beard > 1) {
            DrawCubeV({0, -0.030f * hh, 0.094f * hh}, {0.072f * hh, 0.016f * hh, 0.024f * hh}, bc); // moustache
            DrawCubeV({0, -0.088f * hh, 0.058f * hh}, {0.100f * hh, 0.034f * hh, 0.028f * hh}, bc); // fuller chin
        }
        rlPopMatrix();
    }
    // arms - two segments with a real elbow; armRaise lerps hands from
    // hanging to raised overhead (ladder work), detain pins wrists behind
    // the back (security escort). Shoulders anchor on `top` so they lean
    // with the spine instead of floating off it
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh{top.x + r.x * shoulderW * 1.02f * s, shoulderY - 0.05f * a.height,
                   top.z + r.z * shoulderW * 1.02f * s};
        Vector3 hand, elbow;
        if (detain > 0.f) {
            // cuffed: wrists crossed low over the tailbone
            hand = {base.x - f.x * 0.15f * a.height + r.x * 0.05f * s * a.height,
                    hipY + 0.01f,
                    base.z - f.z * 0.15f * a.height + r.z * 0.05f * s * a.height};
            elbow = {sh.x - f.x * 0.06f + r.x * 0.03f * s, hipY + 0.24f * a.height,
                     sh.z - f.z * 0.06f + r.z * 0.03f * s};
        } else if (seated) {
            // forearms lie along the thighs - upper arm drops to an elbow at
            // the hip, hand slides forward to rest near the knee
            hand = {base.x + r.x * 0.105f * s * a.height + f.x * 0.23f * a.height,
                    hipY + 0.015f * a.height,
                    base.z + r.z * 0.105f * s * a.height + f.z * 0.23f * a.height};
            elbow = {sh.x + r.x * 0.055f * s + f.x * 0.025f,
                     hipY + 0.19f * a.height,
                     sh.z + r.z * 0.055f * s + f.z * 0.025f};
        } else {
            // relaxed hang: hands land mid-thigh close to the body, a touch
            // forward - elbows bend toward the front, never wing out to the
            // sides. Swing stays small; big arcs read as marching robots
            float armSw = -std::sin(animT) * s * 0.07f * wA;
            Vector3 lo{base.x + r.x * (shoulderW + 0.005f) * s + f.x * (sway + armSw + 0.035f),
                       hipY - 0.31f * a.height,
                       base.z + r.z * (shoulderW + 0.005f) * s + f.z * (sway + armSw + 0.035f)};
            Vector3 hi{sh.x + r.x * 0.24f * s + f.x * 0.22f, shoulderY + 0.46f * a.height, sh.z + r.z * 0.24f * s + f.z * 0.22f};
            hand = {lo.x + (hi.x - lo.x) * armRaise, lo.y + (hi.y - lo.y) * armRaise, lo.z + (hi.z - lo.z) * armRaise};
            // elbow points slightly BACK - the olecranon apex is behind the
            // arm line; pushed forward it reads as a backwards joint
            float eb = 0.05f * (1.f - armRaise);
            elbow = {(sh.x + hand.x) * 0.5f - f.x * eb + r.x * 0.008f * s,
                     (sh.y + hand.y) * 0.5f + 0.01f,
                     (sh.z + hand.z) * 0.5f - f.z * eb + r.z * 0.008f * s};
        }
        rlNormal3f(f.x + r.x * 0.5f * s, 0.1f, f.z + r.z * 0.5f * s);
        // deltoid pad covers the torso->sleeve joint so the arm flows out
        // of the shoulder rather than bolting on
        Sph(sh, 0.068f * a.height, a.shirt);
        DrawCylinderEx(sh, elbow, 0.054f * a.height, 0.046f * a.height, 7, a.shirt);
        Sph(elbow, 0.044f * a.height, a.shirt);
        // forearm reads as a rolled sleeve - slightly slimmer, same fabric
        DrawCylinderEx(elbow, hand, 0.041f * a.height, 0.033f * a.height, 7, a.shirt);
        Sph(hand, 0.039f * a.height, a.skin);
    }
    // legs - women in skirts get bare legs; the skirt hem sits mid-thigh
    Color legCol = a.female ? a.skin : a.pants;
    for (int s = -1; s <= 1; s += 2) {
        // thighs attach close enough that their tops overlap in the middle -
        // the pair merges into one pants mass instead of a skirt over pegs
        Vector3 hip{base.x + r.x * 0.058f * s * a.height, hipY + 0.04f * a.height,
                    base.z + r.z * 0.058f * s * a.height};
        if (seated) {
            // thigh runs forward off the seat edge, shin drops just past it.
            // knee never sinks below a believable bend: on a tall seat the
            // shin stays near-vertical instead of folding to the floor
            float kneeY = std::max(feet.y + 0.28f * a.height, hipY - 0.10f);
            Vector3 knee{hip.x + f.x * 0.30f, kneeY, hip.z + f.z * 0.30f};
            Vector3 foot{knee.x + f.x * 0.05f, feet.y + 0.02f, knee.z + f.z * 0.05f};
            rlNormal3f(f.x, 0.15f, f.z);
            DrawCylinderEx(hip, knee, 0.090f * a.height, 0.056f * a.height, 8, legCol);
            DrawCylinderEx(knee, foot, 0.054f * a.height, 0.042f * a.height, 8, legCol);
            // shoe: low box plus a rounded toe - no floating bricks
            rlPushMatrix();
            rlTranslatef(foot.x, feet.y + 0.026f, foot.z);
            rlRotatef(yawDeg, 0, 1, 0);
            DrawCubeV({0, 0, 0.05f * a.height}, {0.085f * a.height, 0.045f, 0.13f * a.height},
                      Color{26, 25, 30, 255});
            rlPopMatrix();
            Sph({foot.x + f.x * 0.115f * a.height, feet.y + 0.026f,
                 foot.z + f.z * 0.115f * a.height}, 0.045f * a.height, Color{26, 25, 30, 255});
        } else {
            // walk cycle: legs swing fore/aft in alternate phase, the foot
            // lifts a touch on its back-to-front pass, and the knee folds
            // forward while the leg is airborne. A permanent small bend
            // keeps planted legs from reading as plumb rods
            float ph = std::sin(animT) * s;
            float lift = std::max(0.f, std::cos(animT) * s) * 0.055f * wA;
            Vector3 foot{hip.x + f.x * 0.15f * ph * wA, feet.y + 0.02f + lift,
                         hip.z + f.z * 0.15f * ph * wA};
            float kBend = 0.024f + lift * 1.8f + detain * 0.02f;
            Vector3 knee{(hip.x + foot.x) * 0.5f + f.x * kBend,
                         (hipY + foot.y) * 0.5f + 0.02f * wA * lift * 20.f,
                         (hip.z + foot.z) * 0.5f + f.z * kBend};
            rlNormal3f(f.x, 0.15f, f.z);
            DrawCylinderEx(hip, knee, 0.088f * a.height, 0.055f * a.height, 8, legCol);
            DrawCylinderEx(knee, foot, 0.052f * a.height, 0.041f * a.height, 8, legCol);
            // shoe: low box plus a rounded toe
            rlPushMatrix();
            rlTranslatef(foot.x, feet.y + 0.026f, foot.z);
            rlRotatef(yawDeg, 0, 1, 0);
            DrawCubeV({0, 0, 0.05f * a.height}, {0.088f * a.height, 0.045f, 0.13f * a.height},
                      Color{26, 25, 30, 255});
            rlPopMatrix();
            Sph({foot.x + f.x * 0.115f * a.height, feet.y + 0.026f,
                 foot.z + f.z * 0.115f * a.height}, 0.046f * a.height, Color{26, 25, 30, 255});
        }
    }
}

// ---------------- VisitorSystem ----------------

void VisitorSystem::Init(const json& vj, const json& cfg) {
    data = vj;
    schedule = vj["schedule"];
    schedTemplate = vj["schedule"];   // slot pattern preserved for roster rebuilds
    schedFull = vj["schedule"];       // full book before the per-night cap
    doorWaitSec = JF(cfg, "door_wait_sec", 55);
    hostileRevealSec = JF(cfg, "hostile_reveal_sec", 70);
    hostileGraceSec = JF(cfg, "hostile_grace_sec", 45);
    softLen = JF(cfg, "interview_soft_length_sec", 150);
    walkSpeed = JF(cfg, "walk_speed", 1.15f);
    Reset();
}

void VisitorSystem::Reset(bool freshNight) {
    cur = Visitor{};
    transcript.clear();
    ambient.clear();
    scheduleIdx = 0;
    // a fresh night re-rolls the lore slot so the persona is rare, not
    // permanent; loading a save keeps the book exactly as it was saved
    if (freshNight) InjectLore();
    // tonight's book = the full cast list cut to this night's difficulty cap
    schedule = schedFull.empty() ? schedTemplate : schedFull;
    if ((int)schedule.size() > slotCap) schedule.erase(schedule.begin() + slotCap, schedule.end());
    // night 0 is the scripted orientation: a fixed two-appointment book -
    // one plain interview, then one whose file disagrees with them. The
    // second slot is dispatched by the tutorial once the lessons land.
    if (night == 0 && data.contains("tutorial_schedule") && !data["tutorial_schedule"].empty())
        schedule = data["tutorial_schedule"];
    // ...and the anomalous share of it is capped too. Stock nights front-load
    // 4 anomaly slots in a 6-visitor book - four naive releases was +56
    // instability and a dead first shift. Keep the earliest tags (the
    // learnable ones), demote the overflow to mundane interviews. "repeat"
    // slots are exempt: a demoted repeat still books the same face twice,
    // which reads as an unmarked anomaly - leave those alone entirely.
    {
        int an = 0;
        for (auto& e : schedule) {
            std::string a = JS(e, "anomaly", "");
            if (a.empty() || a == "repeat") continue;
            if (++an > anomalyCap) {
                e["anomaly"] = nullptr;
                TraceLog(LOG_INFO, "[book] night %d anomaly cap: slot demoted to mundane", night);
            }
        }
    }
    for (auto& e : schedule) e.erase("visited");   // fresh night, nothing done yet
    if (const char* e = std::getenv("EI_VISIT"))   // dev: jump the schedule
        scheduleIdx = std::clamp(atoi(e), 0, (int)schedule.size());
    waitingEmpty = false;
    figureActive = false;
    exteriorFigure.clear();
    ambientTimer = 15.f;
    knocked = false;
    bangerActive = false; bangerAt.clear(); bangT = 0;
    ambientMutated = false;
}

const json* VisitorSystem::RosterFind(const std::string& id) const {
    for (auto& p : data["roster"]) if (JS(p, "id") == id) return &p;
    return nullptr;
}

// Parse + validate an LLM roster array, then rebuild the night's schedule:
// normal slots get normal personas, "repeat" reuses an earlier visitor, and
// hostile/impossible slots prefer personas flagged always_anomaly for that.
bool VisitorSystem::ApplyRosterJson(const std::string& text) {
    auto a = text.find('['), b = text.rfind(']');
    if (a == std::string::npos || b <= a) return false;
    json arr;
    try { arr = json::parse(text.substr(a, b - a + 1)); }
    catch (...) { return false; }
    if (!arr.is_array() || arr.size() < 5) return false;

    auto cleanStr = [](const json& j, const char* k, size_t mx) {
        std::string s = SanitizeText(JS(j, k));   // curly ' etc fold to ASCII
        if (s.size() > mx) s.resize(mx);
        for (auto& c : s) if ((unsigned char)c < 32 || c > 126) c = ' ';
        return s;
    };
    auto cleanCol = [](const json& j) {
        Color c{160, 130, 110, 255};
        if (j.is_array() && j.size() >= 3)
            for (int i = 0; i < 3; i++)
                (&c.r)[i] = (unsigned char)std::clamp(j[i].get<int>(), 0, 255);
        return json{(int)c.r, (int)c.g, (int)c.b};
    };

    json roster = json::array();
    int idx = 0;
    for (auto& p : arr) {
        if (!p.is_object()) continue;
        std::string name = cleanStr(p, "name", 40);
        if (name.empty()) continue;
        json q;
        q["id"] = "gen_" + std::to_string(idx++);
        q["name"] = name;
        q["dept"] = cleanStr(p, "dept", 30);
        q["role"] = cleanStr(p, "role", 40);
        q["employed"] = cleanStr(p, "employed", 20);
        q["reason"] = cleanStr(p, "reason", 80);
        q["persona"] = cleanStr(p, "persona", 300);
        json ap;
        const json pap = p.value("appearance", json::object());   // missing field -> defaults, not UB
        ap["height"] = std::clamp(JF(pap, "height", 1.f), 0.9f, 1.15f);
        ap["skin"] = cleanCol(pap.value("skin", json()));
        ap["shirt"] = cleanCol(pap.value("shirt", json()));
        ap["hair"] = cleanCol(pap.value("hair", json()));
        std::string ey = JS(pap, "eyes", "normal");
        ap["eyes"] = (ey == "black" || ey == "none") ? ey : "normal";
        // pass through the optional appearance fields the prompt asks for -
        // they used to be dropped on the floor, flattening the cast variety
        std::string g = ToLower(JS(pap, "gender", JS(pap, "sex", "")));
        if (g == "female" || g == "male") ap["gender"] = g;
        if (pap.contains("hair_style")) {
            if (pap["hair_style"].is_number())
                ap["hair_style"] = std::clamp(pap["hair_style"].get<int>(), 0, 6);
            else {
                std::string hs = ToLower(JS(pap, "hair_style"));
                static const char* names[] = {"cap","buzz","long","ponytail","bun","balding","bald"};
                for (int i = 0; i < 7; i++) if (hs == names[i]) ap["hair_style"] = i;
            }
        }
        if (pap.contains("pants")) ap["pants"] = cleanCol(pap["pants"]);
        if (pap.contains("eye_color")) ap["eye_color"] = cleanCol(pap["eye_color"]);
        if (pap.contains("glasses")) {
            const json& gv = pap["glasses"];
            ap["glasses"] = gv.is_array() ? cleanCol(gv) : json(gv.is_boolean() && gv.get<bool>());
        }
        if (pap.contains("beard")) {
            const json& bv = pap["beard"];
            if (bv.is_number()) ap["beard"] = std::clamp(bv.get<int>(), 0, 2);
            else if (bv.is_boolean()) ap["beard"] = bv.get<bool>();
            else {
                std::string bs = ToLower(JS(pap, "beard"));
                if (bs == "full" || bs == "stubble" || bs == "goatee") ap["beard"] = bs;
            }
        }
        if (pap.contains("tie")) {
            const json& tv = pap["tie"];
            ap["tie"] = tv.is_array() ? cleanCol(tv) : json(tv.is_boolean() && tv.get<bool>());
        }
        if (pap.contains("badge") && pap["badge"].is_boolean()) ap["badge"] = pap["badge"].get<bool>();
        q["appearance"] = ap;
        std::string aa = JS(p, "always_anomaly");
        if (aa == "hostile" || aa == "impossible_visitor") q["always_anomaly"] = aa;
        // visibly-wrong people must count as anomalies - a black-eyed
        // persona in a "normal" slot punishes a correct Security call
        if (JS(q["appearance"], "eyes", "normal") != "normal" && !q.contains("always_anomaly"))
            q["always_anomaly"] = "impossible_visitor";
        // a persona literally described as "ANOMALOUS ..." must always count
        // as an anomaly too, flag or no flag
        {
            std::string ps = JS(q, "persona");
            size_t p0 = ps.find_first_not_of(" \t");
            if (p0 != std::string::npos && ps.size() - p0 >= 9) {
                bool anom = true;
                for (size_t i = 0; i < 9; i++) {
                    char ch = ps[p0 + i];
                    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                    if (ch != "ANOMALOUS"[i]) { anom = false; break; }
                }
                if (anom && !q.contains("always_anomaly"))
                    q["always_anomaly"] = "impossible_visitor";
            }
        }
        roster.push_back(q);
    }
    if (roster.size() < 5) return false;

    // rebuild the night's schedule: same times/anomaly tags, fresh faces.
    // "repeat" slots reuse the first slot's visitor; anomaly slots prefer
    // personas whose always_anomaly matches the slot's tag.
    json sched = json::array();
    std::vector<int> normals, impossibles, hostiles;
    for (int i = 0; i < (int)roster.size(); i++) {
        std::string aa = JS(roster[i], "always_anomaly");
        if (aa == "impossible_visitor") impossibles.push_back(i);
        else if (aa == "hostile") hostiles.push_back(i);
        else normals.push_back(i);
    }
    if (normals.empty()) return false;
    int ni = 0, firstVisitor = -1;
    auto take = [&](std::vector<int>& pool) -> int {
        if (!pool.empty()) { int v = pool.front(); pool.erase(pool.begin()); return v; }
        return normals[(ni++) % (int)normals.size()];
    };
    for (auto& slot : schedTemplate) {
        json e = slot;
        std::string an = JS(slot, "anomaly", "");
        // night gates demote the SLOT before casting - an always-anomalous
        // persona planted in a mundane interview still reads as the anomaly
        // (deceased file, impossible dialogue) while resolving as normal
        if (night < 2 && an == "impossible_visitor") an.clear();
        if (night < 3 && an == "hostile") an.clear();
        e["anomaly"] = an.empty() ? json(nullptr) : json(an);
        int pick;
        if (an == "repeat" && firstVisitor >= 0) pick = firstVisitor;
        else if (an == "impossible_visitor") pick = take(impossibles);
        else if (an == "hostile") pick = take(hostiles);
        else pick = normals[(ni++) % (int)normals.size()];
        if (firstVisitor < 0 && an != "repeat") firstVisitor = pick;
        e["visitor"] = JS(roster[pick], "id");
        sched.push_back(e);
    }
    // nobody books twice in one night - any persona the pool wraparound
    // planted twice is, by definition, coming back. AUTO-REJECTED in the
    // book and anomalous in play: the second listing can never be a
    // mundane interview (repeat entries resolve through Security).
    {
        std::set<std::string> booked;
        for (auto& e : sched) {
            std::string id = JS(e, "visitor");
            if (id.empty()) continue;
            if (!booked.insert(id).second && JS(e, "anomaly", "").empty())
                e["anomaly"] = "repeat";
        }
    }
    data["roster"] = roster;
    data["schedule"] = sched;
    schedFull = sched;
    schedule = sched;
    InjectLore();
    return true;
}

// rare lore encounter: a persona with office history quietly takes one of the
// mundane schedule slots - mechanically normal, the interview is the story.
// They book like anyone else; the file is what's wrong.
void VisitorSystem::InjectLore() {
    // restore any displaced bookings first - a lore visitor is a guest for
    // ONE night, not a permanent fixture of the book
    for (auto& e : schedFull)
        if (e.contains("visitor_orig")) {
            e["visitor"] = e["visitor_orig"];
            e.erase("visitor_orig");
        }
    if (night < 2) return;
    if (!data.contains("lore_personas") || data["lore_personas"].empty()) return;
    if (RandRange(0.f, 1.f) > 0.45f) return;
    // writes into schedFull - the live book is rebuilt from it on Reset
    std::vector<int> cands;
    for (int i = 1; i < (int)schedFull.size() - 1; i++)
        if (JS(schedFull[i], "anomaly", "").empty() &&
            JS(schedFull[i], "visitor").rfind("lore_", 0) != 0)
            cands.push_back(i);
    if (cands.empty()) return;
    int slot = cands[RandInt(0, (int)cands.size() - 1)];
    // prefer personas the player hasn't sat across from yet - the cast is a
    // collection to complete across nights, not a dice roll that repeats
    const json* lp = &data["lore_personas"][RandInt(0, (int)data["lore_personas"].size() - 1)];
    if (loreSeen) {
        std::vector<const json*> fresh;
        for (auto& p : data["lore_personas"])
            if (!loreSeen->count(JS(p, "id"))) fresh.push_back(&p);
        if (!fresh.empty()) lp = fresh[RandInt(0, (int)fresh.size() - 1)];
    }
    std::string id = JS(*lp, "id");
    if (!RosterFind(id)) data["roster"].push_back(*lp);
    schedFull[slot]["visitor_orig"] = JS(schedFull[slot], "visitor");  // displaced booking
    schedFull[slot]["visitor"] = id;
    if (slot < (int)schedule.size()) schedule[slot]["visitor"] = id;
    TraceLog(LOG_INFO, "[lore] %s slipped into tonight's book (slot %d)", id.c_str(), slot);
}

int VisitorSystem::PendingCount() const {
    return (int)schedule.size() - scheduleIdx;
}

void VisitorSystem::WalkAlong(Visitor& v, const std::vector<Vector2>& path, float dt, float speed) {
    if (v.wp >= (int)path.size()) return;
    Vector2 t = path[v.wp];
    Vector2 d{t.x - v.pos.x, t.y - v.pos.z};
    float dist = std::sqrt(d.x * d.x + d.y * d.y);
    if (dist < 0.08f) { v.wp++; return; }
    d.x /= dist; d.y /= dist;
    v.pos.x += d.x * speed * dt;
    v.pos.z += d.y * speed * dt;
    v.yaw = std::atan2(d.x, d.y) * RAD2DEG;
    v.animT += dt * 6.f;
}

void VisitorSystem::SpawnScheduled(Office& office) {
    if (scheduleIdx >= (int)schedule.size()) return;
    auto& e = schedule[scheduleIdx];
    const json* person = RosterFind(JS(e, "visitor"));
    if (!person) { scheduleIdx++; return; }
    cur = Visitor{};
    cur.person = person;
    cur.anomaly = JS(e, "anomaly", "");
    if (cur.anomaly.empty() && person->contains("always_anomaly"))
        cur.anomaly = JS(*person, "always_anomaly");
    // early nights keep the lethal/harder slots out of the rotation
    if (night < 2 && cur.anomaly == "impossible_visitor") cur.anomaly.clear();
    if (night < 3 && cur.anomaly == "hostile") cur.anomaly.clear();
    // a demoted slot can't keep a persona built to be wrong: the file still
    // reads "deceased", the dialogue still plays dead - while nothing is
    // mechanically wrong, so Security rightly reports a clean room. Swap in
    // a plain employee and fix the appointment-book entry so they agree.
    if (cur.anomaly.empty() && person->contains("always_anomaly")) {
        int n = (int)data["roster"].size();
        for (int k = 0; n > 0 && k < n; k++) {
            const json& r = data["roster"][(scheduleIdx + k) % n];
            if (r.contains("always_anomaly")) continue;
            TraceLog(LOG_INFO, "[visitor] night-demoted slot: %s swapped for mundane persona %s",
                     JS(*person, "id").c_str(), JS(r, "id").c_str());
            person = &r; cur.person = person;
            schedule[scheduleIdx]["visitor"] = JS(r, "id");
            break;
        }
    }
    cur.app = AppFromJson(*person);
    // invariant: a visitor only LOOKS wrong when they ARE wrong - clears
    // odd features on slots the night gate just made mundane
    if (cur.anomaly.empty()) cur.app.eyes = "normal";
    cur.state = VState::ToDoor;
    cur.wp = 0;
    cur.slotIdx = scheduleIdx;
    // spawn at hall door (first point of path)
    auto& path = office.paths["visitor_to_door"];
    cur.pos = {path[0].x, 0, path[0].y};
    transcript.clear();
    scheduleIdx++;
    knocked = false;
}

std::string VisitorSystem::NextName() const {
    if (scheduleIdx >= (int)schedule.size()) return "";
    const json* p = RosterFind(JS(schedule[scheduleIdx], "visitor"));
    return p ? JS(*p, "name") : std::string();
}

bool VisitorSystem::CallNextEarly(Office& office) {
    if (cur.Active() || scheduleIdx >= (int)schedule.size()) return false;
    SpawnScheduled(office);
    return true;
}

void VisitorSystem::InviteIn() {
    // arriving at an already-open door calls this while still ToDoor -
    // same pivot onto the chair path, which starts just inside the door
    if (cur.state == VState::AtDoor || cur.state == VState::ToDoor) {
        cur.state = VState::ToChair;
        cur.wp = 0;
    }
}

void VisitorSystem::EndInterview() {
    if (cur.state == VState::Seated) {
        cur.state = VState::Leaving;
        cur.wp = 0;
        cur.seated = false;
    }
}

void VisitorSystem::SecurityRemove() {
    if (cur.Active()) {
        cur.state = VState::Leaving;
        cur.wp = 0;
        cur.seated = false;
        cur.escorted = true;   // a handled exit - no release penalty at Gone
        cur.detained = true;   // guards drive the position - cuffed walk out
    }
}

bool VisitorSystem::BangerStrike() {
    if (!bangerActive) return false;
    // strike lands when the swing wave wraps past its peak
    float phase = std::fmod(bangT * 5.2f, 6.28318f);
    bool hit = phase < lastBangPhase;
    lastBangPhase = phase;
    return hit;
}

void VisitorSystem::MarkHostileRevealed() {
    if (!cur.hostileStarted) { cur.hostileStarted = true; cur.hostileT = 0; }
}

void VisitorSystem::Update(float dt, float gameHour, Office& office,
                           const std::function<void(const std::string&)>& notify,
                           const std::function<void()>& onKnock,
                           const std::function<void(Vector3)>& onLunge) {
    // scheduled spawns
    if (scheduleIdx < (int)schedule.size()) {
        float t = JF(schedule[scheduleIdx], "time", -1);
        // schedule times >=24 wrap past midnight; our gameHour is absolute 18..30
        float gh = t < 12.f ? t + 24.f : t;
        if (gameHour >= gh && !cur.Active()) SpawnScheduled(office);
    }

    // the door state is sampled CONTINUOUSLY while a visitor is active -
    // never just on an arrival frame. doorSeenT = seconds since it was last
    // seen open, so a swing/shut/flicker race can never wedge a visitor at
    // the doorway; two sources (the game flag + the live panel angle) make a
    // desync between them unable to lie about it either
    {
        const Furni* doorF = office.Find("door");
        bool doorPhysOpen = doorOpen || (doorF && doorF->rotY < -25.f);
        if (cur.Active()) {
            if (doorPhysOpen) cur.doorSeenT = 0;
            else if (cur.doorSeenT < 900.f) cur.doorSeenT += dt;
        }
    }

    switch (cur.state) {
        case VState::ToDoor: {
            auto& path = office.paths["visitor_to_door"];
            WalkAlong(cur, path, dt, walkSpeed * 1.4f);
            if (cur.wp >= (int)path.size()) {
                // door open now, or open moments ago during the approach -
                // they saw it and come straight in, no AtDoor wait
                if (cur.doorSeenT < 2.f) {
                    knocked = true;
                    InviteIn();
                    notify(cur.Name() + " sees the open door and comes in.");
                } else {
                    cur.state = VState::AtDoor;
                    cur.waitT = 0;
                }
            }
            break;
        }
        case VState::AtDoor: {
            cur.yaw = 0; // yaw 0 => facing +z => toward the office door
            cur.waitT += dt;
            cur.animT += dt;   // keep the idle sway alive while waiting
            // open (or freshly-closed-mid-swing) is an invitation, checked
            // every frame; they take it almost immediately
            bool openNow = cur.doorSeenT < 0.6f;
            if (!knocked && cur.waitT > 2.5f && !openNow) { knocked = true; onKnock(); notify("Someone is at the door - use the door to invite them in."); }
            if (openNow && cur.waitT > 0.35f) {
                knocked = true;
                InviteIn();
                notify(cur.Name() + " sees the open door and comes in.");
            }
            else if (cur.waitT > doorWaitSec) { // they let themselves in
                InviteIn();
            }
            break;
        }
        case VState::ToChair: {
            auto& path = office.paths["visitor_to_chair"];
            WalkAlong(cur, path, dt, walkSpeed);
            if (cur.wp >= (int)path.size()) {
                cur.state = VState::Seated;
                cur.seated = true;
                cur.seatT = 0;
                // sit ON the chair: shift onto the seat so the backrest is
                // behind the torso, not inside it, and hips meet the seat
                cur.pos = office.VisitorSitPos();
                cur.seatH = 0.5f;
                cur.yaw = 0; // face player (south)
                notify("Interview started - toggle computer / visitor with the swap key.");
            }
            break;
        }
        case VState::Seated: {
            cur.seatT += dt;
            cur.animT += dt;
            // face the player desk
            Vector3 t = office.seatCam;
            Vector2 d{t.x - cur.pos.x, t.z - cur.pos.z};
            cur.yaw = std::atan2(d.x, d.y) * RAD2DEG;
            // visitor wraps up on their own so the schedule can't stall
            // (hostile visitors never leave on their own - that's the threat)
            if (cur.seatT > softLen && cur.anomaly != "hostile") {
                cur.state = VState::Leaving;
                cur.wp = 0;
                cur.seated = false;
                notify(cur.Name() + " wraps up the interview and heads out.");
                break;
            }
            if (cur.anomaly == "hostile" && cur.hostileStarted) {
                cur.hostileT += dt;
                if (cur.hostileT > hostileGraceSec && !cur.lunge) {
                    // it was never going to fill the paperwork out - over the
                    // desk it comes. Game decides if the lunge connects or
                    // it lands on an empty chair and walks out furious.
                    cur.lunge = true;
                    cur.lungeT = 0;
                    cur.lungeFrom = cur.pos;
                    cur.lungeTarget = office.seatCam;
                    cur.seated = false;
                }
            }
            if (cur.lunge) {
                // the vault: a fast smooth hop across the desk, arms up
                cur.lungeT += dt;
                float k = std::min(1.f, cur.lungeT / 0.42f);
                float e = k * k * (3.f - 2.f * k);
                cur.pos.x = cur.lungeFrom.x + (cur.lungeTarget.x - cur.lungeFrom.x) * e;
                cur.pos.z = cur.lungeFrom.z + (cur.lungeTarget.z - cur.lungeFrom.z) * e;
                cur.pos.y = std::sin(e * 3.14159f) * 0.5f;   // arc over the desktop
                Vector2 d{cur.lungeTarget.x - cur.pos.x, cur.lungeTarget.z - cur.pos.z};
                if (d.x * d.x + d.y * d.y > 0.002f)
                    cur.yaw = std::atan2(d.x, d.y) * RAD2DEG;
                cur.animT += dt * 14.f;
                if (cur.lungeT >= 0.42f) {
                    cur.pos.y = 0;
                    cur.lunge = false;
                    onLunge(cur.lungeFrom);
                    cur.state = VState::Gone;
                }
            }
            break;
        }
        case VState::Leaving: {
            if (cur.detained) {
                // cuffed: the escort crew moves them - feet shuffle only while
                // the detail is actually marching (detainWalk)
                if (cur.detainWalk) cur.animT += dt * 5.5f;
            } else {
                auto& path = office.paths["visitor_out"];
                WalkAlong(cur, path, dt, walkSpeed * 1.3f);
                if (cur.wp >= (int)path.size()) {
                    cur.state = VState::Gone;
                    notify("They left.");
                }
            }
            break;
        }
        default: break;
    }

    // stall watchdog: walking states must keep moving. If a waypoint or
    // transition edge ever wedges the visitor (the old open-door-at-arrival
    // InviteIn no-op did exactly this), force progress + log it loud so the
    // schedule can't silently stall behind a frozen visitor
    if (cur.state == VState::ToDoor || cur.state == VState::ToChair ||
        (cur.state == VState::Leaving && !cur.detained)) {
        float dx = cur.pos.x - stallPos.x, dz = cur.pos.z - stallPos.z;
        if (cur.state != stallState || cur.wp != stallWp || dx * dx + dz * dz > 0.0004f) {
            stallState = cur.state; stallWp = cur.wp; stallPos = cur.pos; stallT = 0;
        } else if ((stallT += dt) > 6.f) {
            TraceLog(LOG_WARNING,
                     "[visitor] STALL: %s state=%d wp=%d pos=(%.2f,%.2f) - forcing past waypoint",
                     cur.Name().c_str(), (int)cur.state, cur.wp, cur.pos.x, cur.pos.z);
            cur.wp++;
            stallT = 0;
        }
    } else if (cur.state == VState::AtDoor) {
        stallT = 0;
        // a normal wait ends at doorWaitSec; well past it something is wrong
        if (cur.waitT > doorWaitSec + 15.f) {
            TraceLog(LOG_WARNING, "[visitor] STALL: %s at door %.1fs - forcing entry",
                     cur.Name().c_str(), cur.waitT);
            InviteIn();
        }
    } else stallT = 0;

    // a visit counts as done once the visitor is actually GONE - not when
    // their booked time merely rolls past
    if (cur.state == VState::Gone && cur.slotIdx >= 0 &&
        cur.slotIdx < (int)schedule.size() &&
        !schedule[cur.slotIdx].value("visited", false))
        schedule[cur.slotIdx]["visited"] = true;

    // banging rhythm: ~1.9 strikes/sec, exposed via BangerStrike()
    if (bangerActive) bangT += dt;

    // ambient waiting-room NPCs: arrivals walk in, sit a while, and
    // eventually stand up and leave so the population churns
    ambientTimer -= dt;
    if (ambientTimer <= 0 && office.paths.count("ambient_arrival")) {
        ambientTimer = RandRange(60.f, 140.f);
        if (ambient.size() < office.waitingSeats.size() && !waitingEmpty) {
            Ambient a;
            auto& path = office.paths["ambient_arrival"];
            a.pos = {path[0].x, 0, path[0].y};
            a.path = path;
            a.wp = 0;
            a.walking = true;
            // pick a non-anomalous persona - generated rosters can flag any slot
            for (int tries = 0; tries < 8; tries++) {
                const json& p = data["roster"][RandInt(0, (int)data["roster"].size() - 1)];
                if (p.contains("always_anomaly")) continue;
                if (JS(p.value("appearance", json::object()), "eyes", "normal") != "normal") continue;
                a.app = AppFromJson(p);
                break;
            }
            // jitter the palette a little so the room isn't clones
            auto jit = [](Color& c) {
                c.r = (unsigned char)std::clamp((int)c.r + RandInt(-18, 18), 20, 235);
                c.g = (unsigned char)std::clamp((int)c.g + RandInt(-18, 18), 20, 235);
                c.b = (unsigned char)std::clamp((int)c.b + RandInt(-18, 18), 20, 235);
            };
            jit(a.app.skin); jit(a.app.shirt); jit(a.app.hair);
            a.t = 0;
            ambient.push_back(a);
        }
    }
    // exit route through the hall door: the authored path if it exists, else
    // the arrival route run backward with the door tail appended
    auto exitPath = [&office]() {
        std::vector<Vector2> p;
        if (office.paths.count("ambient_exit")) {
            p = office.paths["ambient_exit"];
        } else {
            if (office.paths.count("ambient_arrival")) {
                auto& arr = office.paths["ambient_arrival"];
                for (auto it = arr.rbegin(); it != arr.rend(); ++it) p.push_back(*it);
            }
            p.push_back({5.6f, -10.9f});
            p.push_back({5.6f, -12.55f});
            p.push_back({8.9f, -12.55f});
        }
        return p;
    };
    for (auto& a : ambient) {
        a.t += dt;
        if (a.walking) {
            if (a.wp < (int)a.path.size()) {
                Vector2 tp = a.path[a.wp];
                Vector2 d{tp.x - a.pos.x, tp.y - a.pos.z};
                float dist = std::sqrt(d.x * d.x + d.y * d.y);
                if (dist < 0.1f) a.wp++;
                else {
                    a.pos.x += d.x / dist * walkSpeed * dt;
                    a.pos.z += d.y / dist * walkSpeed * dt;
                    a.yaw = std::atan2(d.x, d.y) * RAD2DEG;
                    a.t += dt * 5.f;            // gait phase ~6 rad/s while moving
                }
            } else if (a.leaving) {
                // exit route walked to its end - erased after the loop
            } else if (a.seatIdx < 0) {
                // claim the nearest free seat, then WALK to a stand-spot in
                // front of it so the body never crosses the bench footprint
                float best = 1e9f; int bi = -1;
                for (int i = 0; i < (int)office.waitingSeats.size(); i++) {
                    bool taken = false;
                    for (auto& o : ambient)
                        if (&o != &a && o.seatIdx == i) taken = true;
                    if (taken) continue;
                    float dx = office.waitingSeats[i].pos.x - a.pos.x;
                    float dz = office.waitingSeats[i].pos.z - a.pos.z;
                    float dd = dx * dx + dz * dz;
                    if (dd < best) { best = dd; bi = i; }
                }
                if (bi >= 0) {
                    const auto& st = office.waitingSeats[bi];
                    float syr = st.yaw * DEG2RAD;
                    a.seatIdx = bi;
                    a.seatYaw = st.yaw;
                    a.seatH = st.seatH;
                    // sit ON the seat: nudge along the facing so shins clear
                    // the front edge and the backrest stays behind the torso
                    a.seatPos = {st.pos.x + std::sin(syr) * 0.08f, st.pos.y,
                                 st.pos.z + std::cos(syr) * 0.08f};
                    // pre-sit stand-spot ~0.45m out along the seat's facing
                    a.standSpot = {st.pos.x + std::sin(syr) * 0.45f, 0.f,
                                   st.pos.z + std::cos(syr) * 0.45f};
                    a.path.push_back({a.standSpot.x, a.standSpot.z});
                } else {
                    a.walking = false;          // nothing free - they just stand
                    a.leaveT = RandRange(40.f, 90.f);   // ...then give up and go
                }
            } else if (a.sitT < 0.f) {
                // reached the stand-spot: start lowering onto the seat
                a.sitT = 0.f;
                a.standSpot = a.pos;
                a.standYaw = a.yaw;
            }
        }
        // sit-down glide: horizontal lerp onto the cushion over ~0.45s while
        // the draw code drops the hips (seated pose kicks in past halfway)
        if (a.sitT >= 0.f && a.sitT < 1.f && !a.standing) {
            a.sitT += dt / 0.45f;
            float k = std::min(a.sitT, 1.f);
            k = k * k * (3.f - 2.f * k);        // smoothstep
            a.pos.x = a.standSpot.x + (a.seatPos.x - a.standSpot.x) * k;
            a.pos.z = a.standSpot.z + (a.seatPos.z - a.standSpot.z) * k;
            // turn to face the seat's direction as they lower (shortest arc)
            float dy = a.seatYaw - a.standYaw;
            while (dy > 180.f) dy -= 360.f;
            while (dy < -180.f) dy += 360.f;
            a.yaw = a.standYaw + dy * k;
            if (a.sitT >= 1.f) {
                a.pos = a.seatPos; a.yaw = a.seatYaw; a.walking = false;
                a.leaveT = RandRange(90.f, 240.f);   // settle in for a couple of minutes
            }
        }
        // settled in a seat: count down to heading out so the room churns
        // instead of going static. mutants never leave - frozen in place is
        // creepier - and the emptied room stays empty
        if (!a.walking && !a.leaving && a.seatIdx >= 0 && a.sitT >= 1.f) {
            if (!ambientMutated) a.leaveT -= dt;
            if (a.leaveT <= 0.f && !ambientMutated && !waitingEmpty) {
                bool busy = false;
                for (auto& o : ambient)
                    if (&o != &a && o.leaving) busy = true;
                if (busy) a.leaveT = 15.f;          // only one exit in progress at a time
                else { a.leaving = true; a.standing = true; a.sitT = 0.999f; }
            }
        }
        // seatless loiterers head out too, on a shorter fuse
        if (!a.walking && !a.leaving && a.seatIdx < 0) {
            if (!ambientMutated) a.leaveT -= dt;
            if (a.leaveT <= 0.f && !ambientMutated && !waitingEmpty) {
                bool busy = false;
                for (auto& o : ambient)
                    if (&o != &a && o.leaving) busy = true;
                if (busy) a.leaveT = 15.f;
                else { a.leaving = true; a.path = exitPath(); a.wp = 0; a.walking = true; }
            }
        }
        // stand-up glide: the sit lerp run in reverse, seat -> stand-spot.
        // the seat frees the moment they're upright even though they're still
        // walking out - fresh arrivals may claim it while they exit (turnover)
        if (a.standing) {
            a.sitT -= dt / 0.5f;
            a.t += dt * 3.f;                    // slow gait phase for the step back
            float k = std::clamp(a.sitT, 0.f, 1.f);
            k = k * k * (3.f - 2.f * k);        // smoothstep
            a.pos.x = a.standSpot.x + (a.seatPos.x - a.standSpot.x) * k;
            a.pos.z = a.standSpot.z + (a.seatPos.z - a.standSpot.z) * k;
            float dy = a.seatYaw - a.standYaw;   // ease the yaw back as well
            while (dy > 180.f) dy -= 360.f;
            while (dy < -180.f) dy += 360.f;
            a.yaw = a.standYaw + dy * k;
            if (a.sitT <= 0.f) {
                a.standing = false;
                a.sitT = -1.f;
                a.pos = a.standSpot;
                a.seatIdx = -1;
                a.path = exitPath();
                a.wp = 0;
                a.walking = true;
            }
        }
    }
    // leavers who reached the end of the exit route are gone
    std::erase_if(ambient, [](const Ambient& a) {
        return a.leaving && a.walking && a.wp >= (int)a.path.size();
    });
}

void VisitorSystem::Draw() const {
    // ambient waiting NPCs
    if (!waitingEmpty) {
        for (auto& a : ambient) {
            if (ambientMutated && !a.walking) {
                // stretched, pale, black-eyed - and every head tracks the player
                HumanoidApp m = a.app;
                m.height = a.app.height * 1.28f;
                m.eyes = "black";
                m.skin = Color{185, 180, 178, 255};
                m.shirt = Color{45, 42, 48, 255};
                float yaw = std::atan2(playerPos.x - a.pos.x, playerPos.z - a.pos.z) * RAD2DEG;
                DrawHumanoid(a.pos, yaw, m, true, a.t, 0.f, a.seatH);
            } else {
                // mid-sit: seated pose flips on past halfway and the hips
                // ride down from standing height onto the cushion; anyone
                // leaving draws standing even mid stand-up glide
                bool seated = (!a.walking || a.sitT > 0.5f) && !a.leaving;
                float sh = a.seatH;
                if (seated && a.sitT >= 0.f && a.sitT < 1.f) {
                    float k = std::clamp((a.sitT - 0.5f) * 2.f, 0.f, 1.f);
                    k = k * k * (3.f - 2.f * k);
                    sh = 0.91f + (a.seatH - 0.91f) * k;
                }
                float wAmt = a.walking
                    ? (a.sitT < 0.f ? 1.f : std::max(0.f, 1.f - a.sitT) * 0.7f)
                    : (a.standing ? std::max(0.f, 1.f - a.sitT) * 0.7f : 0.f);
                // people glance at you when you're close - quiet rooms watch
                float dx = playerPos.x - a.pos.x, dz = playerPos.z - a.pos.z;
                Vector3 look = (dx * dx + dz * dz < 7.f) ? playerPos : Vector3{};
                DrawHumanoid(a.pos, a.yaw, a.app, seated, a.t, 0.f, sh, wAmt, 0.f, look);
            }
        }
    }
    // exterior figure anomaly. The DOOR figure doubles as the door banger:
    // it stands close enough to reach the panel and hammers it on the same
    // beat BangerStrike() plays the Knock sfx (game.cpp UpdateCommon).
    if (figureActive) {
        bool doorBanger = exteriorFigure == "figure_at_door" && bangerActive && bangerAt == "door";
        float swing = doorBanger ? std::max(0.f, std::sin(bangT * 5.2f)) : 0.f;
        Office::DrawShadowFigure(doorBanger ? bangerPos : figurePos, 0.f, 1.15f, swing);
    }
    // the banger: a normal-looking person mid-swing, fist coming down on
    // beat. Skipped when the shadow figure already owns this spot (the door
    // figure IS the banging thing - no second body in the doorway)
    if (bangerActive && !(exteriorFigure == "figure_at_door" && bangerAt == "door")) {
        float swing = std::max(0.f, std::sin(bangT * 5.2f));   // sharp raise, hard drop
        HumanoidApp b;
        b.shirt = Color{55, 50, 58, 255};
        DrawHumanoid(bangerPos, 0.f, b, false, bangT * 3.f, swing);
    }
    // the current visitor
    if (cur.Active()) {
        if (cur.lunge) {
            // mid-vault over the desk: upright, arms thrown up, fast gait
            DrawHumanoid(cur.pos, cur.yaw, cur.app, false, cur.animT, 0.85f, cur.seatH, 1.f);
            return;
        }
        float wAmt = (cur.detained ? cur.detainWalk
                   : (cur.state == VState::ToDoor || cur.state == VState::ToChair ||
                      cur.state == VState::Leaving)) ? 1.f : 0.f;
        // the person across the desk watches you while you talk
        Vector3 look = (cur.seated || cur.state == VState::Seated) ? playerPos : Vector3{};
        DrawHumanoid(cur.pos, cur.yaw, cur.app, cur.seated, cur.animT, 0.f, cur.seatH, wAmt,
                     cur.detained ? 1.f : 0.f, look);
    }
}

json VisitorSystem::Serialize() const {
    json j;
    j["scheduleIdx"] = scheduleIdx;
    j["transcript"] = transcript;
    // the book itself: which slots are done, so a loaded save still knows
    // the night is finished when it is (early clock-out uses this)
    json vis = json::array();
    for (auto& e : schedule) vis.push_back(e.value("visited", false));
    j["visited"] = vis;
    if (cur.Active()) {
        j["visitor"] = JS(*cur.person, "id");
        j["anomaly"] = cur.anomaly;
        j["state"] = (int)cur.state;
        j["seatT"] = cur.seatT;
        j["escorted"] = cur.escorted;
        j["slotIdx"] = cur.slotIdx;
    }
    return j;
}

void VisitorSystem::Deserialize(const json& j, Office& office) {
    scheduleIdx = j.value("scheduleIdx", 0);
    transcript = j.value("transcript", std::vector<std::pair<std::string, std::string>>{});
    if (j.contains("visited") && j["visited"].is_array())
        for (int i = 0; i < (int)schedule.size() && i < (int)j["visited"].size(); i++)
            if (j["visited"][i].is_boolean()) schedule[i]["visited"] = j["visited"][i].get<bool>();
    if (j.contains("visitor")) {
        const json* person = RosterFind(j["visitor"].get<std::string>());
        if (person) {
            cur = Visitor{};
            cur.person = person;
            cur.anomaly = j.value("anomaly", std::string());
            cur.app = AppFromJson(*person);
            cur.escorted = j.value("escorted", false);
            cur.slotIdx = j.value("slotIdx", -1);
            int st = j.value("state", 4);
            cur.state = (VState)st;
            if (st == (int)VState::Seated) {
                cur.pos = office.VisitorSitPos();
                cur.seatH = 0.5f;
                cur.seated = true;
                cur.seatT = j.value("seatT", 0.f);
            } else {
                cur.pos = {0, 0, -5.75f}; // outside, at the door
                cur.yaw = 0;
                cur.state = VState::AtDoor;
            }
        }
    }
}
