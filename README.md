# EXIT INTERVIEW

A horror / spot-the-anomaly game. You are the night-shift exit-interview officer at
Meridian Logistics. Employees come to your office for their exit interviews.
Not everything that sits down across your desk is an employee.

## Get the game

- **Steam** — buying it there is a *donation*: it funds the work, and nothing
  is locked behind it.
- **Here** — this repo always carries the latest source; build it yourself for
  free and you get the newest build before Steam does.
- Windows and Linux packages build automatically on every push via CI
  (`.github/workflows/build.yml`) — grab them from the Actions artifacts.

## Build (Windows, MSVC + vcpkg)

```
cmake --preset default
cmake --build build --config Release
build\Release\exit_interview.exe
```

vcpkg path is `D:/windsurf/vcpkg` (edit `CMakePresets.json` if different).

## How to play

- Survive the shift: **6:00 PM → 6:00 AM**.
- **WASD** move, **mouse** look, **E** interact, **ESC** pause.
- Visitors knock at the office door — press **E** on the door to let them in.
- **Sit at your desk** (E on your chair) - the camera eases into the seat. The
  fake OS lives on the physical monitor: aim at it with the mouse to click and
  type. **TAB** toggles between facing the visitor and the monitor. Hold **A**
  or **D** to lean sideways and peek at the visitor or the window. Bad things
  can happen while you look away.
- Talk to the visitor by typing. Cross-check their story against the
  **APPOINTMENTS** and **PERSONNEL** apps on the computer.

### Handling anomalies

| Where the anomaly is | What to do |
|---|---|
| Something changed **in the office** | Computer → **REPORT**, describe it |
| Something wrong **outside** (window / waiting room) | Computer → **PHONE → Facilities**, describe it |
| **The visitor** is the anomaly | Computer → **PHONE → Security** |
| **Someone standing in your office** | Computer → **PHONE → Security**, fast |

Wrong reports and wrong calls raise the (hidden) instability. Higher instability
= more frequent + more dangerous anomalies. At 100% you are terminated.

At dawn you get a **shift evaluation** (S/A/B/C/D) scored on interviews
completed, anomalies resolved, forms filed, and mistakes made - it persists on
the title screen as your LAST EVALUATION.

## LLM

- Default backend: **Groq** (`GROQ_API_KEY` env var, or paste the key in
  Pause → settings). Model/endpoint in `assets/config.json`.
- Without a key or network, a built-in **stub** keeps the game playable
  (canned lines + keyword report validation).
- Provider selectable in pause menu: `groq` / `local` (OpenAI-compatible,
  e.g. Ollama at `localhost:11434`) / `stub`.
- On HTTP 429 the game auto-saves and pauses; offline stub covers the cooldown.

## Layout

- `assets/config.json` — tuning (shift length, instability curve, LLM)
- `assets/office.json` — room geometry, furniture, paths, anomaly spawn points
- `assets/visitors.json` — roster personas + tonight's schedule
- `assets/anomalies.json` — anomaly defs per channel + validator keywords
- `assets/dialogs.json` — LLM prompts, stub lines, tutorial sticky note
