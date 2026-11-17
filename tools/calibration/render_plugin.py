#!/usr/bin/env python3
"""Render a calibration signal through the plugin DSP (no JUCE needed).

Compiles render_core.cpp once into tools/calibration/build/, converts the WAV
to raw float32, runs the DSP core, and writes a 24-bit WAV.

Example:
    python render_plugin.py --input signals/01_static_curve.wav \
        --output rendered/01_static_curve.wav --amount -0.5 --threshold -20 --mode 0

amount is the slope minus 1: -0.5 gives 2:1 compression, +0.5 gives 1:1.5 expansion.
Needs a C++17 compiler on the PATH (g++ by default; set CXX to use another).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC_DIR = os.path.join(REPO, "src")
CORE_HEADER = os.path.join(SRC_DIR, "dsp", "CompanderCore.h")
CPP = os.path.join(HERE, "render_core.cpp")
BUILD_DIR = os.path.join(HERE, "build")
EXE = os.path.join(BUILD_DIR, "render_core.exe" if os.name == "nt" else "render_core")


def build_if_needed():
    newest_src = max(os.path.getmtime(CPP), os.path.getmtime(CORE_HEADER))
    if os.path.exists(EXE) and os.path.getmtime(EXE) >= newest_src:
        return
    compiler = os.environ.get("CXX", "g++")
    if shutil.which(compiler) is None:
        sys.exit(f"Compiler '{compiler}' not found. Install a C++17 compiler (MinGW-w64 g++ on Windows) "
                 f"or set the CXX environment variable.")
    os.makedirs(BUILD_DIR, exist_ok=True)
    cmd = [compiler, "-std=c++17", "-O2", "-I", SRC_DIR, CPP, "-o", EXE]
    print("Compiling:", " ".join(cmd))
    if subprocess.run(cmd).returncode != 0:
        sys.exit("Compile failed.")


def main():
    ap = argparse.ArgumentParser(description="Render a calibration WAV through the plugin DSP.")
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--amount", type=float, default=0.0,
                    help="-1 to +1; slope = 1 + amount (default 0 = unity)")
    ap.add_argument("--threshold", type=float, default=-20.0, help="threshold in dBFS (default -20)")
    ap.add_argument("--mode", type=int, default=0, choices=[0, 1],
                    help="0 = linear, 1 = above threshold (default 0)")
    ap.add_argument("--output-db", dest="output_db", type=float, default=0.0,
                    help="output trim in dB (default 0)")
    ap.add_argument("--link", type=int, default=1, choices=[0, 1],
                    help="1 = stereo linked detection (default 1)")
    args = ap.parse_args()

    if not -1.0 <= args.amount <= 1.0:
        sys.exit("--amount must be between -1 and +1.")

    x, fs = sf.read(args.input, dtype="float32", always_2d=True)
    if x.shape[1] != 2:
        sys.exit("The input must be stereo.")

    build_if_needed()

    with tempfile.TemporaryDirectory() as tmp:
        raw_in = os.path.join(tmp, "in.raw")
        raw_out = os.path.join(tmp, "out.raw")
        x.astype("<f4").tofile(raw_in)
        cmd = [EXE, raw_in, raw_out, str(args.amount), str(args.threshold), str(args.mode),
               str(args.output_db), str(args.link), str(fs)]
        if subprocess.run(cmd).returncode != 0:
            sys.exit("render_core failed.")
        y = np.fromfile(raw_out, dtype="<f4").reshape(-1, 2)

    if len(y) != len(x):
        sys.exit(f"Length mismatch: input {len(x)}, rendered {len(y)}.")

    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)
    sf.write(args.output, y.astype(np.float64), fs, subtype="PCM_24")
    slope = 1.0 + args.amount
    mode_name = "linear" if args.mode == 0 else "above threshold"
    print(f"Rendered {args.input} -> {args.output}")
    print(f"slope {slope:.3f}, threshold {args.threshold} dBFS, mode {mode_name}, "
          f"output {args.output_db} dB, link {args.link}, {fs} Hz")


if __name__ == "__main__":
    main()
