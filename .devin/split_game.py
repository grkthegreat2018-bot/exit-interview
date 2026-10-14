#!/usr/bin/env python3
# One-shot refactor: split src/game.cpp into focused translation units.
# Every top-level block (leading //-comment + function/static def) moves whole.
import re, pathlib

SRC = pathlib.Path(__file__).parent.parent / "src" / "game.cpp"
lines = SRC.read_text(encoding="utf-8").splitlines(keepends=True)

fn_start = re.compile(r"^[A-Za-z_].*?\bGame::|^static ")
starts = []
for idx, ln in enumerate(lines):
    if idx >= 5 and fn_start.match(ln) and not ln.startswith((" ", "\t")):
        if re.match(r"^(if|for|while|switch|return|else)\b", ln):
            continue
        starts.append(idx)

def block_start(idx):
    j = idx - 1
    while j >= 0 and lines[j].strip().startswith("//"):
        j -= 1
    return j + 1

spans = []
for k, s in enumerate(starts):
    bs = block_start(s)
    e = block_start(starts[k + 1]) if k + 1 < len(starts) else len(lines)
    spans.append([bs, e, lines[s].strip()])

# Move trailing comment-only/blank lines (section banners for the NEXT block)
# out of each span and into the following span.
for k in range(len(spans) - 1):
    bs, e, sig = spans[k]
    j = e - 1
    while j >= bs and (lines[j].strip() == "" or lines[j].strip().startswith("//")):
        j -= 1
    tail = j + 1
    if tail > bs and tail < e:
        # only split if the tail begins with a comment or blank
        moved = lines[tail:e]
        if all(l.strip() == "" or l.strip().startswith("//") for l in moved):
            spans[k][1] = tail
            spans[k + 1][0] = tail

NAME = re.compile(r"Game::(\w+)|static (?:void|std::string|const) (\w+)")
def fname(sig):
    m = NAME.search(sig)
    return m.group(1) or m.group(2)

TARGET = {
    "persist.cpp": {"ApplyProvider", "ApplyAudio", "LoadSettings", "SaveSettings",
                    "ExportProfile", "ImportProfile", "Save", "Load"},
    "comms.cpp": {"RequestRoster", "RequestPaperwork", "ApplyPaperworkJson",
                  "BuildVisitorSystem", "SayToVisitor", "SubmitReport",
                  "OtherChannelHint", "CallSecurity", "CallFacilities",
                  "CallFrontDesk", "OnIntruderStrike", "UpdatePhone",
                  "AnswerPhone", "PhoneSay", "HangUpPhone", "PhoneOutcome",
                  "PollLLM"},
    "crew.cpp": {"DrawLadder", "PointBlocked", "NavPath", "PickStandSpot",
                 "DispatchCrew", "UpdateCrew", "DrawCrew"},
    "menus.cpp": {"KeyName", "BindLabel", "MenuBtn", "SliderRow", "TextRow",
                  "DrawTooltipBar", "DrawTitle", "ApiKeyField", "DrawSettings",
                  "DrawPause"},
    "ending.cpp": {"EvalScore", "EvalGrade", "PlayerSees", "AllDutiesDone",
                   "AllVisitorsDone", "WinShift", "DrawEnding", "DrawDebugOverlay",
                   "Jumpscare", "Die", "AppointmentsLines", "RecordLines"},
}
# statics handled separately: ReplaceAll + VERIFY_REQ -> common.h
MOVE_TO_COMMON = {"ReplaceAll", "VERIFY_REQ"}

out = {t: [] for t in TARGET}
keep = []  # stays in game.cpp
common = []
for bs, e, sig in spans:
    n = fname(sig)
    body = "".join(lines[bs:e])
    if n in MOVE_TO_COMMON:
        common.append(body)
    else:
        dest = next((t for t, s in TARGET.items() if n in s), None)
        (out[dest].append(body) if dest else keep.append(body))

HDR = '#include "game.h"\n#include "hud.h"\n#include <rlgl.h>\n#include <cstdlib>\n#include <cstring>\n\n'
srcdir = SRC.parent
for t, blocks in out.items():
    (srcdir / t).write_text(HDR + "\n".join(b.rstrip() + "\n" for b in blocks), encoding="utf-8")
    print(f"wrote {t}: {sum(len(b) for b in blocks)} bytes, {len(blocks)} fns")

new_game = "".join(lines[:6]) + "\n" + "\n".join(b.rstrip() + "\n" for b in keep)
SRC.write_text(new_game, encoding="utf-8")
print(f"game.cpp now {len(new_game)} bytes, {len(keep)} fns kept")
print("COMMON:", [b.split('(')[0].strip()[:60] for b in common])
