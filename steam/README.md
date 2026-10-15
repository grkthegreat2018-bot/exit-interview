# EXIT INTERVIEW — Steam release checklist

## 1. Steamworks App Admin (web, one-time)

- **App ID / Depot IDs** — Steam auto-creates one depot (AppID+1). Add a
  second depot so Windows and Linux are separate: name them `Windows` /
  `Linux`, set OS filter to Windows-only / Linux-only on each.
- **Launch options** (App Admin → Installation → General):
  - Windows: `exit_interview.exe` — no args, working dir blank.
  - Linux: `exit_interview` — no args. (Both live at depot root; the game
    finds `assets/` relative to cwd, which Steam sets to the install dir.)
- **Packages** — add both depots to your Developer Comp package AND the
  store package, then **Publish** the config. Unpublished config is the #1
  cause of "depot not included" upload errors.
- **Fill the .vdf IDs** — replace `APPID`, `DEPOT_WIN`, `DEPOT_LINUX` in the
  three scripts here.

## 2. Files Steam needs in the depot

Per-OS depot contents (already what CI produces):

```
exit_interview(.exe)   launch binary
*.dll                  win64 only (raylib is linked dynamically)
assets/                everything: config.json, anomalies.json,
                       dialogs.json, Music/, etc.
manifest.json          self-update manifest (harmless under Steam;
                       the updater just no-ops when versions match)
README.txt / icon      optional
```

Not needed / not wanted: `steam_api.dll` (no Steamworks integration —
game is DRM-free, fine to ship without it), `steam_appid.txt` (dev only,
keep OUT of the depot), `*.pdb`, `settings.json`, `save.json` (player data
is written at runtime — the FileProperties "userconfig" flags in the depot
scripts protect them anyway if a seed copy ever ships).

## 3. Stage + upload

```powershell
# on Windows, repo root:
cmake --preset default && cmake --build build --config Release
.\launch.py --dist            # or your existing dist refresh steps
steam\stage_content.ps1 -LinuxTar .\exit-interview-linux64.tar.gz
```

Then upload. **Windows depot** can go from Windows steamcmd:
```
steamcmd +login <user> +run_app_build steam\depot_build_win64.vdf +quit
```
(single-depot run: point ContentRoot at `steam\content\win64\` — or just
run `app_build.vdf` from a Linux box which handles both depots at once.)

**Linux depot MUST be uploaded from Linux** — Windows steamcmd drops the
executable bit and players get "permission denied". Easiest path: run
steamcmd in WSL or any Linux CI runner:
```bash
sudo apt install steamcmd   # or download steamcmd_linux.tar.gz
chmod +x steam/content/linux64/exit_interview
steamcmd +login <user> +run_app_build $(pwd)/steam/depot_build_linux64.vdf +quit
```

Verify the uploaded depot file list in the build log — `exit_interview`
should appear with executable permissions.

## 4. Store page assets (Steamworks → Edit Store Page)

- Capsules: 460x215 small, 616x353 main, 2310x1740 library hero,
  3840x1240 hero capsule, 384x384 logo — PNG or JPG.
- Screenshots: min 5, 1920x1080 recommended (F12 captures land in
  `shots/` — or the `EI_RECDUMP` frame dumper for motion stills).
- Short description, full description (mention: source builds are free on
  GitHub — the Steam purchase is a donation/support). System requirements:
  Win10+, GL 3.3 GPU, ~200MB disk; Linux: glibc 2.31+ (built on
  ubuntu-latest CI), same GL requirement.
- Release state → "Coming Soon" page needs capsule+screenshots+description
  approved ~2 weeks before launch per Valve rules.

## 5. Dev testing

Drop `steam_appid.txt` (containing just the AppID) next to the exe for
local Steam-context runs. Do NOT ship it in the depot.
