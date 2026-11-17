#!/usr/bin/env python3
"""Analyze a recording of one calibration signal (see README.md).

Works on hardware recordings and on renders from render_plugin.py. Timing
comes from manifest.json. The sync click aligns the output to the input.

Examples:
  python analyze.py --manifest signals/manifest.json \
      --input signals/01_static_curve.wav --output recorded/a_unity/01_static_curve.wav \
      --test static --label "hw a_unity"

  python analyze.py --manifest signals/manifest.json \
      --input signals/03_stereo_link.wav --output recorded/a_unity/03_stereo_link.wav \
      --test stereo_link --static-csv results/static_hw_a_unity.csv --label "hw a_unity"
"""
import argparse
import csv
import json
import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy import signal

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except ImportError:
    HAVE_MPL = False

TESTS = ["static", "attack_release", "stereo_link", "freq_response", "thd"]


class Report:
    """Collects printed lines so the same text can be written to --out-dir."""

    def __init__(self):
        self.lines = []

    def add(self, text=""):
        self.lines.append(text)
        print(text)


def db(power):
    return 10.0 * np.log10(np.maximum(power, 1e-20))


def seg_db(x, fs, start_s, end_s, frac=(0.2, 0.8)):
    """Level in dBFS (RMS) over the middle 60% of a segment, averaged over the channels in x."""
    a = int(round(start_s * fs))
    b = int(round(end_s * fs))
    dur = b - a
    m0 = a + int(round(frac[0] * dur))
    m1 = a + int(round(frac[1] * dur))
    return float(db(np.mean(x[m0:m1] ** 2)))


def find_latency(x_in, x_out, fs, manifest, rep):
    """Latency in samples (output lags input by this many samples).

    The search uses the pre-content region (0 to content_start). That region
    holds only the sync click, so the correlation is not disturbed by the test
    signal. Searching the first 1 s would include content that can be louder
    than the click.
    """
    n = int(round(manifest["content_start_s"] * fs))
    tmpl = x_in[:n, 0]
    seg = x_out[:n, 0] if len(x_out) >= n else np.pad(x_out[:, 0], (0, n - len(x_out)))
    c = signal.correlate(seg, tmpl, mode="full", method="fft")
    lag = int(np.argmax(np.abs(c))) - (n - 1)

    click_i = int(round(manifest["click_time_s"] * fs))
    out_i = click_i + lag
    rest = np.delete(seg, np.arange(max(0, out_i - 50), min(n, out_i + 50)))
    if 0 <= out_i < n and rest.size:
        ratio_db = 20.0 * np.log10(abs(seg[out_i]) / max(np.sqrt(np.mean(rest ** 2)), 1e-12))
    else:
        ratio_db = float("nan")
    rep.add(f"Sync click: found in output at sample {out_i} "
            f"(input {click_i}); latency {lag} samples = {1000.0 * lag / fs:.3f} ms")
    if not (ratio_db > 20.0):
        rep.add(f"WARNING: click is weak or not clearly found (click vs rest of pre-roll: "
                f"{ratio_db:.1f} dB). Check the recording starts before the click and the "
                f"click is not lost in noise.")
    return lag


def align(x_out, lat, n):
    """Output on the input timeline: out_aligned[i] = out[i + lat], zero padded or cut to n."""
    idx = np.arange(n) + lat
    ok = (idx >= 0) & (idx < len(x_out))
    res = np.zeros((n, x_out.shape[1]))
    res[ok] = x_out[idx[ok]]
    return res


def two_segment_fit(x, y, min_pts=3):
    """Two straight lines with a free break point; the break minimizes total squared error."""
    best = None
    for k in range(min_pts, len(x) - min_pts + 1):
        p1 = np.polyfit(x[:k], y[:k], 1)
        p2 = np.polyfit(x[k:], y[k:], 1)
        sse = np.sum((np.polyval(p1, x[:k]) - y[:k]) ** 2) + np.sum((np.polyval(p2, x[k:]) - y[k:]) ** 2)
        if best is None or sse < best[0]:
            best = (sse, k, p1, p2)
    _, k, p1, p2 = best
    knee = 0.5 * (x[k - 1] + x[k])
    return p1[0], p2[0], knee


def save_fig(fig, path, rep):
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)
    rep.add(f"Plot: {path}")


def run_static(x_in, x_out, fs, entry, rep, base):
    steps = sorted([s for s in entry["segments"] if s["kind"] == "step"], key=lambda s: s["start_s"])
    rows = []
    for s in steps:
        i_db = seg_db(x_in, fs, s["start_s"], s["end_s"])
        o_db = seg_db(x_out, fs, s["start_s"], s["end_s"])
        rows.append((i_db, o_db, o_db - i_db))
    rows = np.array(rows)
    x, y, g = rows[:, 0], rows[:, 1], rows[:, 2]

    with open(base + ".csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["input_db", "output_db", "gain_db"])
        for r in rows:
            w.writerow([f"{r[0]:.3f}", f"{r[1]:.3f}", f"{r[2]:.3f}"])
    rep.add(f"CSV: {base}.csv")

    rep.add("")
    rep.add("Input dBFS   Output dBFS   Gain dB")
    for r in rows:
        rep.add(f"{r[0]:9.2f}   {r[1]:10.2f}   {r[2]:7.2f}")

    overall = np.polyfit(x, y, 1)[0]
    s_lo, s_hi, knee = two_segment_fit(x, y)
    rep.add("")
    rep.add(f"Slope, all steps:          {overall:.4f}")
    rep.add(f"Slope below knee:          {s_lo:.4f}")
    rep.add(f"Slope above knee:          {s_hi:.4f}")
    rep.add(f"Estimated knee (input):    {knee:.2f} dBFS")
    rep.add("Note: for linear mode the two slopes should match and the knee has no meaning.")

    if HAVE_MPL:
        fig, ax = plt.subplots(figsize=(6, 6))
        ax.plot(x, y, "o-", label="output")
        lo, hi = min(x.min(), y.min()), max(x.max(), y.max())
        ax.plot([lo, hi], [lo, hi], "--", color="gray", label="1:1")
        ax.set_xlabel("input level (dBFS RMS)")
        ax.set_ylabel("output level (dBFS RMS)")
        ax.set_title("Static curve")
        ax.grid(True, alpha=0.3)
        ax.legend()
        save_fig(fig, base + ".png", rep)
    else:
        rep.add("matplotlib not installed: PNG skipped.")


def env_db(x, w):
    """Envelope in dB over non-overlapping windows of w samples (channel-averaged power)."""
    n = len(x) // w
    blocks = x[: n * w].reshape(n, w, x.shape[1])
    return db(np.mean(blocks ** 2, axis=(1, 2)))


def run_attack(x_in, x_out, fs, entry, rep, base):
    w = int(round(fs * 0.001))                       # 1 ms windows; 48 samples = 1 cycle of 1 kHz
    e_in = env_db(x_in, w)
    e_out = env_db(x_out, w)
    g = e_out - e_in                                  # gain envelope in dB, index = ms
    segs = sorted([s for s in entry["segments"] if s["kind"] == "alt"], key=lambda s: s["start_s"])

    rep.add("")
    rep.add("Change        t85 ms   gain before -> after (dB)   gain change   release rate (dB/s, first 50 ms, fit window ms, R2)")
    rows = []
    for prev, nxt in zip(segs[:-1], segs[1:]):
        if abs(nxt["level_dbfs"] - prev["level_dbfs"]) < 1e-9:
            continue
        t0 = int(round(nxt["start_s"] * 1000))
        dur_ms = int(round((nxt["end_s"] - nxt["start_s"]) * 1000))
        g_prev = g[t0 - 200:t0].mean()
        g_next = g[t0 + dur_ms - 200:t0 + dur_ms].mean()
        final = g_next - g_prev
        target = g_prev + 0.85 * final
        seg = g[t0:t0 + dur_ms]
        hit = np.nonzero(seg >= target)[0] if final > 0 else np.nonzero(seg <= target)[0]
        t85 = float(hit[0]) if hit.size else float("nan")

        drop = nxt["level_dbfs"] < prev["level_dbfs"]
        rate, win, r2 = float("nan"), 0, float("nan")
        if drop:
            ms = np.arange(0, 51, dtype=float)
            y = g[t0:t0 + 51]
            best = None
            for k in range(10, 51):
                tt, yy = ms[:k + 1], y[:k + 1]
                p = np.polyfit(tt, yy, 1)
                resid = yy - np.polyval(p, tt)
                ss_tot = np.sum((yy - yy.mean()) ** 2)
                r = 1.0 - np.sum(resid ** 2) / ss_tot if ss_tot > 0 else 0.0
                if r >= 0.98:
                    best = (p[0] * 1000.0, k, r)
            if best is not None:
                rate, win, r2 = best

        kind = "drop" if drop else "rise"
        rows.append(dict(kind=kind, frm=prev["level_dbfs"], to=nxt["level_dbfs"], t0=t0,
                         t85=t85, g_prev=g_prev, g_next=g_next, final=final, rate=rate,
                         win=win, r2=r2))
        rate_txt = f"{rate:8.1f}  (fit 0-{win} ms, R2 {r2:.3f})" if drop else "      -"
        rep.add(f"{kind:4s} {prev['level_dbfs']:6.1f}->{nxt['level_dbfs']:6.1f} dB at {t0/1000:6.3f}s "
                f"{t85:7.1f}   {g_prev:7.2f} -> {g_next:7.2f}   {final:8.2f}   {rate_txt}")

    if not rows:
        rep.add("No level changes found in the manifest for this file.")
    rep.add("")
    rep.add("Attack rows (rise): t85 is the time to reach 85% of the final gain change.")
    rep.add("Release rows (drop): t85 is the same measure; the release rate is the gain slope over the first 50 ms.")

    if HAVE_MPL:
        fig, axes = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
        tt = np.arange(len(e_in)) / 1000.0
        axes[0].plot(tt, e_in, label="input", alpha=0.7)
        axes[0].plot(tt, e_out, label="output", alpha=0.7)
        axes[0].set_ylabel("level (dBFS)")
        axes[0].legend()
        axes[1].plot(tt, g, color="C2")
        axes[1].set_ylabel("gain (dB)")
        axes[1].set_xlabel("time (s)")
        for r in rows:
            axes[1].axvline(r["t0"] / 1000.0, color="gray", alpha=0.3)
        axes[1].grid(True, alpha=0.3)
        axes[0].set_title("Attack and release: gain envelope (1 ms windows)")
        save_fig(fig, base + ".png", rep)
    else:
        rep.add("matplotlib not installed: PNG skipped.")


def load_static(path):
    with open(path, newline="", encoding="utf-8") as f:
        rows = [(float(r["input_db"]), float(r["gain_db"])) for r in csv.DictReader(f)]
    rows.sort()
    arr = np.array(rows)
    return arr[:, 0], arr[:, 1]


def run_stereo(x_in, x_out, fs, entry, rep, base, static_csv):
    segs = [s for s in entry["segments"] if s["kind"] == "tone" and s["channels"] in ("L", "R")]
    static = load_static(static_csv) if static_csv else None
    link_flags = []
    rep.add("")
    for s in segs:
        sig = 0 if s["channels"] == "L" else 1
        sil = 1 - sig
        i_db = seg_db(x_in[:, [sig]], fs, s["start_s"], s["end_s"])
        o_db = seg_db(x_out[:, [sig]], fs, s["start_s"], s["end_s"])
        gain = o_db - i_db
        sil_out = seg_db(x_out[:, [sil]], fs, s["start_s"], s["end_s"])
        sil_in = seg_db(x_in[:, [sil]], fs, s["start_s"], s["end_s"])
        name = "left only" if sig == 0 else "right only"
        rep.add(f"[{name}] signal channel: in {i_db:.2f} dBFS, out {o_db:.2f} dBFS, gain {gain:.2f} dB")
        rep.add(f"[{name}] silent channel: input {sil_in:.1f} dBFS, output {sil_out:.1f} dBFS "
                f"({'silent' if sil_out < -90 else 'NOT silent: crosstalk or boosted noise'})")
        if static is not None:
            g_ind = float(np.interp(i_db, static[0], static[1]))
            # Linked detector sums the inputs (s = 0.5 (L + R)): a signal on one channel
            # only reads 20 log10(0.5) = -6.02 dB below its own level.
            g_link = float(np.interp(i_db + 20.0 * np.log10(0.5), static[0], static[1]))
            d_ind, d_link = abs(gain - g_ind), abs(gain - g_link)
            rep.add(f"[{name}] static-curve gain at this level, independent detection: {g_ind:.2f} dB")
            rep.add(f"[{name}] static-curve gain at this level, linked (sum of both inputs, as in the hardware): {g_link:.2f} dB")
            if min(d_ind, d_link) < 0.5 and d_link < d_ind:
                verdict = "consistent with linked detection"
            elif min(d_ind, d_link) < 0.5 and d_ind < d_link:
                verdict = "consistent with independent detection"
            else:
                verdict = "inconclusive"
            rep.add(f"[{name}] gain verdict: {verdict}")
            link_flags.append(verdict)
        else:
            rep.add(f"[{name}] no --static-csv given: gain cannot be compared with the static curve.")
    if static is not None and link_flags:
        if all(v == "consistent with linked detection" for v in link_flags):
            rep.add("")
            rep.add("RESULT: linked")
        elif all(v == "consistent with independent detection" for v in link_flags):
            rep.add("")
            rep.add("RESULT: independent (not linked)")
        else:
            rep.add("")
            rep.add("RESULT: inconclusive")


def run_freq(x_in, x_out, fs, entry, rep, base):
    sw = [s for s in entry["segments"] if s["kind"] == "sweep"][0]
    a = int(round(sw["start_s"] * fs))
    b = int(round(sw["end_s"] * fs))
    sweep = x_in[a:b, 0]
    T = len(sweep) / fs
    L = np.log(sw["f2_hz"] / sw["f1_hz"])
    t = np.arange(len(sweep)) / fs
    inv = sweep[::-1] * np.exp(-t * L / T)            # Farina inverse filter

    nh = 2 ** 17                                       # 2.7 s, 0.37 Hz resolution
    freqs = np.fft.rfftfreq(nh, 1.0 / fs)
    centers = np.logspace(np.log10(20.0), np.log10(20000.0), 400)
    taper = np.ones(nh)
    m = nh // 4
    taper[-m:] = 0.5 * (1.0 + np.cos(np.pi * np.arange(m) / m))

    chan_db = []
    for ch in range(2):
        ir = signal.fftconvolve(x_out[:, ch], inv, mode="full")
        p = int(np.argmax(np.abs(ir)))
        h = ir[p:p + nh]
        if len(h) < nh:
            h = np.pad(h, (0, nh - len(h)))
        P = np.abs(np.fft.rfft(h * taper)) ** 2
        sm = np.empty(len(centers))
        for i, fc in enumerate(centers):
            mask = (freqs >= fc * 2 ** (-1 / 12)) & (freqs <= fc * 2 ** (1 / 12))
            sm[i] = np.mean(P[mask])
        chan_db.append(db(sm))
    resp = np.mean(chan_db, axis=0)
    ref_mask = (centers >= 200) & (centers <= 5000)
    resp = resp - np.mean(resp[ref_mask])

    rep.add("")
    rep.add("Response (1/6 octave smoothed, normalized to the 200 Hz to 5 kHz average):")
    for f_show in [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000]:
        i = int(np.argmin(np.abs(centers - f_show)))
        rep.add(f"  {centers[i]:8.1f} Hz  {resp[i]:+6.2f} dB")

    c = int(np.argmin(np.abs(centers - 1000.0)))
    below = np.nonzero(resp[:c + 1] < -1.0)[0]
    above = np.nonzero(resp[c:] < -1.0)[0]
    lo = f"{centers[below[-1]]:.1f} Hz" if below.size else "below 20 Hz (not reached)"
    hi = f"{centers[c + above[0]]:.1f} Hz" if above.size else "above 20 kHz (not reached)"
    rep.add("")
    rep.add(f"-1 dB points: low {lo}, high {hi}")

    with open(base + ".csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["freq_hz", "response_db"])
        for fc, r in zip(centers, resp):
            w.writerow([f"{fc:.2f}", f"{r:.3f}"])
    rep.add(f"CSV: {base}.csv")

    if HAVE_MPL:
        fig, ax = plt.subplots(figsize=(8, 4.5))
        ax.semilogx(centers, resp)
        ax.set_xlim(20, 20000)
        ax.set_ylim(-6, 3)
        ax.set_xlabel("frequency (Hz)")
        ax.set_ylabel("response (dB)")
        ax.set_title("Frequency response (output / input)")
        ax.grid(True, which="both", alpha=0.3)
        save_fig(fig, base + ".png", rep)
    else:
        rep.add("matplotlib not installed: PNG skipped.")


def thd_analyze(x, fs, f0, t0, dur=1.0):
    i0 = int(round(t0 * fs))
    n = int(round(dur * fs))
    seg = x[i0:i0 + n]
    w = signal.windows.blackmanharris(n, sym=False)
    S = np.fft.rfft(seg * w)
    P = np.abs(S) ** 2
    fr = np.fft.rfftfreq(n, 1.0 / fs)
    binhz = fs / n
    k0 = int(round(f0 / binhz))
    amp = 2.0 * np.abs(S[k0]) / np.sum(w)            # peak amplitude of the fundamental
    fund_p = P[k0 - 4:k0 + 5].sum()
    out = {"fund_dbfs": 20.0 * np.log10(amp / np.sqrt(2.0) + 1e-20)}
    harm = {}
    harm_p = 0.0
    for h in range(2, 6):
        kh = int(round(h * f0 / binhz))
        hp = P[kh - 4:kh + 5].sum()
        harm[h] = float(db(hp / fund_p))
        harm_p += hp
    band = (fr >= 20) & (fr <= 20000)
    band[k0 - 6:k0 + 7] = False
    noise_p = P[band].sum()
    out["thdn_db"] = float(db(noise_p / fund_p))
    out["thdn_pct"] = float(100.0 * np.sqrt(noise_p / fund_p))
    out["harm"] = harm
    out["thd_2to5_db"] = float(db(harm_p / fund_p))
    return out


def run_thd(x_in, x_out, fs, entry, rep, base):
    seg = [s for s in entry["segments"] if s["kind"] == "tone" and abs(s["freq_hz"] - 1000.0) < 1e-6][0]
    dur = seg["end_s"] - seg["start_s"]
    t0 = seg["start_s"] + (dur - 1.0) / 2.0
    f0 = seg["freq_hz"]
    rep.add("")
    rep.add(f"Window: 1 s, Blackman-Harris, starting {t0:.3f} s. Fundamental {f0:.0f} Hz.")
    for label, x in (("generated input (reference)", x_in), ("output", x_out)):
        for ch, name in ((0, "L"), (1, "R")):
            r = thd_analyze(x[:, ch], fs, f0, t0)
            rep.add(f"{label}, {name}: fundamental {r['fund_dbfs']:.2f} dBFS RMS")
            rep.add(f"  THD+N (20 Hz-20 kHz): {r['thdn_db']:.1f} dB = {r['thdn_pct']:.4f} %")
            rep.add(f"  THD (harmonics 2-5): {r['thd_2to5_db']:.1f} dB")
            for h in range(2, 6):
                rep.add(f"  harmonic {h} ({h * f0:.0f} Hz): {r['harm'][h]:.1f} dBc")


def main():
    ap = argparse.ArgumentParser(description="Analyze one calibration recording.")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--input", required=True, help="the calibration signal file (digital source)")
    ap.add_argument("--output", required=True, help="the recording or rendered file")
    ap.add_argument("--test", required=True, choices=TESTS)
    ap.add_argument("--label", default="", help="name for this run, used in output file names")
    ap.add_argument("--out-dir", default="results")
    ap.add_argument("--static-csv", default=None,
                    help="stereo_link only: CSV written by the static test of the same setting")
    args = ap.parse_args()

    with open(args.manifest, encoding="utf-8") as f:
        manifest = json.load(f)
    key = os.path.basename(args.input)
    if key not in manifest["files"]:
        sys.exit(f"{key} is not listed in {args.manifest}")
    entry = manifest["files"][key]

    for path in (args.input, args.output):
        if not os.path.isfile(path):
            sys.exit(f"File not found: {path}")
    x_in, fs = sf.read(args.input, dtype="float64", always_2d=True)
    x_out, fs_out = sf.read(args.output, dtype="float64", always_2d=True)
    if fs != manifest["sample_rate"] or fs_out != fs:
        sys.exit(f"Both files must be {manifest['sample_rate']} Hz (input {fs}, output {fs_out}). "
                 f"Resample or re-export in the DAW.")
    if x_in.shape[1] != 2:
        sys.exit("The input signal must be stereo.")

    rep = Report()
    rep.add(f"Test: {args.test}    Label: {args.label or '-'}")
    rep.add(f"Input : {args.input}")
    rep.add(f"Output: {args.output} ({len(x_out)} samples, {len(x_out) / fs:.3f} s)")
    if x_out.shape[1] == 1:
        rep.add("WARNING: output is mono; the same channel is used for both sides. "
                "Stereo results are not valid.")
        x_out = np.repeat(x_out, 2, axis=1)

    lat = find_latency(x_in, x_out, fs, manifest, rep)
    n = len(x_in)
    if len(x_out) - lat != n:
        rep.add(f"Length: output {len(x_out)} samples, input {n} samples after alignment "
                f"(output is {'cut' if len(x_out) - lat > n else 'padded with silence'} to match).")
    x_out = align(x_out, lat, n)

    slug = re.sub(r"[^A-Za-z0-9]+", "_", args.label or "run").strip("_").lower()
    os.makedirs(args.out_dir, exist_ok=True)
    base = os.path.join(args.out_dir, f"{args.test}_{slug}")

    if args.test == "static":
        run_static(x_in, x_out, fs, entry, rep, base)
    elif args.test == "attack_release":
        run_attack(x_in, x_out, fs, entry, rep, base)
    elif args.test == "stereo_link":
        run_stereo(x_in, x_out, fs, entry, rep, base, args.static_csv)
    elif args.test == "freq_response":
        run_freq(x_in, x_out, fs, entry, rep, base)
    elif args.test == "thd":
        run_thd(x_in, x_out, fs, entry, rep, base)

    with open(base + ".txt", "w", encoding="utf-8") as f:
        f.write("\n".join(rep.lines) + "\n")
    print(f"Report: {base}.txt")


if __name__ == "__main__":
    main()
