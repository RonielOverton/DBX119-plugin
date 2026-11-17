# dbx 119 comparison kit

This kit records your real dbx 119 and compares it with the RTO 119 Compander plugin. It uses the same test signals for both, so the results can be compared directly.

Read all of this once before you start. Each step below is numbered.

## What you need

- An audio interface with at least one line output and one line input (two channels each is enough).
- A DAW (any, e.g. Reaper, Cubase, Ableton, Pro Tools).
- The dbx 119 with its power supply.
- Two unbalanced cables: RCA to TS (quarter-inch mono) cables, one per direction. The 119 uses RCA jacks, the interface uses TS jacks.
- A phone camera to photograph the front panel of the 119 for each setting.
- Python 3 (see "Install Python" at the end).

## Before you start: make the test signals

From the repository root, run once:

```
py tools\calibration\make_signals.py
```

This creates the `signals\` folder with six WAV files and `manifest.json`.

## Safety

1. Turn the interface output all the way down before you connect anything.
2. The 119 can add up to about 20 dB of gain in expansion mode. Keep headphone and speaker levels low when you listen to the 119 output.
3. Raise the interface output slowly, only after step 6 below.

## Wiring

1. Connect interface line output → dbx 119 RCA input (use RCA-to-TS cables).
2. Connect dbx 119 line output → interface line input (RCA-to-TS cables).
3. Turn off any processing on the interface (effects, auto-gain, "direct monitor" effects). The signal must go through unchanged.
4. Set the 119 to its normal line operation. Note the 119 is unbalanced at consumer level, about -10 dBV.

## Step A: Record the loopback (measure the interface)

The loopback shows what the interface alone does to a signal. You need it to correct the level.

1. Disconnect the 119. Connect interface line output → interface line input with one cable.
2. Create a DAW session at 48 kHz, 24-bit.
3. Import all six files from the `signals\` folder.
4. Play `00_alignment.wav` from a track. Record the interface input on a second track.
5. Export the recording as `00_alignment.wav` into a folder called `recorded/loopback/`. Do not trim, fade or normalize.
6. Repeat for `01_static_curve.wav` and `02_attack_release.wav` if you want the full check (optional).
7. Reconnect the 119 when done.

## Step B: Set the levels with 00_alignment.wav

The aim: the level through the interface is the same going in and out, and the 119 is not overloaded.

1. Play `00_alignment.wav` in loopback (step A). Open the recording in a level meter. It should read about -20 dBFS RMS. Adjust the interface input gain until it does. Do not let peaks reach 0 dBFS.
2. Check the loopback with the analyzer. This needs the loopback recording of `01_static_curve.wav` from step A (record it if you skipped it):
   ```
   py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\01_static_curve.wav --output recorded\loopback\01_static_curve.wav --test static --label loopback
   ```
   The gain column should read about 0.0 dB at every step. If it does not, adjust the interface input gain by the average offset and record the loopback again.
3. Connect the 119 again (wiring above).
4. Play `00_alignment.wav` through the 119 with the 119 set to unity (step C1). Watch the recorded level of the 119 output. If the signal distorts or the recording peaks near 0 dBFS, lower the interface output.
5. Once set, do not change the interface output or input gain again. Write the interface settings down, because every recording depends on them.

## Step C: Record the 119

### Settings to record

Set the 119 for each row, photograph the front panel, and fill in the knob values as printed on the panel. Use the same name for the folder.

| ID | Folder name | Compress/Expand knob (as printed) | THRESHOLD knob (as printed) | MODE | Files to record |
|---|---|---|---|---|---|
| a | `a_unity` | ______ (unity) | ______ | LINEAR | 01, 02, 03, 04, 05 |
| b | `b_max_comp_lin` | ______ (most compression) | ______ | LINEAR | 01, 02, 03 |
| c | `c_max_exp_lin` | ______ (most expansion) | ______ | LINEAR | 01, 02, 03 |
| d1 | `d1_mid_comp_lin` | ______ (middle compression) | ______ | LINEAR | 01, 02, 03 |
| d2 | `d2_mid_exp_lin` | ______ (middle expansion) | ______ | LINEAR | 01, 02, 03 |
| e | `e_max_comp_above` | ______ (most compression) | ______ (middle) | ABOVE THRESHOLD | 01, 02, 03 |
| f | `f_mid_exp_above` | ______ (middle expansion) | ______ (middle) | ABOVE THRESHOLD | 01, 02, 03 |

Notes:
- Setting `a` is the reference. Record 04 and 05 only at setting `a`.
- For setting `e` and `f`, set THRESHOLD to the middle of its range.
- Write down the knob values exactly as printed, and note anything you cannot read.

### Recording procedure (for each setting and each file)

1. Set the 119 to the setting for this folder. Photograph the panel.
2. In the DAW, put the signal file on track 1. Route track 1 output to the interface line output.
3. Create track 2. Set its input to the interface line input (left and right). Arm it for recording.
4. Start recording on track 2, then press play on track 1 from the very start of the file. Stop after the file ends, plus a second.
5. Do not add plugins, fades or normalization. Do not trim the start: the sync click at 0.25 s is used for alignment.
6. Export track 2 only: WAV, 48 kHz, 24-bit, stereo. Name the file exactly as the signal file (for example `01_static_curve.wav`).
7. Save it in `recorded/<folder name>/`. Create the folder first.

Repeat for every file in the table.

## Step D: Analyze the hardware recordings

Run these from the repository root in a Command Prompt or PowerShell window. Replace `a_unity` and the file names as needed.

Static curve (compression or expansion curve, slope and knee):
```
py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\01_static_curve.wav --output recorded\a_unity\01_static_curve.wav --test static --label "hw a_unity"
```

Attack and release:
```
py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\02_attack_release.wav --output recorded\a_unity\02_attack_release.wav --test attack_release --label "hw a_unity"
```

Stereo link (give the static CSV from the same setting; the file name is printed by the static run, for example `results\static_hw_a_unity.csv`):
```
py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\03_stereo_link.wav --output recorded\a_unity\03_stereo_link.wav --test stereo_link --static-csv results\static_hw_a_unity.csv --label "hw a_unity"
```

Frequency response (setting a only):
```
py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\04_freq_response.wav --output recorded\a_unity\04_freq_response.wav --test freq_response --label "hw a_unity"
```

THD (setting a only):
```
py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\05_thd.wav --output recorded\a_unity\05_thd.wav --test thd --label "hw a_unity"
```

Results go to `results\`: a text report (`.txt`), a CSV for the static and frequency tests, and a PNG plot when matplotlib is installed.

## Step E: Render and analyze the plugin

The plugin renders the same signal files through the same DSP. Map each setting to a plugin amount. Use the slope the static test measures, not the knob label, to match the plugin to the 119. The slope is `1 + amount`:

- amount -0.5 gives slope 0.5 (2:1 compression)
- amount +0.5 gives slope 1.5 (1:1.5 expansion)
- amount 0 gives slope 1 (unity)

Render for the same threshold and mode as the 119 setting (`--mode 0` is LINEAR, `--mode 1` is ABOVE THRESHOLD):

```
py tools\calibration\render_plugin.py --input signals\01_static_curve.wav --output rendered\lin_comp_2to1\01_static_curve.wav --amount -0.5 --threshold -20 --mode 0
py tools\calibration\analyze.py --manifest signals\manifest.json --input signals\01_static_curve.wav --output rendered\lin_comp_2to1\01_static_curve.wav --test static --label "plugin lin 2to1"
```

Repeat the render for files 02 and 03, then analyze them the same way as the hardware. Compare the printed slopes, attack and release times, and the stereo result with the hardware.

The render step needs a C++17 compiler. On Windows, install MinGW-w64 (g++), make sure `g++` is on the PATH, then run the command. The first run compiles `render_core` into `tools\calibration\build\`. You can set the `CXX` environment variable to use another compiler.

## Compare

For each setting, put the hardware and plugin static results side by side:

1. Slope below and above the knee, and the knee position (hardware and plugin).
2. Attack and release times at 85% (from the attack/release report).
3. Release rate in dB/s.
4. Frequency response -1 dB points (setting a only).
5. THD+N and harmonic levels (setting a only).
6. Stereo link result.

Record the differences in a note. Do not change the plugin yet. The results are for the calibration epic in Jira (D1P-13).

Expansion note: with strong expansion, the loud steps at the top of `01_static_curve.wav` can push the output past 0 dBFS. If the hardware recording or the plugin render clips there, ignore those steps, or render the plugin with `--output-db -6` and lower the 119 recording level to match.

## Send the results

Zip the `recorded\` folder together with the front-panel photos and the filled-in settings table, and add it to the Claude project (or a shared folder). The operator agent then runs the comparison and the calibration (Jira D1P-13).

## Install Python (Windows)

1. Install Python 3 from python.org. Tick "Add python.exe to PATH" in the installer.
2. Open PowerShell and check: `py --version`.
3. Install the packages:
   ```
   py -m pip install numpy scipy soundfile matplotlib
   ```
   matplotlib is optional. Without it the analyzer still prints all numbers and skips the PNG plots.

## Files in this kit

- `make_signals.py`: writes the test signals and `manifest.json`. Run it once before you start: `py tools\calibration\make_signals.py` (creates `signals\`).
- `analyze.py`: analyzes a recording or render.
- `render_plugin.py`: renders a signal through the plugin DSP.
- `render_core.cpp`: the small C++ program that `render_plugin.py` compiles.
