# RTO 119 Compander

A VST3 compressor/expander plugin inspired by the dbx 119, a 1970s stereo dB-linear compander. Built with JUCE 9 and CMake.

## Controls

| Control | What it does |
|---|---|
| Compress / Expand | One knob: infinite compression (left) → 1:1 (center) → 1:2 expansion (right) |
| Threshold | Level where gain is unity (-60 to 0 dBFS) |
| Mode | **Linear**: acts on the whole range. **Above Threshold**: acts only above the threshold (full left = limiter) |
| Output | Make-up gain, ±24 dB |
| Stereo Link | One detector for both channels (default on) |
| LED | Lights when the level is above the threshold |

## Build

Requires CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on macOS).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The VST3 is written to `build/RTO119_artefacts/Release/VST3/`. On Windows, copy `RTO 119 Compander.vst3` to `C:\Program Files\Common Files\VST3\`.

Every push to GitHub builds the Windows and macOS VST3 in GitHub Actions; download it from the run's Artifacts section.

## Tests

```
cmake -B build-tests -DRTO119_BUILD_PLUGIN=OFF
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Formats

VST3 and Standalone. VST2 is not supported: Steinberg stopped licensing VST2 to new developers in October 2018.

## Project

- How the work is run: `CLAUDE.md` (operator agent) and `.claude/agents/` (sub-agents)
- Hardware research and sources: `docs/research/dbx119.md`
- Tracking: Jira space `D1P` on ronielo86.atlassian.net; notes in the `DBX 119 Plugin` folder of the Obsidian vault

"dbx" is a trademark of Harman International. This project is not affiliated with or endorsed by dbx or Harman.
