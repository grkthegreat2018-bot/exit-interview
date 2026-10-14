# Bug log

## Wall decorations flickered / z-fought (fixed 2026-10-05)

**Symptom.** Framed posters/certificates shimmered and flickered, worst at
grazing angles — the classic coplanar-face z-fight.

**Root cause.** In the `poster` branch of `DrawItem`, every procedural art
layer drew at the same plane (`face * dep`), and each layer's back face was
exactly coplanar with the card front. The backing plate also sat proud of
the card face by 6 mm, so the card's front surface was swallowed inside the
backing. Plus JSON mounting: `light_switch` sat fully buried inside the
north wall face, `wr_clock` floated off its wall, and the remix
`wall_south` anchors put spawned posters ~0.9 m off the wall.

**Fix.** Per-layer `lift` (+1.2 mm per art layer, starting 1.6 mm off the
card) gives every surface its own depth slice; backing ends 4 mm *behind*
the card face. JSON + remix anchors audited so every mounted face clears
its wall. Lesson for future wall-mounted detail: never share a plane with
the layer beneath — budget at least ~1 mm of separation per coplanar stack.

## Wall clock rendered as a solid dark disc (fixed 2026-10-05)

**Symptom.** After the clock-detail pass the dial was unreadable — a flat
dark circle with no face or hands.

**Root cause.** The new bezel was a *solid* cylinder ending at
`z = s.z + 0.004` — 4 mm in FRONT of the face cap and 0.02 wider in radius —
so its front lid occluded the whole dial. Only the hands (drawn at
`z >= s.z + 0.006`) poked through, invisible against the dark bezel color.

**Fix.** The bezel now ends at `s.z - 0.002`, 2 mm behind the face cap —
only the annular ring shows around the dial. Rule of thumb: a "rim" made
from a solid cylinder must never extend past the face it frames.

## Waiting-room NPCs sat inside their benches (fixed 2026-09-28)

**Symptom.** Ambient NPCs seated in the waiting room looked sunk into the
benches, shins visibly clipping toward the floor.

**Root cause.** `waiting_npc_seats` entries without `seat_h` defaulted to
0.25 while the benches are 0.5 high — hips landed inside the seat slab.
`DrawHumanoid` also let the knee drop straight down, so on tall seats the
shin ran unrealistically deep.

**Fix.** `seat_h: 0.46` on the three bench seats (matches the cushion top at
`s.y * 0.93`); seated-leg math now clamps `kneeY = max(feet.y + 0.28*h,
hipY - 0.10)` so the shin stays a believable length, plus a flat shoe box at
each ankle. Same seat_h check applies to any future seat entries — the value
must match the furniture's seat-surface height, not a default.

## Door/blinds interaction area stayed at the closed pose (fixed 2026-09-28)

**Symptom.** With the door swung open, the interact prompt still anchored to
the doorway where the panel used to be; the same for blinds rotated/moved by
anomalies (e.g. `blinds_crooked`), and raised blinds kept a full-height
invisible click volume across the empty window.

**Fix.** `UpdateRoam`'s interact + look-at rays now go through
`RayVsOBB_Y` (common.h): the door tests the swung panel reconstructed from
its hinge (`pos - size.x/2` pivot, `rotY` sweep), blinds test the live
`pos`/`rotY` box, and open blinds shrink the grab zone to the stacked top
rail. Lesson: any furniture whose geometry moves (hinges, mutations) needs
its interact volume derived from the same transform, not the JSON box.

## Ringing desk phone unanswerable while hidden (fixed 2026-09-28)

**Symptom.** With the new `phone_gone` anomaly (desk_phone `hidden: true`)
active, a call_event would ring forever with no way to answer -> forced
`on_ignore` instability penalty.

**Root cause.** The interact scan in `UpdateRoam` skips all `f.hidden`
furniture, and answering a call only happens through the phone prop's
`interact == "phone"` path.

**Fix.** The scan now lets a hidden `interact == "phone"` item through while
`phone.ringing` is true (the handset is still physically there), and
`DoInteract` prints "Your hand finds the handset in the dark." Same pattern
to remember for any future `hidden` anomaly on an interactable.

## Stats bleed across save loads (fixed 2026-09-28)

**Symptom.** Resuming a saved shift after dying showed the dead run's deeds
(interviews, resolves) on the ending screen.

**Root cause.** `Load()` never reset `deeds`/`paperIdx`/`heartT`/`evalScore`
- those lived only in `NewShift`, which `Load()` doesn't call.

**Fix.** `Load()` now clears `deeds`, resets `paperIdx`/`heartT`/`evalScore`.
Deeds are intentionally per-shift and NOT serialized into save.json.

## Black screen in-game (fixed 2026-09-27)

**Symptom.** 3D scene rendered fully black; HUD/title text (drawn after the
post blit) survived.

**Root cause.** Two compounding issues in the new `src/fx.h` pipeline:

1. The post-process shader was created fs-only (`LoadShaderFromMemory(nullptr,
   POST_FS)`). On this driver (NVIDIA 617.x, GL 3.3, raylib 5.5) the resulting
   program rasterized nothing — a completely black blit. Fixed by supplying an
   explicit passthrough vertex shader (`POST_VS`).
2. The in-world monitor OS quad was drawn into the scene FBO with raw
   `rlBegin(RL_QUADS)` calls, which silently failed (draws dropped / textureId
   reset when the draw-element mode changed). Replaced by `DrawMonitorPoly()`
   → `DrawTexturePro` in the 2D pass on the main framebuffer after `fx.Blit()`.

**Diagnosis trail.** Env toggles `EI_NOLIGHT`/`EI_RAW` bisected the pipeline:
unlit world rendered, light-only rendered (dim), post alone = black;
`EI_DUMPRT` (dump raw scene RT) + a green `DrawRectangle` proved 2D-in-FBO
works while raw rlgl quads did not.

## Can't enter Groq API key in settings (fixed 2026-09-27)

**Symptom.** No visible way to enter the key — the settings UI existed only on
the pause menu, paste failures were silent (GLFW clipboard warnings), and the
masked display gave no confirmation a key was stored.

**Fix.** `Game::ApiKeyField` shared widget (field + PASTE) now lives on the
title screen *and* the pause menu; masked display shows `****last4 (len)`;
paste failures raise a Notify toast; ENTER while focused on title applies the
key instead of starting the shift. Also swept non-ASCII glyphs (`—`, `·`)
out of all rendered strings — the loaded fonts are ASCII-only and rendered
them as `?`.

## Giant smeared plane / OS texture flooding the screen (fixed 2026-09-27)

**Symptom.** A huge blue-grey surface with parallel bands filled the upper
frame at certain camera angles (looked like a corrupted wall/ceiling).

**Root cause.** `DrawMonitorPoly` drew the OS texture as a projected quad, but
the "camera facing the screen" check (`fwd·to > 0.02`) passed even at grazing
angles — projected corners exploded thousands of pixels offscreen and the
smeared quad showed stretched osTex rows (the "bands" = wallpaper/taskbar
rows). Fixed by requiring `fwd·to > 0.30*|to|` and rejecting quads whose
projected points exceed ±4000 px.

Side note: rlgl batch elements keep `textureId` across same-mode draws and
`rlSetTexture(0)` does NOT reset to the default white texture — order matters:
`rlBegin(mode)` first (allocates elem w/ default tex), `rlSetTexture(id)`
second, then verts, `rlEnd`.

## Typing dead in text fields (fixed 2026-09-27)

**Symptom.** Could not type into the report/phone/API-key/dialog fields at
times.

**Root cause.** `GetCharPressed` has one global queue and three consumers:
`FakeOS::TextField` (runs every frame inside `os.Draw`, even in other modes),
`DialogPanel`, `ApiKeyField`. An open REPORT/PHONE app in osTex drained chars
before the focused field saw them. Fixed with a single-owner model:
`os.acceptsText = (mode == SitComputer)`; modes with no text field discard
the queue once per frame.

## "Lights went out" reports rejected (fixed 2026-10-02)

**Symptom.** Office lights visibly kept dying but "power/lights went out"
reports always failed.

**Root cause.** The blackouts were purely atmospheric — `UpdateCommon`
rolled short `blackoutT` dropouts above instability 55 with no backing
anomaly, so there was nothing to resolve. `wr_lights_out` exists but is an
`exterior` (waiting-room) anomaly, reportable only via PHONE > Facilities.

**Fix.** New office anomaly `office_lights` (`kind: "lights_office"`,
targetless, `mutation: {}`) — while active, `ad.HasKind("lights_office")`
pins lamps to ~10% with weak surges plus partial `fx.blackout`, i.e.
sustained darkness that clearly differs from the 50-160 ms ambient dropouts
(now rarer: rate 0.10 -> 0.04). Resolved via the REPORT app like any office
anomaly; nothing to revert since the effect is flag-driven.
`SpawnOffice`/`Deserialize` now copy `kind` onto office anomalies.
Verified: `EI_ANOM=office_lights EI_LLMTEST=report` -> Groq
`{"match": true, "anomaly_id": "office_lights"}`.

## Channel routing rework + wrong-channel rejections (fixed 2026-10-02)

**Symptom.** Calling Security about the shadow figure got "no anomaly found";
office reports of exterior events were rejected; lights stayed out with no
obvious remedy.

**Fix.** Channels now split by what the anomaly IS:
- **Security** = humanoids/danger: intruder, visitor anomalies, AND the
  `figure_at_window`/`figure_at_door` exterior anomalies (resolved via
  `TryResolve` in `CallSecurity`, perimeter sweep). A security call with
  nothing humanoid live gets a steering hint ("building -> Facilities",
  "office -> REPORT").
- **Facilities** = infrastructure/objects: `lights_out`, `empty_room`.
  Figure candidates are filtered out of its LLM prompt + candidates;
  describing a person to Facilities redirects ("call Security") with no
  penalty; a model-side figure match is guarded in `PollLLM` too.
- **Front desk** = flavor/visitor info (unchanged).
- Rejected office reports + facilities calls now run `OtherChannelHint`:
  keyword-scan of live anomalies on OTHER channels -> Notify toast pointing
  at the right channel. `repText`/`facText` keep the last submitted text.

## Text too small (fixed 2026-10-02)

All 2D font sizes scaled ~1.2x (scripted literal bump across game.cpp,
fakeos.cpp, dialogui.h, hud.h; 64 sites). Font atlas bases raised to match
(font 32, fontSmall 20, fontBig 72, fontMono 24) so glyphs stay crisp.

## Visitor answered as a generic assistant (fixed 2026-10-02)

**Symptom.** Saying "Hello" to a seated client got
"Hello, what can I help you with?" — assistant persona, not the roster
character.

**Root cause.** Transcript lines were labeled `You:` in the LLM context; in
chat format that reads as the model's OWN prior turn, so it slipped into
assistant role. The system prompt also never explicitly said the visitor is
the interview subject.

**Fix.** Player lines are now sent as `Interviewer:`; `visitor_system_prompt`
rewritten to lead with identity ("You are {name} ... you are a VISITOR ...
the subject of this interview, not an assistant") and forbid offering help.
Verified: `EI_LLMTEST=dialog` -> Marcus Okafor replies "I'm here for my exit
interview. I'm moving out of state."

## Lights stuck permanently dim, reports "no anomalies" (fixed 2026-10-03)

**Symptom.** Around ~6:05-6:16 PM the room lighting dropped and never came
back. Reports returned "no anomalies" and no spawn cue played.

**Root cause.** Stranded deferred resolution in the two-phase crew flow:
`BeginResolve(deferFx=true)` moved the anomaly from `active` to `pending`, but
`DispatchCrew` returned early when `crew.size() >= 2` — so no crew ever ran
`ApplyResolveFx`. The anomaly was gone from `active` (reports: "no anomaly")
while `HasKind("lights_office")` still saw it in `pending` and kept the room
dim forever. `Reset()` and `Deserialize()` also never cleared `pending`, so
the stuck state could leak across shifts/loads. Separately, the spawn-cue
delta check ran before `EnsureVisitor` synced visitor anomalies into `active`,
so those spawns never triggered the creak/dip.

**Fix.** Queue-full dispatches now apply `ApplyResolveFx` immediately instead
of stranding the anomaly; `Reset()`/`Deserialize()` clear `pending`; visitor
sync moved before the `ActiveCount` delta so every spawn gets its cue.
Ambient instability shimmer capped at ~9% dip + rare 40-90 ms brownouts so it
can't be mistaken for the sustained `office_lights` outage; desk lamp and
monitor glow are an anchor circuit that never flickers.

Note: the perceived "dims when I sit" timing is coincidence — the first spawn
lands at a fixed `nextSpawnIn = 55s` (~6:16 PM at 200 s/game-hour), right
after players typically sit down. Sitting touches nothing lighting-related.

## Scene dims + light "sweeps" when an NPC spawns (fixed 2026-10-04)

**Symptom.** When a humanoid appeared (ambient NPC, visitor, crew) the whole
room dimmed, the light pools visibly slid across the room, wall shading
cycled, then everything stayed dim. Verified: raw scene RT already dark, all
uniform uploads (`lightPos`, `lightCol`, ambient) correct, transform stack
identity.

**Root cause.** rlgl render-batch overflow. A humanoid is ~12k verts
(7 spheres x 1632); the office scene already sits at ~16-18k. Crossing the
32,768-vertex `rlCheckRenderBatchLimit` *inside* a `DrawSphereEx`'s
`rlPushMatrix` flushed the batch with `RLGL.State.transform = translate*scale`
— and rlgl uploads `matModel`/`matNormal` at flush time, applying that matrix
to EVERY vertex accumulated so far. `gl_Position` survived (verts are
CPU-transformed), but `fragPosition`/`fragNormal` were re-transformed — all
light distances wrong -> `att ~ 0` -> ambient-only render. The NPC's motion
moved the stale transform, producing the visible "light sweeps".

**Fix.** The light vertex shader now uses rlgl's already-world-space
`vertexPosition`/`vertexNormal` directly (drops `matModel`/`matNormal`
entirely — redundant AND hazardous), guards zero normals, and procedural
geometry emits an explicit `rlNormal3f` before cylinders/spheres
(`DrawCylinderEx`/`DrawSphere` never emit normals). Sphere LOD cut to a
`Sph()` helper (9x10 / 5x6 rings) in office.cpp + visitor.cpp, keeping the
whole scene comfortably under the 32k batch limit.

Dev hooks used: `EI_DIAG` (per-second lighting state + transform identity),
`EI_DUMPRT` (raw scene-RT dumps), `EI_NOLIGHT`/`EI_NOVIS`/`EI_NOFURN`
(bisection), `EI_FCUT` (draw-order cutoff), dbg modes in the light FS.

## OS quad warped when glancing sideways while seated (fixed 2026-10-04)

**Symptom.** The fake-OS screen smeared/rotated when the seated player
glanced left/right.

**Root cause.** It was drawn as a 2D projected polygon (`GetWorldToScreen` of
the monitor corners) — projection math breaks down at grazing angles even
with the earlier facing/clipping guards.

**Fix.** `DrawMonitorPoly` now emits a real world-space quad pinned to
`Office::MonitorQuad()` inside the 3D pass (after `fx.LitOff` -> emissive,
depth-tested, FBO v-flip handled in texcoords). It cannot warp because it is
scene geometry. Mouse aim still uses `Office::MonitorUV` raycast, unchanged.

