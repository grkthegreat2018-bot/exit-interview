# EXIT INTERVIEW — Agent Notes

C++20 horror/spot-the-anomaly game. raylib rendering, JSON-driven content,
Groq/OpenAI-compatible LLM for dialog + report validation (stub fallback).

## Build & run

```
cmake --preset default
cmake --build build --config Release
build\Release\exit_interview.exe     # must run with assets/ next to it (auto-copied)
```

vcpkg toolchain: `D:/windsurf/vcpkg` (in CMakePresets.json). Deps: raylib,
nlohmann-json, cpp-httplib[openssl]. All tuned in `assets/config.json`.

## Releases / cross-platform

- `.github/workflows/build.yml` — push to GitHub builds **win64** (VS) and
  **linux64** (ubuntu-latest + X11 dev headers + vcpkg `x64-linux`) and
  uploads `exit-interview-win64.zip` / `exit-interview-linux64.tar.gz`
  artifacts. No local Linux needed; the source has no Windows-only APIs.
- `linux` CMake preset exists for a WSL/manual build (`$env{VCPKG_ROOT}`
  toolchain, ninja, `build-linux/`).
- Credits/repo line on the title are data-driven: `game.credits` and
  `game.repo_url` in `assets/config.json` — fix the URL after the repo
  is created.

## Architecture

- `src/game.cpp` — Init, state machine + core glue (modes: Title/Roam/
  SitVisitor/SitComputer/Pause/Dead/Win/Jumpscare), update+draw loops,
  interactions. LLM request ids polled in PollLLM. Split out of game.cpp:
  `crew.cpp` (dispatch/A*/walk-in crews), `comms.cpp` (reports, phone calls,
  roster/paperwork LLM prompts, PollLLM), `menus.cpp` (title/pause/settings
  widgets), `persist.cpp` (settings/save/profile import-export), `ending.cpp`
  (eval score/grade, ending screen, jumpscare/death, debug overlay).
- `src/office.*` — JSON furniture model + renderer (raylib primitives only).
  `ApplyMutation` applies anomaly deltas; director snapshots Furni to revert.
- `src/visitor.*` — visitor FSM (ToDoor→AtDoor→ToChair→Seated→Leaving),
  humanoid procedural renderer, waiting-room ambient NPCs.
- `src/anomalies.*` — instability meter (hidden), tiered spawner, resolve.
  Channels: office (REPORT app), exterior infrastructure (Phone>Facilities),
  anything humanoid/dangerous (Phone>Security) - visitor anomalies, intruder,
  and exterior figure_* anomalies. Front desk is flavor/visitor info.
- Crew dispatch (`Game::DispatchCrew`/`UpdateCrew`/`DrawCrew` + `PointBlocked`/
  `PickStandSpot`/`NavPath` in game.cpp): accepted reports/calls spawn hi-vis
  workers or navy guards who spawn OFF camera around the corridor's L-turn
  (8.9,-12.55), file in THROUGH the hall door (pathIn prepends the corner +
  doorway waypoints), A* to a furniture-free stand spot filtered to the SAME
  side of the doorZ=-5 wall as the work target (never behind the wall), act
  (planted ladder + elevated arm-raise for ceiling lights, box swap for
  vanished items, toolbox for moved/darkened, movers carry spawned furniture
  out, guards sweep/escort), then file back out through the hall door and
  past the corridor's L-turn (x=11.9) before despawning - nothing pops in
  view of an open hall door. Carry-outs are restricted to what two movers
  can plausibly lift (`Carryable()` in crew.cpp): wall-mounted posters/
  clocks are unmounted first (slide down during the work phase), while
  wired-in or heavy spawns (outlet, door, rug, cabinet, cooler, couch)
  get `dismantleId` - torn down on site and gone when the work ends.
  Security escorts now DETAIN anomalous visitors: on phase 1 they face the
  guards (cuff beat, `Visitor.detained` + head-bow/wrists-behind pose via
  DrawHumanoid's `detain` arg), then phase 2 marches them out pinned
  between the two guards (`detainWalk` drives the gait) - Gone is set when
  the detail despawns, never a self-walk.
  The hall_door Furni itself swings open (rotY lerped in UpdateCommon) when
  any crew man / visitor / ambient NPC is near it.
  During the work phase, crew physically push moved/turned/tinted furniture
  back toward its `BeginResolve` snapshot (smoothstep lerp on
  pos/rotY/size/color; the closest free worker holds a push pose via
  armRaise~0.55; ApplyResolveFx snaps the remainder), and the
  desk_drawer anomaly's slide amount tracks their progress.
  Positional anomaly mutations (translate/pos_set/rot_set) don't pop either:
  `Office::ApplyMutationAnimated` registers a MoveGlide (ease-out-back, ~0.6s)
  advanced by `Office::Update` in UpdateCommon - Game plays the new
  `AudioBank::Sfx::Screech` + a shake once per glide (`g.announced`).
  `BeginResolve(deferFx)` keeps the anomaly visibly active
  until the work phase ends (`ApplyResolveFx`). Nav grid insets rooms by
  margin but NOT the z=-5 dividing wall - doorX gap stays walkable.
  OtherChannelHint() steers wrong-channel reports to the right phone line.
- `src/llm.*` — async queue, 2 workers, Groq/local HTTP + stub backend.
  429 → save+pause via game PollLLM.
- `src/fakeos.*` — monitor UI (apps). `src/dialogui.h` — interview panel.
- `src/audio.h` — synthesized WAV sounds at runtime, incl. instability
  drone (SetDroneLevel) + hum, Buzz/Heart/Creak sfx. One binary asset
  family: streamed menu themes in `assets/Music/` (Music stream,
  `music.looping`; ~2.2s fade-in; failed loads remembered in `musicBad`
  and skipped). The rotation is directory-scanned every time `night`
  changes - drop any .mp3/.flac/.ogg/.wav in and it joins. `not as it
  seems.flac` is gated to night >= 2 (survive a full shift first).
  Title/Dead/Win keep it playing; shift start stops it. A now-playing
  chip (name + elapsed/length + progress) sits top-left on the title.
  `Menu_*.mp3` were synthesized by `.devin/gen_music.py` (numpy -> wav ->
  ffmpeg lame). Volume = master x per-category slider (AUDIO tab, all
  cap at 200%): vol_sfx, vol_ambience, vol_drone, vol_music.
- `src/fx.h` — render pipeline: point-light shader (ambient + up to 10
  lights + fog + world-noise albedo) for the 3D pass into a supersampled
  scene RenderTexture (`render_scale` in config), then a post blit
  (vignette/grain/chromatic aberration/instability warp/blackout/scare).
  Lights are auto-derived from furniture in `Game::CollectLights`.
  NOTE: the light VS consumes rlgl's already-world-space vertexPosition/
  vertexNormal - never matModel/matNormal (flush-time transform is stale on
  batch overflow -> whole-scene lighting corruption). Keep humanoid vertex
  counts low (use the local `Sph()` LOD helper, not raw DrawSphere).
- `src/fx.h` — render pipeline: point-light shader (ambient + up to 10
  lights + fog + world-noise albedo) for the 3D pass into a supersampled
  scene RenderTexture (`render_scale` in config), then a post blit with
  FXAA + filmic grade (cool shadows/warm highlights, `EI_GRADE` override)
  + vignette/grain/chromatic aberration/instability warp/blackout/scare.
- `office.cpp` — `DrawBlobShadow()` paints soft contact-AO pools under
  floor-standing furniture + all humanoids (inside the lit pass is safe:
  blob texel rgb is 0 so base*acc stays black; wraps verts in
  rlDisableDepthMask so coplanar blobs blend, never z-fight).
- Shift evaluation: `deeds[]` counters feed `EvalScore()` (0-100) +
  `EvalGrade()` (S/A/B/C/D) on the ending screen; `last_eval` persists in
  settings.json and shows on the title.
- Anomaly channels (config tier weights): office | exterior | visitor |
  intruder | call | remix. `hardcodedOnly` (settings, now DEFAULTS ON -
  the ANOM: SCRIPT toggle) sets ad.freeform=false, gating call+remix so
  the base game runs scripted anomalies only; the toggle re-enables LLM.
  Exterior defs may now also carry target+mutation (waiting-room
  furniture) - SpawnExterior applies them like SpawnOffice (Facilities
  channel), and Deserialize re-applies them on load.
  `call` = desk-phone event: pendingCall -> Game
  rings -> DialogPanel conversation (LLM purpose `phonecall`) -> `phoneeval`
  verdict -> persona's on_calm/on_ignore/on_hostile JSON effects via
  `ad.TriggerEffect` (instability/figure/lights/banging/relief).
  Call personas are LLM-generated per shift too: `RequestCallPersonas`
  (purpose "callpersonas") runs alongside the roster/paperwork jobs on the
  title screen; validated events fold into `defs["call_events"]` via
  `ad.InjectCallEvents` (required fields enforced, effect blocks clamped to
  the TriggerEffect vocabulary). Stock events stay in the pool either way.
  `remix` = LLM-designed anomaly validated against `assets/anomaly_parts.json`
  (spawnable types/anchors/limits/mutation whitelist) via `ad.ApplyRemixJson`.
  Both are gated by `ad.freeform` (the SCRIPTED ONLY toggle).
- New anomaly kinds: window_flip/window_void (office.windowView vista),
  hall_long (office.hallLong corridor), banging_door/banging_window
  (vs.banger* + BangerStrike -> Knock sfx), mutated_waiting
  (vs.ambientMutated - stretched pale NPCs track the player; routed to
  Phone>Security like the figures), eyes mutation (Furni.eyes -
  office.eyeTarget = player eye pos), hall_writing (periodic scratching sfx
  from the walls), note_slid (spawned "note" furni slides in under the door
  via UpdateCommon), spin_chair (visitor_chair rotY creeps until a crew
  holds it), desk_drawer (office.drawerOpen/drawerT drives the desk's top
  drawer slide-out; crew push it back in as they work).
  Mutations: translate/pos_set/rot_set/scale/color/hidden/eyes/stopped/
  backwards (wall clocks run counter-clockwise)/art (poster art swap:
  "figure" = landscape + a dark silhouette standing in it, "face" = ID
  photo with a hollow face + staring eyes, "scrawl" = frantic scribbles
  over paper)/spawn (new Furni; "note", "printer" etc are spawnable types
  - every type has a DrawItem branch). Spawn defs accept art/eyes/
  stopped/backwards too (a dead clock, an eyed prop, a scrawled sign),
  plus room:"waiting" for waiting-room drops and interact:"note" for
  readable lore slips. SpawnVisitorCorruption's soft-kind pool is
  data-driven: every visitor_anomalies key except the gated
  impossible_visitor/hostile pairs joins the random rotation - new JSON
  kinds need only a def + a dialogs addendum to be live.
  `Furni.ft` is the cached `FT` enum of the `type` string - compare enums in
  per-frame code, never strings. `Office::Solids()` returns a cached
  `const vector<AABB>&` rebuilt at most once per frame (tick bumps in
  `Office::Update`); copy it if you append transient boxes.
- Humanoid walk cycle: `DrawHumanoid(..., walkAmt, detain)` - two-segment
  arms/legs with real elbows/knees, head bobs twice a stride, chest breathes
  at idle, hips rock across the stance. `detain=1` is the cuffed escort
  pose: wrists crossed behind the back, head bowed (single `hb` anchor
  drives head+hair+eyes). pass walkAmt 1 while pathing, 0 when planted. Ambient waiting NPCs path to a stand-spot in FRONT of their
  seat, then sitT lerps them into the cushion - no more walking into chairs.
  They also CHURN: each seated ambient gets leaveT (~90-240s); on expiry one
  at a time they stand (reverse sit glide to the stand-spot), walk
  paths["ambient_exit"] out through the hall door, and despawn - seats free
  up and new arrivals trickle in. Frozen while ambientMutated/waitingEmpty.
- Visitors and crew now leave through the hall door - visitor_out and
  ambient_exit paths end around the corridor's L-turn at (8.9,-12.55)
  instead of vanishing mid-room, and hall_door swings open for anyone near
  it. A visitor arriving while the office door is already swung open comes
  in on their own (AtDoor: door rotY < -25 -> InviteIn after ~1.6s, no
  knock/no E).
- A visitor who leaves un-interviewed (Seated->Leaving/Gone with no player
  lines via SayToVisitor, and no anomaly tag) costs a warning:
  deeds["visitor_missed"]++, a 10s toast, -6 on EvalScore, and an ending
  row. Ending an interview you never questioned counts as a miss.
- New furniture: "mouse" (on desk), "pc_tower" (under desk left, solid -
  keep the hide pocket clear), "outlet" (wall plates + art:"floor" plates),
  cords drawn in `Office::DrawCables()` (world space, pc->monitor +
  pc->outlet_floor, follows live positions when anomalies move the pc).
- LLM roster appearances drive the 3D models end-to-end
  (appearance{height,skin,shirt,hair,eyes} -> HumanoidApp via AppFromJson);
  personas prefixed "ANOMALOUS" or with non-normal eyes are force-flagged
  always_anomaly so a mutant never sits in a mundane slot.
  Accessories: appearance.glasses (bool|[rgb]), .beard (bool|0-2|
  "stubble"|"full"), .tie (bool|[rgb]), .badge (bool) - explicit JSON
  wins, otherwise a per-persona hash seeds them so the cast varies.
  ApplyRosterJson now passes gender/hair_style/pants/eye_color through
  too (they used to be dropped). EI_NPC keys: glasses[=r.g.b],
  beard=0|1|2|stubble|full, tie[=r.g.b], badge[=0|1].
- Hide pocket lore: a paper note is taped to the desk underside (drawn in
  the desk branch); while `player.hiding` the HUD shows its contents as a
  parchment card ("file the report. - M.").
- Wall-clock face: bezel cylinder must end BEHIND the dial cap (see
  bugs.md) - ticks + h/m/second hands + centre pin; stops on `stopped`.
- fakeos taskbar clock: `os.clockHour` (set from gameHour each frame; <0 hides it).
- Desk phone prop: `desk_phone` (type "phone", interact "phone"); sit also
  via monitor ("sit_monitor", gated chair-side + close); door animates via
  `doorAnim` smoothstep and the swung panel blocks the player;
  PHONE>FRONT DESK>SEND NEXT VISITOR IN calls early (`vs.CallNextEarly`).
- Desk hide pocket: crouching drops the desk's slab AABB but the two side
  legs, visitor-side modesty panel, and drawer pedestal keep collision via
  rotated sub-boxes in `UpdateRoam` (they track `pos`/`rotY`/`size`, so desk
  anomalies still behave). Chair-side edge is the only entry.
- Interact rays for door/blinds use `RayVsOBB_Y` (common.h) built from the
  live transform - hinge math for the door (`pos.x - size.x/2` pivot), plain
  yaw OBB for blinds (shrinks to the top rail while open).
- Camera feel (Roam only): footsteps on bob zero-crossings (panned L/R,
  quieter crouched), strafe roll on `cam.up`, sprint FOV +4.5, lateral sway,
  idle breath, stop-settle dip. Seated modes force `cam.up` level.

## Render pipeline notes

- `DrawWorld()` renders the 3D scene into `fx.scene` (RenderTexture) —
  `fx.BeginWorld()` turns on the light shader, `fx.LitOff()` ends it before
  unlit/emissive draws (shadow figures, glass). `fx.Blit()` post-processes
  to the screen after BeginDrawing; HUD/UI stays at screen res.
- Custom shaders MUST supply an explicit vertex shader — raylib 5.5's
  fs-only path produced a silently-dead program on this driver (NVIDIA
  617.x / GL 3.3) → black screen.
- The in-world monitor OS (`osTex` RenderTexture) is a REAL world-space
  quad on the monitor face (`DrawMonitorPoly` inside the 3D pass after
  `LitOff`, depth-tested, FBO v-flip in texcoords) — NOT a 2D projected
  polygon (that warped at grazing angles). Mouse aims via `MonitorUV` raycast.
- Menus: Title and Pause both use `menuPage` (0 main, 1 settings). The
  settings page (`Game::DrawSettings`) has 4 tabs - GENERAL (sensitivity,
  night reset, reset-all), AUDIO (master/sfx/ambience sliders ->
  `volMaster`/`AudioBank::sfxMul`/`ambMul` via `ApplyAudio`), CONTROLS
  (rebindable `KeyMap` keys with conflict-swap; `Game::KeyName` returns
  std::string - a shared static buffer aliased all keys to one letter),
  LLM (provider cycle, ApiKeyField, endpoint/model `TextRow` overrides,
  scripted/+LLM anomaly toggle, PING via `testReq`/`testResult` polled in
  PollLLM). Every widget registers `tip` for the tooltip bar.
- API-key field (`Game::ApiKeyField`) lives on the LLM tab. ENTER applies
  via ApplyProvider; while focused on title it does NOT start the shift.
- Breach: window threat (blinds/broken glass + figure/banger) OR door
  threat (office door open + figure/banger at door). A broken line of
  defence collapses the grace: smashed window x0.35, open door x0.30 of
  `breach_sec`, and `AnomalyDirector`'s intruder timer drains ~2.4x while
  glass is broken or the door stands open (`breachDoor` picks the door
  rush + "IT CAME THROUGH THE DOOR" ending).

## Dev hooks (env vars)

- `EI_SHOT=<sec>` — auto screenshot to `shots/ei_shot.png` + quit.
- `EI_PLAY=1|sit` — skip title straight into Roam / seated at computer.
- `EI_DEBUG=1` — F3 debug overlay + osTex thumbnail on from boot.
- `EI_YAW=<deg>` / `EI_PITCH=<deg>` — set spawn look direction.
- `EI_CAM="x,y,z,yaw,pitch"` — exact debug camera pose; enables flycam +
  debug so EI_SHOT frames land precisely (e.g. `EI_CAM="4.6,1.6,0.5,90,0"`).
- `EI_NOLIGHT` / `EI_RAW` — disable light shader / post blit (bisecting).
- `EI_DUMPRT` — dump raw scene RT to `shots/ei_rt_<sec>.png` at fixed times.
- `EI_NPC="x,y,z[,yaw][,flags/keys]; ..."` — posed humanoid dummies with no
  AI, for debug screenshots. 4th bare number = yaw deg. Flags: `seated`,
  `walk`, `detain`, `female`, `male`, `frozen` (gender seeds randomly
  when neither flag is given). Keys: `raise=0..1`, `anim=rad`,
  `hair=0..6`, `h=scale`, `seat=h`, `eyes=normal|black|none`,
  `skin|shirt|pants|hairc|eyecol=r.g.b` (dot-separated). Combine with
  `EI_NOVIS=1` to hide the real visitor/ambient cast.
- `EI_ANOM=<id>` — force-spawn an office anomaly by def id (e.g. office_lights).
- `EI_KIND=<kind>` — force-spawn any anomaly by kind (banging_window,
  window_flip, mutated_waiting, hall_long, figure_at_door...).
- `EI_CALL=1` — force the desk-phone call event at boot (first persona).
- `EI_DOOR=1` — start with the office door swung open.
- `EI_HIDE=1` — spawn crouched inside the desk hide pocket.
- `EI_BLINDS=1` — start with the blinds raised.
- `EI_WIN=1` — jump to the shift-end eval screen (stuffed sample deeds).
- `EI_GRADE=<0..1>` — post grade strength override (default 0.22).
- `EI_LLMTEST=1|dialog|report` — live LLM check: frontdesk ping / real
  persona dialog ("What brings you in tonight?" to m_okafor) / report
  submission ("the office lights went out"). Logs backend/http/text.
- `EI_MENU=<0..3>` — pin the title settings page open on a tab (shots).
- `EI_PAUSE=1` — pop the pause menu once after entering Roam/seated (shots).
- `EI_APP=book|phone|paper|building` — pin a monitor app open (EI_PLAY=sit).
- `EI_HOUR=<abs>` / `EI_VISIT=<slot>` — jump shift clock / schedule index.
- `EI_PATH="t,x,y,z,yaw,pitch; ..."` — scripted camera keyframes (flycam
  rides smoothstep-lerped positions; input ignored; auto-quits 1s past the
  last key). HUD/prompts/notes are suppressed while a path runs.
- `EI_RECDUMP=<fps>` — dumps every rendered frame to `shots/rec/f_%05d.png`
  and switches the sim to a FIXED dt=1/fps timestep so PNG write cost never
  warps motion. The `EI_PATH` clock also runs on this accumulator, so path
  timing stays frame-exact. Auto-enables `cleanDebug`.
- `EI_CLEAN=1` — clean-recording mode: flycam/path mechanics still work but
  all debug chrome is hidden (F3 overlay, osTex thumbnail, HUD clock,
  prompts, toasts, crosshair, ring indicator). Implied by `EI_RECDUMP`.
- `EI_UPDATE=off|fake` — disable the github update check / inject a canned
  "update available" popup for UI shots. After a real apply, the apply
  script drops `_upd_done.txt` in the install root and the game shows the
  "update applied" card once on next boot.
- F3 debug overlay now draws a live state block (pos/cam/mode/door/blinds/
  visitor/anomaly ids); F4 toggles FLYCAM — free-fly camera, player body
  locked, WASD + SPACE/CTRL + SHIFT-fast, F6 drops the player at the lens,
  F7 bookmarks the pose, F8 snaps back to it (enters flycam if needed).
- `F12` in-game — manual screenshot to `shots/screenshot.png`.
- All dev screenshots/RT dumps write under `shots/` (auto-created at boot);
  raylib TakeScreenshot strips directories, so captures use
  LoadImageFromScreen + ExportImage instead.
- `F9` (with F3 debug on) — hot-reload data JSONs: anomaly catalog, dialogs,
  config tuning, remix parts. Office layout + live anomaly snapshots stay put.
- All `EI_*` env lookups go through `DevEnv("X")` (common.h) - cached, safe in
  per-frame paths. Don't call std::getenv in draw/update loops.
- `Assets` (src/assets.*) resolves assets/ from cwd, exe dir, or up to 3
  parents; per-file load errors land in `lastError`, validation notes in
  `warnings`. Optional bundled fonts: `assets/fonts/{regular|ui,mono}.ttf` win
  over the Windows fallback list.

## Conventions

- All game content lives in `assets/*.json` — prefer editing data over code.
- JSON helpers in `common.h` (JF/JI/JS/JV3/JC). Furniture pos = bottom-center.
- Raylib DrawCubeV is CENTER-aligned; use local `Box()` for bottom-aligned.
- Time is absolute game hour 18..30 (next-day hours >24).
- Fonts are ASCII-only (95 glyphs) — keep rendered strings ASCII.
- Settings persist to `settings.json` (provider/api_key/sensitivity,
  vol_master/vol_sfx/vol_ambience/vol_drone/vol_music (all 0..2 = 200% cap), keys{...}, llm_endpoint/llm_model,
  hardcoded_anomalies, seen_names, night, last_eval) next to the exe;
  save file is `save.json`.
- ZIP-build migration: EXPORT/IMPORT PROFILE buttons (settings GENERAL) pack
  settings.json + save.json into `ei_profile.json` next to the exe. A build
  with no settings.json auto-imports it on boot — drop the one file into a
  new ZIP build's folder to carry over everything, API key included.

## Auto-updater (src/update.h)

- `GameUpdater` checks `api.github.com/repos/<owner/repo>/releases/latest`
  once per boot from the title screen (repo parsed from
  `game.repo_url` in config.json; current version from `game.version`).
- Popup on title shows version delta + scrollable changelog (release body);
  DOWNLOAD fetches the platform asset (`*-win64.zip` / `*-linux64.tar.gz`)
  into `_update/pkg.bin`, RESTART & APPLY writes a detached apply script
  (powershell on win / sh on linux) that waits for the game pid to exit,
  unpacks, sha256-diffs every manifest entry (only changed files copied,
  files dropped between releases are removed via the OLD root
  `manifest.json`), stamps `_upd_done.txt`, and relaunches. Save files,
  settings and `ei_profile.json` are never in the manifest -> untouched.
- IMPORTANT: keep the apply-script literals at file scope and OUTSIDE
  `#ifdef` - MSVC does not recognize raw strings in skipped preprocessor
  regions, and the shell script's `/*` globs would lex as an unterminated
  comment (C1071) in the Windows build.
- CI (`.github/workflows/build.yml`) builds both packages on every push and
  embeds `manifest.json` (sha256 per file); on `v*` tags it stamps
  `game.version` from the tag and publishes a GitHub Release with both
  assets - the release body IS the popup changelog.

## Trailer tooling (trailer.py)

- `python trailer.py` renders `trailer.mp4` (~45s, 1080p24): it launches
  the game once per scripted scene with `EI_PLAY=1 EI_PATH=... EI_RECDUMP=24`,
  collects `shots/rec/f_*.png`, ffmpeg-encodes each clip, adds drawtext
  title cards + a menu music track. Scenes are data (SCENES list) - path
  keys are `t,x,y,z,yaw,pitch` (yaw 0 = +z). `--scene <name>` renders one
  clip for iteration, `--keep` keeps frame dirs.
- Scene rule: atmosphere + mechanics only, no lore reveals (no figures,
  no endings, no notes content - `wrongness` uses desk_float as the one
  restrained hint).
