#!/usr/bin/env python3
"""Write the calibration test signals for the dbx 119 comparison.

Every file is 48 kHz, 24-bit, stereo WAV. All levels are sine RMS in dBFS
(peak = RMS + 3.01 dB). Each file has a sync click (one sample at -6 dBFS
peak, both channels) at 0.25 s, and content starts at 0.5 s. Every other
timing in a file is measured from that 0.5 s point. manifest.json lists each
segment so that analyze.py does not hard-code any timing.

Usage:
    python make_signals.py [output_folder]     (default: signals/)
"""
import argparse
import json
import os

import numpy as np
import soundfile as sf

FS = 48000
CLICK_TIME_S = 0.25
CLICK_DBFS = -6.0            # peak level of the one-sample sync click
CONTENT_START_S = 0.5        # all content timings are shifted by this
SUBTYPE = "PCM_24"


def rms_db_to_peak(rms_db):
    """Peak amplitude of a sine with the given RMS level in dBFS."""
    return 10.0 ** (rms_db / 20.0) * np.sqrt(2.0)


class Track:
    """Stereo buffer plus the segment list written to the manifest."""

    def __init__(self, duration_s):
        self.n = int(round(duration_s * FS))
        self.data = np.zeros((self.n, 2))
        self.segments = []
        self.data[int(round(CLICK_TIME_S * FS)), :] = 10.0 ** (CLICK_DBFS / 20.0)

    @staticmethod
    def _idx(t):
        return int(round(t * FS))

    def add_sine(self, start_s, dur_s, freq_hz, rms_db, channels="LR", kind="tone", **extra):
        i0, i1 = self._idx(start_s), self._idx(start_s + dur_s)
        t = np.arange(i0, i1) / FS                       # absolute time: phase stays continuous
        y = rms_db_to_peak(rms_db) * np.sin(2.0 * np.pi * freq_hz * t)
        for c in channels:
            self.data[i0:i1, "LR".index(c)] += y
        self.segments.append(dict(kind=kind, start_s=start_s, end_s=start_s + dur_s,
                                  freq_hz=freq_hz, level_dbfs=rms_db,
                                  channels=channels, **extra))

    def add_sweep(self, start_s, dur_s, f1_hz, f2_hz, rms_db):
        """Exponential sine sweep (Farina form), scaled to the given RMS."""
        i0, i1 = self._idx(start_s), self._idx(start_s + dur_s)
        n = i1 - i0
        t = np.arange(n) / FS
        T = n / FS
        L = np.log(f2_hz / f1_hz)
        K = 2.0 * np.pi * f1_hz * T / L
        x = np.sin(K * (np.exp(t * L / T) - 1.0))
        x *= (10.0 ** (rms_db / 20.0)) / np.sqrt(np.mean(x ** 2))
        self.data[i0:i1, :] += x[:, None]
        self.segments.append(dict(kind="sweep", start_s=start_s, end_s=start_s + dur_s,
                                  f1_hz=f1_hz, f2_hz=f2_hz, level_dbfs=rms_db,
                                  channels="LR"))

    def finish(self):
        peak = np.max(np.abs(self.data))
        if peak >= 1.0:
            raise ValueError(f"signal clips (peak {peak:.3f})")
        return self.data


def build_alignment():
    tr = Track(CONTENT_START_S + 10.0)
    tr.add_sine(CONTENT_START_S, 10.0, 1000.0, -20.0, kind="tone")
    return tr


def build_static():
    levels = list(range(-70, -3, 2))                    # -70 ... -4 dBFS, 34 steps
    step_s, pre_s = 1.5, 0.5
    first = CONTENT_START_S + pre_s
    tr = Track(first + len(levels) * step_s)
    for k, lvl in enumerate(levels):
        tr.add_sine(first + k * step_s, step_s, 1000.0, float(lvl), kind="step")
    return tr


def build_attack_release():
    pairs = [(-40.0, -10.0), (-30.0, -20.0)]
    seg_s, cycles = 1.0, 6
    tr = Track(CONTENT_START_S + 2 * cycles * 2 * seg_s)
    t = CONTENT_START_S
    for pair_idx, (lo, hi) in enumerate(pairs):
        for cyc in range(cycles):
            for phase, lvl in (("low", lo), ("high", hi)):
                tr.add_sine(t, seg_s, 1000.0, lvl, kind="alt",
                            pair=pair_idx, cycle=cyc, phase=phase)
                t += seg_s
    return tr


def build_stereo_link():
    tr = Track(CONTENT_START_S + 10.0)
    tr.add_sine(CONTENT_START_S, 5.0, 1000.0, -10.0, channels="L", kind="tone", half="left_only")
    tr.add_sine(CONTENT_START_S + 5.0, 5.0, 1000.0, -10.0, channels="R", kind="tone", half="right_only")
    return tr


def build_freq_response():
    start = CONTENT_START_S + 1.0                       # 1 s silence before the sweep
    tr = Track(start + 10.0)
    tr.add_sweep(start, 10.0, 20.0, 20000.0, -20.0)
    return tr


def build_thd():
    tr = Track(CONTENT_START_S + 5.0)
    tr.add_sine(CONTENT_START_S, 5.0, 1000.0, -10.0, kind="tone")
    return tr


FILES = [
    ("00_alignment.wav", build_alignment),
    ("01_static_curve.wav", build_static),
    ("02_attack_release.wav", build_attack_release),
    ("03_stereo_link.wav", build_stereo_link),
    ("04_freq_response.wav", build_freq_response),
    ("05_thd.wav", build_thd),
]


def main():
    ap = argparse.ArgumentParser(description="Write the dbx 119 calibration signals.")
    ap.add_argument("outdir", nargs="?", default="signals",
                    help="output folder (default: signals/)")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    manifest = {
        "sample_rate": FS,
        "bit_depth": 24,
        "level_convention": "sine RMS in dBFS; peak = RMS + 3.01 dB",
        "click_time_s": CLICK_TIME_S,
        "click_dbfs": CLICK_DBFS,
        "content_start_s": CONTENT_START_S,
        "files": {},
    }
    for name, builder in FILES:
        tr = builder()
        data = tr.finish()
        path = os.path.join(args.outdir, name)
        sf.write(path, data, FS, subtype=SUBTYPE)
        manifest["files"][name] = {
            "duration_s": round(len(data) / FS, 6),
            "segments": tr.segments,
        }
        print(f"wrote {path}  ({len(data) / FS:.3f} s, {len(tr.segments)} segments)")

    mpath = os.path.join(args.outdir, "manifest.json")
    with open(mpath, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print(f"wrote {mpath}")


if __name__ == "__main__":
    main()
