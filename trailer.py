#!/usr/bin/env python3
"""
trailer.py - renders a non-spoiler trailer for EXIT INTERVIEW.

How it works:
  1. For each scripted scene below, the game is launched with dev env hooks:
       EI_PLAY=1        skip the title menu, straight into the shift
       EI_PATH=...      "t,x,y,z,yaw,pitch; ..." smoothstep camera keyframes
       EI_RECDUMP=24    fixed-timestep frame dump -> shots/rec/f_*.png
     plus per-scene hooks (EI_DOOR, EI_NPC, EI_ANOM, ...). The game quits
     itself 1s past the last keyframe.
  2. Frames are collected per scene, ffmpeg encodes each clip at 1080p24
     with a short fade in/out.
  3. Title cards (black frames + drawtext) are generated, everything is
     concatenated, and a menu music track is laid under it (fade out).

No lore is revealed: scenes show the office, the terminal, the waiting
room, one visitor at the door, and one restrained "something is wrong"
beat. Run from the repo root:

    python trailer.py                # full trailer -> trailer.mp4
    python trailer.py --scene push_in  # render one scene only (testing)
    python trailer.py --keep         # keep per-scene work dirs
"""

import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(ROOT, "build", "Release", "exit_interview.exe")
CWD = os.path.join(ROOT, "build", "Release")          # assets live here
WORK = os.path.join(ROOT, "trailer_work")
OUT = os.path.join(ROOT, "trailer.mp4")
FPS = 24
W, H = 1920, 1080

FONT = os.environ.get("TRAILER_FONT", r"C:/Windows/Fonts/arialbd.ttf")
MUSIC = os.environ.get("TRAILER_MUSIC", "")            # auto-picked if empty

# ---------------------------------------------------------------- scenes ---
# path keys: t(seconds),x,y,z,yaw,pitch.  yaw 0 faces +z (toward the desk
# from the door side), yaw 180 faces -z (toward the door/waiting room).
SCENES = [
    # slow push from just inside the office door toward the desk
    dict(name="push_in",
         env={},
         path="0,0.3,1.7,-3.4,2,-4;6.5,-0.4,1.5,-0.6,8,-3"),

    # GAMEPLAY: a real visitor makes the trip - corridor approach, knock,
    # door swings open, they walk in and take the chair. Player POV from
    # behind the desk; ends seated facing them. ~22s, trimmed in encode.
    dict(name="arrival",
         env={"EI_SEQ": "0.3,visitor;15.5,door;20.8,sitv;25,quit"},
         trim=(10.0, 24.0)),

    # GAMEPLAY: the interview - swap off the monitor, ask a line
    dict(name="interview",
         env={"EI_SEQ": "0.2,visitor;0.5,invite;16,sitv;18,say:state your business.;22,quit"},
         trim=(16.5, 21.5)),

    # GAMEPLAY: seated at the terminal - REPORT app then PAPERWORK flips on
    # the in-world monitor OS
    dict(name="computer",
         env={"EI_SEQ": "0.3,sit;1.8,app:report;4.2,app:paper;6.5,quit"},
         trim=(1.2, 6.0)),

    # waiting room pan - benches, magazines, seated dummies (EI_NOVIS keeps
    # the real ambient cast out so the shot is fully controlled)
    dict(name="waiting",
         env={"EI_NOVIS": "1",
              "EI_NPC": "-2.7,0,-9.7,0,seated,female;"
                        "1.9,0,-8.45,0,seated,male,beard=2;"
                        "3.0,0,-8.45,0,seated,glasses,look"},
         path="0,-5.0,1.6,-6.6,118,-5;7,-1.6,1.6,-9.4,196,-4"),

    # the one "wrongness" beat - the desk floats where it should not
    dict(name="wrongness",
         env={"EI_ANOM": "desk_float"},
         path="0,0.4,1.6,-3.2,-2,-2;5.5,-0.3,1.45,-1.6,-7,-6"),

    # GAMEPLAY: under the desk - hide pocket + the taped note card reads
    # itself while crouched (chair-side gap view back into the room)
    dict(name="hide",
         env={"EI_SEQ": "0.2,tp:0,1.55,0;0.6,hold:Q,4;5.5,quit"},
         trim=(0.8, 5.0)),

    # pull back out through the doorway, office receding
    dict(name="pullback",
         env={"EI_DOOR": "1"},
         path="0,0,1.55,0.9,182,-4;6.5,0.2,1.7,-6.6,183,-2"),
]

# ----------------------------------------------------------------- cards ---
CARDS = [
    ("MERIDIAN LOGISTICS",            "calder building - night shift", 2.6),
    ("EXIT INTERVIEW",                "report the anomalies. survive the shift.", 3.4),
]
END_CARD = ("GRKTheGreat & Devin Desktop - 2026",
            "made with AI   -   github.com/grkthegreat2018-bot/exit-interview", 4.0)


def run(cmd, **kw):
    print("+", " ".join(str(c)[:110] for c in cmd))
    r = subprocess.run([str(c) for c in cmd], **kw)
    if r.returncode != 0:
        sys.exit(f"command failed ({r.returncode}): {cmd[0]}")
    return r


def pick_music():
    """First playable track in assets/Music, preferring user tracks."""
    d = os.path.join(ROOT, "assets", "Music")
    pref = ["Another night.mp3", "Break time.mp3", "Coffee Break.mp3",
            "Menu_The-Shift.mp3", "Menu_Another-Night.mp3"]
    if MUSIC:
        return MUSIC if os.path.isabs(MUSIC) else os.path.join(d, MUSIC)
    for n in pref:
        p = os.path.join(d, n)
        if os.path.exists(p):
            return p
    for f in sorted(os.listdir(d)):
        if f.lower().endswith((".mp3", ".flac", ".ogg", ".wav")):
            return os.path.join(d, f)
    return None


def capture(scene):
    """Launch the game once with the scene's env; collect dumped frames."""
    dst = os.path.join(WORK, scene["name"])
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst, exist_ok=True)
    rec = os.path.join(CWD, "shots", "rec")
    shutil.rmtree(rec, ignore_errors=True)

    env = dict(os.environ)
    env.update({"EI_PLAY": "1", "EI_RECDUMP": str(FPS), "EI_UPDATE": "off"})
    if scene.get("path"):
        env["EI_PATH"] = scene["path"]
    env.update(scene.get("env", {}))
    print(f"[{scene['name']}] capturing...")
    subprocess.run([EXE], cwd=CWD, env=env)

    frames = sorted(f for f in os.listdir(rec)) if os.path.isdir(rec) else []
    if not frames:
        sys.exit(f"[{scene['name']}] no frames dumped - is the build ok?")
    for f in frames:
        shutil.move(os.path.join(rec, f), os.path.join(dst, f))
    print(f"[{scene['name']}] {len(frames)} frames")
    return len(frames)


def encode_clip(src_dir, n_frames, out_mp4, fade=True):
    dur = n_frames / FPS
    vf = f"scale={W}:{H}:flags=lanczos,format=yuv420p"
    if fade:
        fo = max(0.0, dur - 0.6)
        vf += f",fade=t=in:st=0:d=0.5,fade=t=out:st={fo:.2f}:d=0.6"
    run(["ffmpeg", "-y", "-framerate", str(FPS),
         "-i", os.path.join(src_dir, "f_%05d.png"),
         "-vf", vf, "-c:v", "libx264", "-preset", "medium", "-crf", "18",
         out_mp4])


def make_card(title, sub, dur, out_mp4):
    font = FONT.replace("\\", "/").replace(":", "\\:")
    vf = (f"drawtext=fontfile='{font}':text='{title}':"
          f"fontcolor=0xD8D3C8:fontsize=64:"
          f"x=(w-text_w)/2:y=(h/2)-70,"
          f"drawtext=fontfile='{font}':text='{sub}':"
          f"fontcolor=0x8A94A6:fontsize=30:"
          f"x=(w-text_w)/2:y=(h/2)+20,"
          f"fade=t=in:st=0:d=0.6,fade=t=out:st={dur-0.7:.2f}:d=0.7,"
          f"format=yuv420p")
    run(["ffmpeg", "-y", "-f", "lavfi",
         "-i", f"color=c=0x06070a:s={W}x{H}:r={FPS}:d={dur}",
         "-vf", vf, "-c:v", "libx264", "-preset", "medium", "-crf", "18",
         out_mp4])


def assemble(clips, music):
    lst = os.path.join(WORK, "concat.txt")
    with open(lst, "w") as f:
        for c in clips:
            f.write(f"file '{c.replace(chr(92), '/')}'\n")
    tmp = os.path.join(WORK, "video.mp4")
    run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", lst,
         "-c", "copy", tmp])
    dur = float(subprocess.check_output(
        ["ffprobe", "-v", "quiet", "-show_entries", "format=duration",
         "-of", "csv=p=0", tmp]).strip())
    af = f"afade=t=out:st={max(0.0, dur - 2.5):.2f}:d=2.5,volume=0.85"
    run(["ffmpeg", "-y", "-i", tmp, "-i", music,
         "-map", "0:v", "-map", "1:a", "-c:v", "copy", "-c:a", "aac",
         "-b:a", "192k", "-af", af, "-shortest", OUT])
    print(f"\ntrailer -> {OUT}  ({dur:.1f}s)")


def main():
    only = None
    keep = "--keep" in sys.argv
    if "--scene" in sys.argv:
        only = sys.argv[sys.argv.index("--scene") + 1]
    os.makedirs(WORK, exist_ok=True)
    music = pick_music()

    clips = []
    for title, sub, dur in CARDS[:1]:
        c = os.path.join(WORK, "card_open.mp4")
        make_card(title, sub, dur, c)
        clips.append(c)

    for s in SCENES:
        if only and s["name"] != only:
            continue
        n = capture(s)
        clip = os.path.join(WORK, s["name"] + ".mp4")
        trim = s.get("trim")
        if trim:
            # keep only [t0,t1) seconds of the capture - deterministic at
            # the fixed FPS, so gameplay beats land exactly where scripted
            lo, hi = int(trim[0] * FPS), int(trim[1] * FPS)
            keep = [f"f_{i:05d}.png" for i in range(lo + 1, min(hi, n - 1) + 1)]
            tdir = os.path.join(WORK, s["name"] + "_trim")
            os.makedirs(tdir, exist_ok=True)
            for i, f in enumerate(keep, 1):
                shutil.copy2(os.path.join(WORK, s["name"], f),
                             os.path.join(tdir, f"f_{i:05d}.png"))
            encode_clip(tdir, len(keep), clip)
        else:
            encode_clip(os.path.join(WORK, s["name"]), n, clip)
        clips.append(clip)
        if only:
            break

    if not only:
        c = os.path.join(WORK, "card_title.mp4")
        make_card(*CARDS[1], c)
        clips.append(c)
        c = os.path.join(WORK, "card_end.mp4")
        make_card(*END_CARD, c)
        clips.append(c)

    if only:
        print(f"scene clip -> {clips[-1]}")
    else:
        if not music:
            print("no music found - video only")
        assemble(clips, music) if music else assemble_silent(clips)
    if not keep and not only:
        shutil.rmtree(os.path.join(WORK, "frames"), ignore_errors=True)


def assemble_silent(clips):
    lst = os.path.join(WORK, "concat.txt")
    with open(lst, "w") as f:
        for c in clips:
            f.write(f"file '{c.replace(chr(92), '/')}'\n")
    run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", lst,
         "-c", "copy", OUT])
    print(f"\ntrailer -> {OUT} (no audio)")


if __name__ == "__main__":
    main()
