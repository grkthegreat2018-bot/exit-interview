#include "assets.h"
#include <filesystem>

namespace fs = std::filesystem;

static bool LoadJsonInto(const std::string& path, json& out, std::string& err) {
    std::ifstream f(path);
    if (!f.good()) { err = path + ": file not found"; return false; }
    std::stringstream ss; ss << f.rdbuf();
    try { out = json::parse(ss.str()); }
    catch (const std::exception& e) { err = path + ": " + e.what(); return false; }
    return true;
}

bool Assets::Load(const std::string& preferred) {
    // resolve the assets dir: cwd-relative first, then next to the exe,
    // then a few parent levels (dev: build/Release -> repo root)
    std::vector<std::string> cands = {preferred};
    const char* appDir = GetApplicationDirectory();
    if (appDir && *appDir) {
        std::string base = std::string(appDir);
        for (int up = 0; up <= 3; up++) {
            std::string p = base;
            for (int i = 0; i < up; i++) p += "../";
            cands.push_back(p + preferred);
        }
    }
    for (auto& c : cands) {
        std::error_code ec;
        if (fs::is_directory(fs::path(c), ec)) { dir = c; break; }
    }
    if (dir.empty()) {
        lastError = "assets/ directory not found (looked near cwd and the exe)";
        return false;
    }
    if (!LoadAll()) return false;
    LoadFonts();
    Validate();
    return true;
}

bool Assets::LoadAll() {
    lastError.clear();
    bool ok = true;
    auto need = [&](const char* name, json& j, bool required) {
        std::string err;
        if (!LoadJsonInto(dir + "/" + name, j, err)) {
            if (required) { lastError = err; ok = false; }
            else warnings.push_back("skipped " + err);
        }
    };
    need("config.json", config, true);
    need("office.json", office, true);
    need("anomalies.json", anomalies, true);
    need("visitors.json", visitors, true);
    need("dialogs.json", dialogs, false);
    need("anomaly_parts.json", anomalyParts, false);
    if (config.empty() || office.empty()) {
        if (lastError.empty()) lastError = "config.json/office.json parsed empty";
        ok = false;
    }
    return ok;
}

bool Assets::ReloadData() {
    if (dir.empty()) return false;
    json keepOffice = office;               // live layout stays put
    std::vector<std::string> w0;
    warnings.swap(w0);                      // collect fresh warnings
    bool ok = LoadAll();
    warnings.insert(warnings.end(), w0.begin(), w0.end());
    office = std::move(keepOffice);
    Validate();
    return ok;
}

// Latin-1 + common typographic glyphs: curly quotes, dashes, ellipsis, bullet,
// arrows, star, check/cross. LoadFontEx's default range bakes ASCII only -
// anything wider renders as holes, so we hand it an explicit codepoint table.
static const std::vector<int>& UIGlyphs() {
    static std::vector<int> cps = [] {
        std::vector<int> v;
        for (int c = 32; c <= 255; c++) v.push_back(c);   // ASCII + Latin-1
        for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D,
                      0x2026, 0x2022, 0x00B7, 0x2190, 0x2191, 0x2192, 0x2193,
                      0x2605, 0x2713, 0x2717})
            v.push_back(c);
        return v;
    }();
    return cps;
}

static Font LoadUIFont(const std::string& path, int size) {
    auto& g = UIGlyphs();
    return LoadFontEx(path.c_str(), size, (int*)g.data(), (int)g.size());
}

static Font TryFont(const std::string& path, int size) {
    if (FileExists(path.c_str())) return LoadUIFont(path, size);
    return Font{0};
}

void Assets::LoadFonts() {
    // bundled fonts win (ZIP build can ship its own); then Windows fallbacks
    std::vector<std::string> ui;
    if (!dir.empty()) {
        ui.push_back(dir + "/fonts/regular.ttf");
        ui.push_back(dir + "/fonts/ui.ttf");
    }
    ui.insert(ui.end(), {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
    });
    for (auto& c : ui) {
        if (!FileExists(c.c_str())) continue;
        font      = LoadUIFont(c.c_str(), 32);
        fontSmall = LoadUIFont(c.c_str(), 20);
        fontBig   = LoadUIFont(c.c_str(), 72);
        fontOk = true;
        break;
    }
    if (!fontOk) {
        font = GetFontDefault();
        fontSmall = GetFontDefault();
        fontBig = GetFontDefault();
        warnings.push_back("no TTF found - using raylib default font");
    }

    std::vector<std::string> mono;
    if (!dir.empty()) mono.push_back(dir + "/fonts/mono.ttf");
    mono.insert(mono.end(), {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "C:/Windows/Fonts/lucon.ttf",
    });
    for (auto& c : mono) {
        if (!FileExists(c.c_str())) continue;
        fontMono = LoadUIFont(c.c_str(), 24);
        monoOk = true;
        break;
    }
    if (!monoOk) fontMono = fontSmall;
}

void Assets::Validate() {
    auto needKey = [&](const json& j, const char* k, const char* file) {
        if (!j.contains(k)) warnings.push_back(std::string(file) + ": missing \"" + k + "\"");
    };
    needKey(config, "game", "config.json");
    needKey(config, "instability", "config.json");
    needKey(office, "furniture", "office.json");
    needKey(office, "player_spawn", "office.json");
    needKey(office, "office_room", "office.json");
    needKey(office, "waiting_room", "office.json");
    needKey(office, "paths", "office.json");
    if (anomalies.contains("office") && !anomalies["office"].is_array())
        warnings.push_back("anomalies.json: \"office\" is not an array");
    if (visitors.contains("roster") && !visitors["roster"].is_array())
        warnings.push_back("visitors.json: \"roster\" is not an array");

    // anomaly defs must reference furniture that actually exists
    if (office.contains("furniture") && anomalies.contains("office")) {
        std::set<std::string> ids;
        for (auto& f : office["furniture"]) ids.insert(JS(f, "id"));
        auto checkList = [&](const json& list, const char* where) {
            if (!list.is_array()) return;
            for (auto& d : list) {
                std::string t = JS(d, "target");
                if (!t.empty() && !ids.count(t))
                    warnings.push_back(std::string(where) + " def \"" + JS(d, "id") +
                                     "\" targets missing furniture \"" + t + "\"");
            }
        };
        checkList(anomalies["office"], "office");
        checkList(anomalies["exterior"], "exterior");
    }
}

void Assets::Unload() {
    if (fontOk) { UnloadFont(font); UnloadFont(fontSmall); UnloadFont(fontBig); }
    if (monoOk) UnloadFont(fontMono);
}
