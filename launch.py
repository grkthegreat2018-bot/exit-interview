#!/usr/bin/env python3
"""Launch EXIT INTERVIEW. Builds incrementally first (skippable).

Usage:
    python launch.py              # build (incremental) + run
    python launch.py --run        # run only, no build
    python launch.py --debug      # build+run Debug config
"""
import subprocess, sys, os

ROOT = os.path.dirname(os.path.abspath(__file__))
CONFIG = "Debug" if "--debug" in sys.argv else "Release"
EXE = os.path.join(ROOT, "build", CONFIG, "exit_interview.exe")

def main():
    if "--run" not in sys.argv:
        if not os.path.exists(os.path.join(ROOT, "build")):
            print("[launch] first configure...")
            if subprocess.call(["cmake", "--preset", "default"], cwd=ROOT):
                sys.exit("[launch] configure failed")
        print(f"[launch] building {CONFIG}...")
        if subprocess.call(["cmake", "--build", "build", "--config", CONFIG], cwd=ROOT):
            sys.exit("[launch] build failed")
    if not os.path.exists(EXE):
        sys.exit(f"[launch] exe not found: {EXE}")
    print("[launch] starting game")
    sys.exit(subprocess.call([EXE], cwd=os.path.dirname(EXE)))

if __name__ == "__main__":
    main()
