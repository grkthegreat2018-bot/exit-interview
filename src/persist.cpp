#include "game.h"
#include "hud.h"
#include <rlgl.h>
#include <cstdlib>
#include <cstring>


void Game::ApplyProvider() {
    auto& c = Cfg()["llm"];
    LLMClient::Provider p = providerIdx == 0 ? LLMClient::Provider::Groq
                          : providerIdx == 1 ? LLMClient::Provider::Local
                                             : LLMClient::Provider::Stub;
    json s = (p == LLMClient::Provider::Local) ? c["local"] : c["groq"];
    std::string ep = !llmEndpoint.empty() ? llmEndpoint : JS(s, "endpoint");
    std::string mdl = !llmModel.empty() ? llmModel : JS(s, "model");
    std::string key = (p == LLMClient::Provider::Groq) ? apiKey : "";
    llm.Configure(p, ep, key, mdl, JI(s, "max_tokens", 220), JF(s, "temperature", 0.7f),
                  JI(s, "timeout_sec", 25), JI(c, "rate_limit_pause_sec", 65));
    SaveSettings();
}


void Game::ApplyAudio() {
    SetMasterVolume(volMaster);
    audio.sfxMul = volSfx;
    audio.ambMul = volAmb;
    audio.droneMul = volDrone;
    audio.SetMenuMusicVol(volMusic);
    audio.SetHumLevel(1.f);     // hum loops - re-apply with the new amb scale
    audio.SetDroneLevel(0.15f + ad.Instability() / 100.f * 0.85f);   // same bed level as UpdateCommon
}


void Game::LoadSettings() {
    json j = LoadJsonFile("settings.json");
    if (j.empty()) {
        // fresh install: an exported profile dropped next to the exe is
        // auto-imported so ZIP builds pick up an old build's data wholesale
        json p = LoadJsonFile("ei_profile.json");
        if (p.is_object() && p.contains("settings") && p["settings"].is_object()) {
            SaveJsonFile("settings.json", p["settings"]);
            j = p["settings"];
            if (p.contains("save") && p["save"].is_object() && !p["save"].empty())
                SaveJsonFile(JS(Cfg(), "save_file", "save.json"), p["save"]);
            TraceLog(LOG_INFO, "[profile] auto-imported ei_profile.json");
        }
    }
    if (j.empty()) return;
    providerIdx = std::clamp(j.value("provider", 0), 0, 2);
    if (apiKey.empty()) apiKey = j.value("api_key", std::string());
    llmEndpoint = j.value("llm_endpoint", std::string());
    llmModel = j.value("llm_model", std::string());
    sens = j.value("sensitivity", sens);
    volMaster = std::clamp(j.value("vol_master", 1.f), 0.f, 2.f);
    volSfx = std::clamp(j.value("vol_sfx", 1.f), 0.f, 2.f);
    volAmb = std::clamp(j.value("vol_ambience", 1.f), 0.f, 2.f);
    volMusic = std::clamp(j.value("vol_music", 1.f), 0.f, 2.f);
    volDrone = std::clamp(j.value("vol_drone", 1.f), 0.f, 2.f);
    hardcodedOnly = j.value("hardcoded_anomalies", true);   // LLM anomalies opt-IN, not default
    seenNames = j.value("seen_names", std::vector<std::string>{});
    loreSeen = j.value("lore_seen", std::set<std::string>{});
    night = std::max(0, j.value("night", 0));   // 0 = orientation not yet run
    lastEval = j.value("last_eval", std::string());
    weekDone = j.value("week_done", false);
    if (j.contains("keys")) {
        auto& k = j["keys"];
        keys.fwd = k.value("fwd", KEY_W);
        keys.back = k.value("back", KEY_S);
        keys.left = k.value("left", KEY_A);
        keys.right = k.value("right", KEY_D);
        keys.sprint = k.value("sprint", KEY_LEFT_SHIFT);
        keys.crouch = k.value("crouch", KEY_Q);
        keys.interact = k.value("interact", KEY_E);
        keys.swap = k.value("swap", KEY_TAB);
    }
    if (const char* e = std::getenv("EI_NIGHT")) night = std::max(0, atoi(e));   // dev: force night
    ApplyAudio();
}


void Game::SaveSettings() {
    json j{{"provider", providerIdx}, {"api_key", apiKey}, {"sensitivity", sens},
           {"hardcoded_anomalies", hardcodedOnly}, {"seen_names", seenNames},
           {"lore_seen", loreSeen},
           {"night", night}, {"last_eval", lastEval}, {"week_done", weekDone},
           {"vol_master", volMaster}, {"vol_sfx", volSfx}, {"vol_ambience", volAmb},
           {"vol_music", volMusic}, {"vol_drone", volDrone},
           {"llm_endpoint", llmEndpoint}, {"llm_model", llmModel},
           {"keys", {{"fwd", keys.fwd}, {"back", keys.back}, {"left", keys.left},
                     {"right", keys.right}, {"sprint", keys.sprint}, {"crouch", keys.crouch},
                     {"interact", keys.interact}, {"swap", keys.swap}}}};
    SaveJsonFile("settings.json", j);
}


// ZIP-build migration: pack settings.json (provider, api key, endpoint,
// model, keybinds, volumes, night progress, seen names) AND save.json into
// one ei_profile.json. Drop that single file next to a new build's exe and
// it auto-imports on boot - or use IMPORT PROFILE below for same-folder.
void Game::ExportProfile() {
    SaveSettings();   // make sure the file on disk reflects current values
    json p;
    p["format"] = "exit_interview_profile";
    p["version"] = 1;
    p["settings"] = LoadJsonFile("settings.json");
    p["save"] = LoadJsonFile(JS(Cfg(), "save_file", "save.json"));
    SaveJsonFile("ei_profile.json", p);
    Notify("Profile exported to ei_profile.json - copy it into the new build folder.");
}


bool Game::ImportProfile() {
    json p = LoadJsonFile("ei_profile.json");
    if (!p.is_object() || !p.contains("settings") || !p["settings"].is_object())
        return false;
    SaveJsonFile("settings.json", p["settings"]);
    LoadSettings();
    ApplyProvider();
    if (p.contains("save") && p["save"].is_object() && !p["save"].empty()) {
        SaveJsonFile(JS(Cfg(), "save_file", "save.json"), p["save"]);
        saveExists = true;
    }
    return true;
}


void Game::Save() {
    json j;
    j["gameHour"] = gameHour;
    j["night"] = night;
    j["player"] = {{"x", player.pos.x}, {"z", player.pos.z}, {"yaw", player.yaw}};
    j["blinds"] = office.blindsOpen;
    j["director"] = ad.Serialize();
    j["visitors"] = vs.Serialize();
    j["tutStep"] = tutStep;   // night-0 lesson plan position survives a reload
    SaveJsonFile(JS(Cfg(), "save_file", "save.json"), j);
    saveExists = true;
}


bool Game::Load() {
    json j = LoadJsonFile(JS(Cfg(), "save_file", "save.json"));
    if (j.empty()) return false;
    gameHour = j.value("gameHour", 18.f);
    office.Load(A.office, Cfg());
    office.blindsOpen = j.value("blinds", false);
    player.pos = {j["player"].value("x", 0.f), 0.f, j["player"].value("z", 3.6f)};
    player.yaw = j["player"].value("yaw", 180.f);
    player.pitch = 0;
    doorOpen = false; doorAnim = 0; intruderSeen = false; shake = 0; blackoutT = 0; manualLightsOff = false;
    heartT = 0; evalScore = -1;
    deeds.clear(); paperIdx = 0;   // stats are per-shift; a loaded save starts a fresh record
    vs.Reset(false);               // keep the book as saved - no lore re-roll mid-shift
    ad.Reset();
    ad.Deserialize(j["director"], office, vs);
    ad.night = night;                    // night lives in settings, not the shift save
    vs.night = night;
    vs.Deserialize(j["visitors"], office);
    tutStep = j.value("tutStep", night == 0 ? 0 : -1);   // resume the lesson plan
    if (night == 0) workCallT = 1e9f;    // and keep the mundane phone quiet
    return true;
}
