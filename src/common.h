#pragma once
#include <raylib.h>
#include <raymath.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>

using json = nlohmann::json;

inline std::mt19937& RNG() {
    static std::mt19937 g{std::random_device{}()};
    return g;
}
inline float Rand01() { return std::uniform_real_distribution<float>(0.f, 1.f)(RNG()); }
inline float RandRange(float a, float b) { return a + Rand01() * (b - a); }
inline int RandInt(int a, int b) { return std::uniform_int_distribution<int>(a, b)(RNG()); }

inline json LoadJsonFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return json::object();
    std::stringstream ss; ss << f.rdbuf();
    try { return json::parse(ss.str()); }
    catch (...) { return json::object(); }
}
inline void SaveJsonFile(const std::string& path, const json& j) {
    std::ofstream f(path);
    if (f.good()) f << j.dump(2);
}

inline float JF(const json& j, const char* k, float d = 0.f) {
    return (j.contains(k) && j[k].is_number()) ? j[k].get<float>() : d;
}
inline int JI(const json& j, const char* k, int d = 0) {
    return (j.contains(k) && j[k].is_number()) ? j[k].get<int>() : d;
}
inline bool JB(const json& j, const char* k, bool d = false) {
    return (j.contains(k) && j[k].is_boolean()) ? j[k].get<bool>() : d;
}
inline std::string JS(const json& j, const char* k, const std::string& d = "") {
    return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : d;
}
inline Vector2 JV2(const json& j, Vector2 d = {0, 0}) {
    if (j.is_array() && j.size() >= 2) return {j[0].get<float>(), j[1].get<float>()};
    return d;
}
inline Vector3 JV3(const json& j, Vector3 d = {0, 0, 0}) {
    if (j.is_array() && j.size() >= 3)
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    if (j.is_array() && j.size() >= 2)
        return {j[0].get<float>(), 0.f, j[1].get<float>()};
    return d;
}
inline Color JC(const json& j, Color d = WHITE) {
    if (j.is_array() && j.size() >= 3)
        return {(unsigned char)j[0].get<int>(), (unsigned char)j[1].get<int>(),
                (unsigned char)j[2].get<int>(),
                j.size() >= 4 ? (unsigned char)j[3].get<int>() : (unsigned char)255};
    return d;
}

struct AABB {
    Vector3 mn{0, 0, 0}, mx{0, 0, 0};
    bool ContainsXZ(Vector2 p, float pad = 0.f) const {
        return p.x >= mn.x - pad && p.x <= mx.x + pad && p.y >= mn.z - pad && p.y <= mx.z + pad;
    }
};
inline AABB BottomBox(Vector3 pos, Vector3 size) {
    return {{pos.x - size.x / 2, pos.y, pos.z - size.z / 2},
            {pos.x + size.x / 2, pos.y + size.y, pos.z + size.z / 2}};
}
// Push a circle (xz) out of an AABB. Returns corrected position.
inline Vector2 CircleVsAABB(Vector2 p, float r, const AABB& b) {
    float cx = std::clamp(p.x, b.mn.x, b.mx.x);
    float cz = std::clamp(p.y, b.mn.z, b.mx.z);
    float dx = p.x - cx, dz = p.y - cz;
    float d2 = dx * dx + dz * dz;
    if (d2 >= r * r) return p;
    if (d2 < 1e-8f) {
        // center inside: push along smallest penetration axis
        float lx = std::min(p.x - b.mn.x, b.mx.x - p.x);
        float lz = std::min(p.y - b.mn.z, b.mx.z - p.y);
        if (lx < lz) p.x = (p.x - b.mn.x < b.mx.x - p.x) ? b.mn.x - r : b.mx.x + r;
        else p.y = (p.y - b.mn.z < b.mx.z - p.y) ? b.mn.z - r : b.mx.z + r;
        return p;
    }
    float d = std::sqrt(d2);
    return {cx + dx / d * r, cz + dz / d * r};
}
// Slab ray vs AABB, returns t or -1
inline float RayVsAABB(Vector3 ro, Vector3 rd, const AABB& b) {
    float tmin = 0.f, tmax = 1e9f;
    const float* o = &ro.x; const float* d = &rd.x;
    const float* mn = &b.mn.x; const float* mx = &b.mx.x;
    for (int i = 0; i < 3; i++) {
        if (std::fabs(d[i]) < 1e-8f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return -1;
        } else {
            float t1 = (mn[i] - o[i]) / d[i];
            float t2 = (mx[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1;
        }
    }
    return tmin;
}
// Slab ray vs a box rotated about Y. `yawRad` maps the box's local +x to
// world (cos yaw, -sin yaw) - the same convention rlRotatef(yawDeg,0,1,0)
// and the door-hinge math in game.cpp use. `half` = half extents.
inline float RayVsOBB_Y(Vector3 ro, Vector3 rd, Vector3 center, Vector3 half, float yawRad) {
    float c = std::cos(yawRad), s = std::sin(yawRad);
    Vector3 lro{(ro.x - center.x) * c - (ro.z - center.z) * s, ro.y - center.y,
                (ro.x - center.x) * s + (ro.z - center.z) * c};
    Vector3 lrd{rd.x * c - rd.z * s, rd.y, rd.x * s + rd.z * c};
    return RayVsAABB(lro, lrd, {{-half.x, -half.y, -half.z}, {half.x, half.y, half.z}});
}

inline std::string ReplaceAll(std::string s, const std::string& a, const std::string& b) {
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
    return s;
}

// verify keys the paperwork parser accepts - everything else becomes "none".
// Each maps to a real in-game deed; checking a box for something you never did
// is refused, so forms can't be faked for the instability freeze.
inline const std::map<std::string, std::string> VERIFY_REQ = {
    {"interview_done", "finish an interview"},
    {"report_filed", "file an accepted anomaly report"},
    {"call_answered", "answer the desk phone"},
    {"security_called", "dispatch security"},
    {"facilities_called", "dispatch facilities"},
    {"frontdesk_called", "contact the front desk"},
    {"blinds_used", "use the window blinds"},
    {"lights_used", "use the light switch"},
    {"visitor_early", "call a visitor in early"},
};

inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
inline bool TextHasAny(const std::string& text, const std::vector<std::string>& kws) {
    std::string t = ToLower(text);
    for (auto& k : kws) if (t.find(ToLower(k)) != std::string::npos) return true;
    return false;
}
// dev-hook env vars never change at runtime - read once, cache the pointer
// (call sites used to getenv() every frame, and some atoi() every frame)
inline const char* DevEnv(const char* name) {
    static std::map<std::string, const char*> cache;
    auto [it, ins] = cache.try_emplace(name, std::getenv(name));
    return it->second;
}

inline std::string FmtClock(float hour) {
    int h = ((int)hour) % 24;
    int m = (int)((hour - (int)hour) * 60.f);
    const char* ap = (h >= 12) ? "PM" : "AM";
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, m, ap);
    return buf;
}
// Ctrl+V clipboard paste into a text field
inline void TryPaste(std::string& s, int maxLen, bool stripWhitespace = false) {
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        const char* c = GetClipboardText();
        if (c) for (const char* p = c; *p && (int)s.size() < maxLen; p++) {
            if (*p >= 33 && *p < 127 && (!stripWhitespace || !std::isspace((unsigned char)*p)))
                s += *p;
            else if (!stripWhitespace && *p == ' ') s += ' ';
        }
    }
}
// Fold UTF-8 punctuation + Latin-1 accents down to the ASCII range our
// fonts actually carry. LLM replies love U+2019 apostrophes, curly quotes
// and em-dashes - raw, those render as holes. Applied to anything shown.
inline std::string SanitizeText(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            if (c == '\n') out += '\n';
            else if (c >= 32 && c < 127) out += (char)c;
            i++;
            continue;
        }
        uint32_t cp = 0; int len = 0;
        if ((c & 0xE0) == 0xC0 && i + 1 < in.size())      { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < in.size()){ cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < in.size()){ cp = c & 0x07; len = 4; }
        if (!len) { i++; continue; }
        bool ok = true;
        for (int k = 1; k < len; k++) {
            unsigned char cc = (unsigned char)in[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { i++; continue; }
        i += len;
        switch (cp) {
            case 0x2018: case 0x2019: case 0x02BC: case 0x2032: out += '\''; break;
            case 0x201C: case 0x201D: case 0x2033: case 0x201E: out += '"'; break;
            case 0x2013: case 0x2014: case 0x2212: case 0x2010: out += '-'; break;
            case 0x2026: out += "..."; break;
            case 0x00A0: case 0x2007: case 0x202F: out += ' '; break;
            case 0x2022: case 0x00B7: out += '-'; break;
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C4: case 0x00C3: case 0x00C5: out += 'A'; break;
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E4: case 0x00E3: case 0x00E5: out += 'a'; break;
            case 0x00C7: out += 'C'; break;
            case 0x00E7: out += 'c'; break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: out += 'E'; break;
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: out += 'e'; break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: out += 'I'; break;
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: out += 'i'; break;
            case 0x00D1: out += 'N'; break;
            case 0x00F1: out += 'n'; break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D6: case 0x00D5: case 0x00D8: out += 'O'; break;
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F6: case 0x00F5: case 0x00F8: out += 'o'; break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: out += 'U'; break;
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: out += 'u'; break;
            case 0x00DD: out += 'Y'; break;
            case 0x00FD: case 0x00FF: out += 'y'; break;
            case 0x00DF: out += "ss"; break;
            case 0x00C6: out += "AE"; break;
            case 0x00E6: out += "ae"; break;
            case 0x0152: out += "OE"; break;
            case 0x0153: out += "oe"; break;
            default: break;   // anything else just drops out
        }
    }
    return out;
}

// word-wrap helper
inline std::vector<std::string> WrapText(Font f, const std::string& text, float size, float spacing, float maxW) {
    std::vector<std::string> lines;
    std::string cur, word;
    std::istringstream ss(text);
    auto width = [&](const std::string& s) { return MeasureTextEx(f, s.c_str(), size, spacing).x; };
    while (ss >> word) {
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (width(trial) > maxW && !cur.empty()) { lines.push_back(cur); cur = word; }
        else cur = trial;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.push_back("");
    return lines;
}
