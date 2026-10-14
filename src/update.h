#pragma once
#include "common.h"
#include <httplib.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <cstring>
#ifdef _WIN32
#include <process.h>            // _getpid - windows.h stays out (near/far macros)
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

// Github-release auto-updater. Checks api.github.com once at the title
// screen, shows a popup when a newer tag exists, downloads the platform
// package (manifest-driven - only files whose sha256 differs are applied,
// and files a previous release installed but this one dropped are removed;
// saves/settings live outside the manifest so they're never touched), then
// stages a detached apply script that waits for the game to exit, patches
// the folder, and relaunches.
// apply scripts live at file scope AND outside #ifdef entirely: MSVC does
// not recognize raw strings inside skipped preprocessor regions, so the
// `/*` globs in the shell script below would lex as a real unterminated
// block comment (C1071) when compiling the Windows build
static const char* kApplyScriptWin = R"PS(param([int]$GamePid)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root '_update\stage'
try {
    while (Get-Process -Id $GamePid -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 300 }
    Expand-Archive -Force (Join-Path $root '_update\pkg.bin') $stage
    $manifest = Get-Content (Join-Path $stage 'manifest.json') -Raw | ConvertFrom-Json
    $newPaths = @{}
    foreach ($f in $manifest.files) {
        $rel = [string]$f.path
        if ($rel -match '\.\.|^[A-Za-z]:' -or $rel.StartsWith('\') -or $rel.StartsWith('/')) { continue }
        $newPaths[$rel] = 1
        $dst = Join-Path $root $rel
        $src = Join-Path $stage $rel
        if (!(Test-Path $src)) { continue }
        $need = $true
        if (Test-Path $dst) {
            $need = ((Get-FileHash $dst -Algorithm SHA256).Hash.ToLower()) -ne ([string]$f.sha256).ToLower()
        }
        if ($need) {
            New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
            Copy-Item -Force $src $dst
        }
    }
    # remove files the previous manifest installed but this release dropped
    $oldMan = Join-Path $root 'manifest.json'
    if (Test-Path $oldMan) {
        foreach ($f in (Get-Content $oldMan -Raw | ConvertFrom-Json).files) {
            $rel = [string]$f.path
            if ($rel -match '\.\.|^[A-Za-z]:' -or $rel.StartsWith('\') -or $rel.StartsWith('/')) { continue }
            if (!$newPaths[$rel]) {
                $p = Join-Path $root $rel
                if (Test-Path $p) { Remove-Item -Force $p }
            }
        }
    }
    Copy-Item -Force (Join-Path $stage 'manifest.json') $oldMan
    if (Test-Path (Join-Path $root '_update\pending.txt')) {
        Copy-Item -Force (Join-Path $root '_update\pending.txt') (Join-Path $root '_upd_done.txt')
    }
    Remove-Item -Recurse -Force (Join-Path $root '_update')
    Start-Process (Join-Path $root 'exit_interview.exe')
} catch {
    Set-Content (Join-Path $root '_update_failed.txt') $_.Exception.Message
}
)PS";
static const char* kApplyScriptNix = R"SH(#!/bin/sh
# waits for the game pid to exit, unpacks, hash-diff applies, relaunches
PID=$1
while kill -0 "$PID" 2>/dev/null; do sleep 0.3; done
cd "$(dirname "$0")/.."
mkdir -p _update/stage
tar xzf _update/pkg.bin -C _update/stage || exit 1
cd _update/stage
if [ -f manifest.json ]; then
  if command -v python3 >/dev/null 2>&1; then
    # apply only files whose sha256 differs
    python3 - <<'PY'
import json, hashlib, os, shutil
root = os.path.abspath("../..")
man = json.load(open("manifest.json"))
new = set()
for f in man["files"]:
    rel = f["path"]
    if ".." in rel or rel.startswith("/"): continue
    new.add(rel)
    dst = os.path.join(root, rel)
    src = os.path.join(".", rel)
    need = True
    if os.path.exists(dst):
        need = hashlib.sha256(open(dst,'rb').read()).hexdigest() != f["sha256"]
    if need and os.path.exists(src):
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        shutil.copy2(src, dst)
# remove files the old manifest installed but this one dropped
oldman = os.path.join(root, "manifest.json")
if os.path.exists(oldman):
    try:
        for f in json.load(open(oldman))["files"]:
            rel = f["path"]
            if rel not in new and ".." not in rel and not rel.startswith("/"):
                p = os.path.join(root, rel)
                if os.path.isfile(p): os.remove(p)
    except Exception: pass
shutil.copy2("manifest.json", oldman)
PY
  elif command -v sha256sum >/dev/null 2>&1; then
    # no python3 - same diff via coreutils. manifest.json is machine-written
    # so "path"/"sha256" lines grep cleanly
    grep '"path"' manifest.json | sed -E 's/.*"path"[^"]*"([^"]+)".*/\1/' > _paths.txt
    grep '"sha256"' manifest.json | sed -E 's/.*"sha256"[^"]*"([^"]+)".*/\1/' > _hashes.txt
    cp _paths.txt _newpaths.txt
    TAB="$(printf '\t')"
    paste _paths.txt _hashes.txt | while IFS="$TAB" read -r rel sum; do
      case "$rel" in *..*|/*) continue;; esac
      [ -f "$rel" ] || continue
      dst="../../$rel"
      cur="$(sha256sum "$dst" 2>/dev/null | cut -d' ' -f1)"
      if [ "$cur" != "$sum" ]; then
        mkdir -p "$(dirname "$dst")"
        cp -f "$rel" "$dst"
      fi
    done
    if [ -f ../../manifest.json ]; then
      grep '"path"' ../../manifest.json | sed -E 's/.*"path"[^"]*"([^"]+)".*/\1/' | \
      while IFS= read -r rel; do
        case "$rel" in *..*|/*) continue;; esac
        grep -qxF "$rel" _newpaths.txt || { [ -f "../../$rel" ] && rm -f "../../$rel"; }
      done
    fi
    cp -f manifest.json ../../manifest.json
  else
    # last resort: overwrite everything except user data
    for f in $(find . -type f ! -name 'manifest.json'); do
      rel="${f#./}"
      case "$rel" in save.json|settings.json|profile*.json|*..*|/*) continue;; esac
      mkdir -p "../../$(dirname "$rel")"
      cp -f "$f" "../../$rel"
    done
    cp -f manifest.json ../../manifest.json
  fi
fi
cd ../..
[ -f _update/pending.txt ] && cp _update/pending.txt _upd_done.txt
rm -rf _update
./exit_interview &
)SH";

class GameUpdater {
public:
    enum class State { Idle, Checking, Available, Downloading, Ready,
                       Applying, Applied, Error };

    void Start(const std::string& repoUrl, const std::string& curVer) {
        if (started || repoUrl.empty()) return;
        started = true;
        cur = curVer;
        // repoUrl may be "github.com/owner/repo" or a full https URL
        std::string r = repoUrl;
        for (auto& ch : r) ch = (char)tolower((unsigned char)ch);
        auto gh = r.find("github.com/");
        if (gh != std::string::npos) r = r.substr(gh + 11);
        while (!r.empty() && (r.back() == '/' || r.back() == ' ')) r.pop_back();
        repo = r;   // owner/repo
        std::thread([this] { CheckThread(); }).detach();
    }

    void Stop() { stop = true; }

    State GetState() const { return st.load(); }
    float Progress() const { return progress.load(); }
    std::string NewVersion() const { std::lock_guard<std::mutex> l(m); return newVer; }
    std::string Changelog() const { std::lock_guard<std::mutex> l(m); return changelog; }
    std::string Error() const { std::lock_guard<std::mutex> l(m); return errMsg; }
    std::string CurrentVersion() const { return cur; }

    void BeginDownload() {
        if (GetState() != State::Available) return;
        st = State::Downloading;
        progress = 0.f;
        std::thread([this] { DownloadThread(); }).detach();
    }

    // writes the apply script + hands off - caller should quit immediately
    bool ApplyAndRelaunch() {
        if (GetState() != State::Ready) return false;
        std::error_code ec;
        std::filesystem::create_directories("_update", ec);
#ifdef _WIN32
        const char* script = "_update/apply_update.ps1";
        {
            std::ofstream f(script, std::ios::binary);
            f << kApplyScriptWin;
        }
        // detached powershell: waits for this PID to exit, patches, relaunches.
        // `start /b` detaches via cmd so no windows.h types are needed here
        std::string cmd = "start \"\" /b powershell -NoProfile -ExecutionPolicy Bypass"
                          " -WindowStyle Hidden -File \"_update\\apply_update.ps1\" -GamePid " +
                          std::to_string(_getpid());
        if (std::system(cmd.c_str()) != 0) {
            st = State::Error;
            std::lock_guard<std::mutex> l(m);
            errMsg = "could not spawn updater";
            return false;
        }
#else
        const char* script = "_update/apply_update.sh";
        {
            std::ofstream f(script, std::ios::binary);
            f << kApplyScriptNix;
        }
        chmod(script, 0755);
        std::string cmd = "setsid sh \"_update/apply_update.sh\" " +
                          std::to_string((int)getpid()) + " >/dev/null 2>&1 &";
        if (std::system(cmd.c_str()) != 0) { st = State::Error; return false; }
#endif
        st = State::Applying;
        return true;
    }

    // used when Start() never ran (fake/applied-marker paths) so the popup
    // still shows the running version in the delta line
    void SetCurrent(const std::string& v) { cur = v; }

    // dev/testing: force a state without a network round-trip
    void ForceState(State s, const char* ver, const char* notes) {
        std::lock_guard<std::mutex> l(m);
        newVer = ver; changelog = notes;
        st = s;
    }

    // one-shot "update applied" report for the boot after an update
    static bool ConsumeAppliedMarker(std::string& ver, std::string& notes) {
        std::ifstream f("_upd_done.txt", std::ios::binary);
        if (!f) return false;
        std::getline(f, ver);
        notes.assign(std::istreambuf_iterator<char>(f), {});
        std::error_code ec;
        f.close();
        std::filesystem::remove("_upd_done.txt", ec);
        return !ver.empty();
    }

private:
    static std::vector<int> ParseVer(const std::string& v) {
        std::vector<int> out;
        std::string s = v;
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
        for (size_t p = 0, q; p <= s.size(); p = q + 1) {
            q = s.find('.', p); if (q == std::string::npos) q = s.size();
            out.push_back(atoi(s.substr(p, q - p).c_str()));
        }
        return out;
    }
    static bool Newer(const std::string& a, const std::string& b) {
        auto x = ParseVer(a), y = ParseVer(b);
        for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
            int xi = i < x.size() ? x[i] : 0, yi = i < y.size() ? y[i] : 0;
            if (xi != yi) return xi > yi;
        }
        return false;
    }

    // https GET with manual redirect hops (release assets bounce off
    // github.com onto the CDN hosts) - hosts restricted to the github set
    bool HttpGet(const std::string& host, const std::string& path,
                 std::string* body, const std::string& saveTo,
                 std::atomic<float>* prog, std::atomic<bool>* cancel) {
        static const char* okHosts[] = {"api.github.com", "github.com",
            "objects.githubusercontent.com", "release-assets.githubusercontent.com",
            "github-releases.githubusercontent.com", "api.github.com"};
        std::string h = host, p = path;
        for (int hop = 0; hop < 5; hop++) {
            bool allowed = false;
            for (auto* o : okHosts) if (h == o) allowed = true;
            if (!allowed) return false;
            httplib::SSLClient cli(h);
            cli.set_connection_timeout(15, 0);
            cli.set_read_timeout(30, 0);
            cli.enable_server_certificate_verification(false);
            cli.set_follow_location(false);
            httplib::Headers hd{{"User-Agent", "exit-interview-updater"},
                                {"Accept", "application/vnd.github+json"}};
            httplib::Result r;
            if (!saveTo.empty()) {
                std::ofstream out(saveTo, std::ios::binary | std::ios::trunc);
                if (!out) return false;
                r = cli.Get(p.c_str(), hd,
                    [&](const httplib::Response& resp) {
                        auto it = resp.headers.find("Content-Length");
                        if (prog && it != resp.headers.end())
                            totalBytes = (double)std::atoll(it->second.c_str());
                        return true;
                    },
                    [&](const char* d, size_t n) {
                        out.write(d, (std::streamsize)n);
                        if (prog) {
                            gotBytes += (double)n;
                            if (totalBytes > 0) *prog = (float)(gotBytes / totalBytes);
                        }
                        return !(cancel && *cancel);
                    });
            } else {
                r = cli.Get(p.c_str(), hd);
            }
            if (!r) return false;
            if (r->status >= 300 && r->status < 400) {
                auto loc = r->headers.find("Location");
                if (loc == r->headers.end()) return false;
                std::string u = loc->second;
                auto hs = u.find("https://");
                if (hs != 0) return false;                    // https only
                auto sl = u.find('/', 8);
                h = u.substr(8, sl == std::string::npos ? std::string::npos : sl - 8);
                p = sl == std::string::npos ? "/" : u.substr(sl);
                continue;
            }
            if (r->status < 200 || r->status >= 300) return false;
            if (body) *body = r->body;
            return true;
        }
        return false;
    }

    void CheckThread() {
        st = State::Checking;
        std::string body;
        if (!HttpGet("api.github.com", "/repos/" + repo + "/releases/latest",
                     &body, "", nullptr, &stop) || stop) {
            st = State::Idle;    // silent: no release yet / offline / not a repo
            return;
        }
        json j;
        try { j = json::parse(body); } catch (...) { st = State::Idle; return; }
        std::string tag = j.value("tag_name", "");
        if (tag.empty() || !Newer(tag, cur)) { st = State::Idle; return; }
        std::string asset;
#ifdef _WIN32
        const char* want = "win64.zip";
#else
        const char* want = "linux64.tar.gz";
#endif
        if (j.contains("assets"))
            for (auto& a : j["assets"]) {
                std::string nm = a.value("name", "");
                if (nm.size() >= strlen(want) &&
                    nm.compare(nm.size() - strlen(want), strlen(want), want) == 0)
                    asset = a.value("browser_download_url", "");
            }
        {
            std::lock_guard<std::mutex> l(m);
            newVer = tag;
            changelog = j.value("body", "");
            assetUrl = asset;
        }
        st = State::Available;
        TraceLog(LOG_INFO, "[update] new release %s available (current %s)",
                 tag.c_str(), cur.c_str());
    }

    void DownloadThread() {
        std::string url;
        { std::lock_guard<std::mutex> l(m); url = assetUrl; }
        if (url.empty()) { st = State::Error; { std::lock_guard<std::mutex> l(m); errMsg = "release has no package asset"; } return; }
        auto hs = url.find("https://");
        if (hs != 0) { st = State::Error; { std::lock_guard<std::mutex> l(m); errMsg = "bad package url"; } return; }
        auto sl = url.find('/', 8);
        std::string host = url.substr(8, sl - 8), path = url.substr(sl);
        gotBytes = 0; totalBytes = 0;
        std::error_code ec;
        std::filesystem::create_directories("_update", ec);
        bool okd = HttpGet(host, path, nullptr, "_update/pkg.bin", &progress, &stop);
        if (!okd || stop) {
            st = State::Error;
            std::lock_guard<std::mutex> l(m);
            errMsg = stop ? "cancelled" : "download failed";
            return;
        }
        progress = 1.f;
        // leave a marker for the apply script + the post-update popup
        {
            std::ofstream f("_update/pending.txt", std::ios::binary);
            std::lock_guard<std::mutex> l(m);
            f << newVer << "\n" << changelog;
        }
        st = State::Ready;
    }


    std::string repo, cur;
    std::atomic<State> st{State::Idle};
    std::atomic<float> progress{0.f};
    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    std::string assetUrl;
    double gotBytes = 0, totalBytes = 0;
    std::string newVer, changelog, errMsg;
    mutable std::mutex m;
};
