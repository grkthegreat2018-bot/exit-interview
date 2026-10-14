#pragma once
#include "common.h"

// Central asset store: all content JSON + UI fonts. Load() resolves the
// assets/ dir relative to cwd first, then the exe's own directory and a few
// parents, so the game can be launched from anywhere.
class Assets {
public:
    json config, office, anomalies, visitors, dialogs, anomalyParts;
    Font font{0}, fontBig{0}, fontSmall{0}, fontMono{0};
    bool fontOk = false;
    bool monoOk = false;

    std::string dir;                    // resolved assets dir ("" until Load)
    std::string lastError;              // human-readable reason Load failed
    std::vector<std::string> warnings;  // validation notes (non-fatal)

    bool Load(const std::string& preferred = "assets");
    // Re-read the data JSONs from disk (dev hot reload - F9). Fonts and the
    // office layout are untouched: live furniture positions keep working.
    bool ReloadData();
    void Unload();

private:
    bool LoadAll();
    void LoadFonts();
    void Validate();
};
