#pragma once
#include "common.h"

// Procedurally synthesized sounds - no binary assets needed.
class AudioBank {
public:
    enum class Sfx { Knock, Ring, Stinger, Thump, Click, TypeTick, Whoosh, Buzz, Heart, Creak, Step, Screech };

    float sfxMul = 1.f;     // settings: one-shots scale (knocks, phone, UI ticks)
    float ambMul = 1.f;     // settings: room-tone loop scale (hum)
    float droneMul = 1.f;   // settings: instability drone bed scale

    void Init() {
        InitAudioDevice();
        if (!IsAudioDeviceReady()) return;
        knock   = MakeSound(GenKnock());
        ring    = MakeSound(GenRing());
        stinger = MakeSound(GenStinger());
        thump   = MakeSound(GenThump());
        click   = MakeSound(GenClick());
        typeT   = MakeSound(GenClick(0.03f, 3000));
        whoosh  = MakeSound(GenWhoosh());
        buzz    = MakeSound(GenBuzz());
        heart   = MakeSound(GenHeart());
        creak   = MakeSound(GenCreak());
        screech = MakeSound(GenScreech());
        step    = MakeSound(GenStep());
        hum     = MakeSound(GenHum());
        SetSoundVolume(hum, 0.10f);
        PlaySound(hum); // loops
        drone   = MakeSound(GenDrone());
        SetSoundVolume(drone, 0.22f);
        PlaySound(drone); // loops
        ok = true;
    }
    void Play(Sfx s, float vol = 1.f) {
        if (!ok) return;
        Sound snd{};
        switch (s) {
            case Sfx::Knock: snd = knock; break;
            case Sfx::Ring: snd = ring; break;
            case Sfx::Stinger: snd = stinger; break;
            case Sfx::Thump: snd = thump; break;
            case Sfx::Click: snd = click; break;
            case Sfx::TypeTick: snd = typeT; break;
            case Sfx::Whoosh: snd = whoosh; break;
            case Sfx::Buzz: snd = buzz; break;
            case Sfx::Heart: snd = heart; break;
            case Sfx::Creak: snd = creak; break;
            case Sfx::Step: snd = step; break;
            case Sfx::Screech: snd = screech; break;
        }
        SetSoundVolume(snd, vol * sfxMul);
        PlaySound(snd);
    }
    void PlayPanned(Sfx s, float vol, float pan) {
        if (!ok) return;
        Sound snd{};
        switch (s) {
            case Sfx::Step: snd = step; break;
            default: Play(s, vol); return;
        }
        SetSoundVolume(snd, vol * sfxMul);
        SetSoundPan(snd, pan);
        PlaySound(snd);
        SetSoundPan(snd, 0.5f);
    }
    void SetDroneLevel(float v) {
        if (!ok) return;
        v = std::clamp(v, 0.f, 1.f);
        SetSoundVolume(drone, v * 0.22f * droneMul);
    }
    void SetHumLevel(float v) {
        if (!ok) return;
        v = std::clamp(v, 0.f, 1.f);
        SetSoundVolume(hum, v * 0.10f * ambMul);
    }

    // ---- menu music: the only binary assets - streamed mp3 loops.
    // Bad/missing files are logged once and skipped silently so a broken
    // track never takes the rest of the audio down with it.
    float musicMul = 1.f;     // settings: menu music channel
    bool MenuPlaying() const { return musicOn; }
    // display name for the menu now-playing chip: filename, extension
    // stripped, separators turned to spaces, "Menu_" prefix dropped
    std::string MenuTrackName() const {
        if (!musicOn) return {};
        std::string fn = musicPath;
        auto sl = fn.find_last_of("/\\");
        if (sl != std::string::npos) fn = fn.substr(sl + 1);
        auto dot = fn.find_last_of('.');
        if (dot != std::string::npos) fn = fn.substr(0, dot);
        for (auto& ch : fn) if (ch == '_' || ch == '-') ch = ' ';
        if (fn.rfind("menu ", 0) == 0) fn = fn.substr(5);
        while (!fn.empty() && fn.front() == ' ') fn.erase(fn.begin());
        return fn;
    }
    float MenuTimePlayed() const { return musicOn ? GetMusicTimePlayed(music) : 0.f; }
    float MenuTimeLength() const { return musicOn ? GetMusicTimeLength(music) : 0.f; }

    void StartMenuMusic(const char* path) {
        if (!ok || musicBad.count(path)) return;
        if (musicOn && musicPath == path) return;
        StopMenuMusic();
        music = LoadMusicStream(path);
        if (!IsMusicValid(music)) { musicBad.insert(path); musicOn = false; return; }
        music.looping = true;           // tracks are authored to loop cleanly
        musicPath = path;
        musicFade = 0.f;
        PlayMusicStream(music);
        SetMusicVolume(music, 0.f);
        SetMusicPitch(music, musicPitch);   // a death may leave it slowed - restore
        musicOn = true;
    }
    // tape-style pitch+speed shift (raylib scales both together). Used to
    // drag the theme down on the game-over screen without restarting it.
    void SetMenuMusicPitch(float p) {
        musicPitch = p;
        if (ok && musicOn) SetMusicPitch(music, p);
    }
    void UpdateMenuMusic() {
        if (!ok || !musicOn) return;
        UpdateMusicStream(music);
        if (musicFade < 1.f) {
            musicFade = std::min(1.f, musicFade + GetFrameTime() * 0.45f);   // ~2.2s in
            SetMusicVolume(music, musicFade * musicMul * 0.85f);
        }
    }
    void SetMenuMusicVol(float v) {
        musicMul = v;
        if (ok && musicOn) SetMusicVolume(music, musicFade * musicMul * 0.85f);
    }
    void StopMenuMusic() {
        if (!ok || !musicOn) { musicOn = false; return; }
        StopMusicStream(music);
        UnloadMusicStream(music);
        musicOn = false;
        musicPath.clear();
    }

    void Shutdown() {
        if (!ok) return;
        StopMenuMusic();
        StopSound(hum);
        StopSound(drone);
        CloseAudioDevice();
    }

private:
    static constexpr int SR = 22050;
    Sound knock{}, ring{}, stinger{}, thump{}, click{}, typeT{}, whoosh{},
          buzz{}, heart{}, creak{}, step{}, screech{}, hum{}, drone{};
    Music music{};
    bool musicOn = false;
    float musicFade = 0.f;
    float musicPitch = 1.f;
    std::string musicPath;
    std::set<std::string> musicBad;
    bool ok = false;

    static Sound MakeSound(Wave w) {
        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        return s;
    }
    static Wave NewWave(int frames) {
        Wave w{};
        w.frameCount = frames;
        w.sampleRate = SR;
        w.sampleSize = 16;
        w.channels = 1;
        w.data = calloc(frames, sizeof(short));
        return w;
    }
    static Wave GenKnock() {
        int n = SR / 3;
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float env = std::exp(-t * 22.f);
            // two taps
            float env2 = (t > 0.12f) ? std::exp(-(t - 0.12f) * 22.f) : 0.f;
            float v = (std::sin(t * 140.f * 6.28f) * 0.7f + (Rand01() - 0.5f) * 0.6f) * (env + env2 * 0.8f);
            d[i] = (short)(v * 30000);
        }
        return w;
    }
    static Wave GenRing() {
        int n = SR;
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float gate = (std::fmod(t, 0.5f) < 0.06f || (std::fmod(t, 0.5f) > 0.1f && std::fmod(t, 0.5f) < 0.14f)) ? 1.f : 0.f;
            float v = (std::sin(t * 440.f * 6.28f) + std::sin(t * 480.f * 6.28f)) * 0.5f * gate;
            d[i] = (short)(v * 16000);
        }
        return w;
    }
    static Wave GenStinger() {
        int n = SR * 2;
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float env = std::exp(-t * 2.2f);
            float v = std::sin(t * 110.f * 6.28f) * 0.5f
                    + std::sin(t * 117.f * 6.28f) * 0.4f
                    + std::sin(t * 220.f * 6.28f) * 0.25f * std::exp(-t * 6.f)
                    + (Rand01() - 0.5f) * 0.9f * std::exp(-t * 8.f);
            d[i] = (short)(v * env * 30000);
        }
        return w;
    }
    static Wave GenThump() {
        int n = SR / 2;
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float v = std::sin(t * 55.f * 6.28f) * std::exp(-t * 9.f);
            d[i] = (short)(v * 28000);
        }
        return w;
    }
    static Wave GenClick(float dur = 0.04f, float freq = 2200) {
        int n = (int)(SR * dur);
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float v = std::sin(t * freq * 6.28f) * std::exp(-t * 160.f);
            d[i] = (short)(v * 14000);
        }
        return w;
    }
    static Wave GenWhoosh() {
        int n = SR / 2;
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float env = std::sin(t / (SR / 2.f) * 3.14159f);
            d[i] = (short)((Rand01() - 0.5f) * env * 12000);
        }
        return w;
    }
    static Wave GenBuzz() {
        int n = (int)(SR * 0.35f);
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        float lp = 0.f, bp = 0.f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float env = std::exp(-t * 7.f);                                   // overall decay
            float fl = (std::sin(t * 37.f * 6.28f) > -0.3f) ? 1.f : 0.25f;    // sputter gate
            float nz = Rand01() * 2.f - 1.f;
            lp += 0.30f * (nz - lp);                                          // lowpass
            bp += 0.55f * ((nz - lp) - bp);                                   // band-limited = bandpass-y
            float sq = (std::sin(t * 120.f * 6.28f) > 0.f) ? 1.f : -1.f;      // 120Hz rattle
            float v = (bp * 1.1f + sq * 0.22f) * env * fl;
            d[i] = (short)(v * 22000);
        }
        return w;
    }
    static Wave GenHeart() {
        int n = (int)(SR * 0.7f);
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            // lub-dub: two low sine thumps
            float lub = std::sin(t * 55.f * 6.28f) * std::exp(-t * 16.f);
            float dub = (t > 0.28f) ? std::sin((t - 0.28f) * 52.f * 6.28f) * std::exp(-(t - 0.28f) * 15.f) * 0.8f : 0.f;
            d[i] = (short)((lub + dub) * 26000);
        }
        return w;
    }
    static Wave GenCreak() {
        int n = (int)(SR * 0.9f);
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        float ph = 0.f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float u = t / 0.9f;                                               // 0..1 progress
            ph += (180.f - 90.f * u) / SR;                                    // 180 -> 90 Hz glide
            float env = std::sin(u * 3.14159f);                               // fade in/out
            float wob = 0.65f + 0.35f * std::sin(t * 7.f * 6.28f);            // amplitude wobble
            float v = (std::sin(ph * 6.28f) + std::sin(ph * 2.f * 6.28f) * 0.3f) * wob
                    + (Rand01() - 0.5f) * 0.18f * wob;
            d[i] = (short)(v * env * 16000);
        }
        return w;
    }
    static Wave GenScreech() {
        // furniture dragged hard across flooring: banded noise scrape that
        // drops pitch as it slides, then a soft thunk where it lands
        int n = (int)(SR * 0.65f);
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        float lp = 0.f, bp = 0.f, ph = 0.f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float u = t / 0.65f;
            float env = (u < 0.12f) ? u / 0.12f : std::exp(-(u - 0.12f) * 3.4f);   // fast attack, tail off
            ph += (900.f - 530.f * u) / SR;                                        // scrape wail ~900 -> 370 Hz
            float nz = Rand01() * 2.f - 1.f;
            lp += 0.18f * (nz - lp);
            bp += 0.5f * ((nz - lp) - bp);                                         // band-limited noise = scrape
            float v = (bp * 1.6f + std::sin(ph * 6.28f) * 0.32f) * env;
            if (u > 0.80f) {                                                       // landing thunk
                float lt = t - 0.80f * 0.65f;
                v += std::sin(lt * 72.f * 6.28f) * std::exp(-lt * 16.f) * 0.7f;
            }
            d[i] = (short)(std::clamp(v, -1.f, 1.f) * 24000);
        }
        return w;
    }
    static Wave GenStep() {
        // soft shoe on office carpet: low thump + muffled noise scuff
        int n = (int)(SR * 0.13f);
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        float lp = 0.f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float nz = Rand01() * 2.f - 1.f;
            lp += 0.22f * (nz - lp);
            float v = std::sin(t * 95.f * 6.28f) * std::exp(-t * 34.f) * 0.8f
                    + lp * std::exp(-t * 26.f) * 0.55f;
            d[i] = (short)(v * 22000);
        }
        return w;
    }
    static Wave GenHum() {
        int n = SR * 2; // loop
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            float v = std::sin(t * 50.f * 6.28f) * 0.5f + (Rand01() - 0.5f) * 0.15f;
            d[i] = (short)(v * 8000);
        }
        return w;
    }
    static Wave GenDrone() {
        int n = SR * 2; // loop
        Wave w = NewWave(n);
        short* d = (short*)w.data;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            // detuned low oscillators + slow LFO wobble (all complete integer
            // cycles over 2s so the loop seam stays quiet)
            float lfo = 0.7f + 0.3f * std::sin(t * 0.5f * 6.28f);
            float v = (std::sin(t * 48.f * 6.28f) * 0.5f
                     + std::sin(t * 50.5f * 6.28f) * 0.4f
                     + std::sin(t * 96.f * 6.28f) * 0.2f) * lfo
                    + (Rand01() - 0.5f) * 0.05f;
            d[i] = (short)(v * 9000);
        }
        return w;
    }
};
