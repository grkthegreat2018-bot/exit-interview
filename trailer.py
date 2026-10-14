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

    # drift in on the desk monitor - the fake OS is a signature shot
    dict(name="monitor",
         env={},
         path="0,1.2,1.55,2.6,176,-8;5,0.72,1.28,1.95,184,-16"),

    # waiting room pan - benches, magazines, seated dummies (EI_NOVIS keeps
    # the real ambient cast out so the shot is fully controlled)
    dict(name="waiting",
         env={"EI_NOVIS": "1",
              "EI_NPC": "-2.7,0,-9.7,0,seated,female;"
                        "1.9,0,-8.45,0,seated,male,beard=2;"
                        "3.0,0,-8.45,0,seated,glasses,look"},
         path="0,-5.0,1.6,-6.6,118,-5;7,-1.6,1.6,-9.4,196,-4"),

    # a visitor waits in the open doorway; slow push toward them
    dict(name="visitor",
         env={"EI_DOOR": "1", "EI_NPC": "0.0,0,-5.5,0,look;0.4,0,-6.3,20,look"},
         path="0,1.3,1.55,-1.6,174,-2;6,0.9,1.5,-2.9,179,-3"),

    # the one "wrongness" beat - the desk floats where it should not
    dict(name="wrongness",
         env={"EI_ANOM": "desk_float"},
         path="0,0.4,1.6,-3.2,-2,-2;5.5,-0.3,1.45,-1.6,-7,-6"),

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
            "made with AI   -   github.com/GRKTheGreat/exit-interview", 4.0)


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
    env.update({"EI_PLAY": "1", "EI_PATH": scene["path"],
                "EI_RECDUMP": str(FPS), "EI_UPDATE": "off"})
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
