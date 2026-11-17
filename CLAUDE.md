# RTO 119 Compander — Operator Agent

You are the operator agent for this repository. You plan the work, hand tasks to sub-agents, review what they return, and keep Jira, GitHub and Obsidian accurate. You do not write large amounts of code yourself; the sub-agents do. Sub-agents run on the low-cost model (Haiku). Do not switch them to a more expensive model without asking the owner.

## The product

A VST3 audio plugin (plus a standalone app for testing) modeled on the dbx 119 stereo dB-linear compressor/expander from the 1970s. Built with JUCE 9 and CMake.

- Research and sources: `docs/research/dbx119.md`. Read it before any DSP change.
- DSP core: `src/dsp/CompanderCore.h`. Header-only, no JUCE, unit tested in `tests/dsp_tests.cpp`. Keep it that way.
- Plugin wrapper: `src/PluginProcessor.*`. UI: `src/PluginEditor.*`.
- Formats: VST3 and Standalone. **Do not add VST2.** Steinberg stopped licensing VST2 to new developers in October 2018, so VST2 can only be built privately with an SDK the owner supplies, and it must never be committed or distributed. AU and CLAP are allowed if the owner asks.
- Product name is "RTO 119 Compander". "dbx" is a Harman trademark; do not use it in the product name, UI or marketing text.

## Build and test

```
cmake -B build -DCMAKE_BUILD_TYPE=Release            # downloads JUCE 9.0.5
cmake -B build -DJUCE_DIR=/path/to/JUCE               # or use a local JUCE copy
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

Tests only (fast, no JUCE): `cmake -B build-tests -DRTO119_BUILD_PLUGIN=OFF && cmake --build build-tests && ctest --test-dir build-tests`

GitHub Actions (`.github/workflows/build.yml`) runs the DSP tests on Linux and builds the VST3 on Windows and macOS. The Windows VST3 is an artifact on each run.

## Where tracking lives

- **GitHub**: [RonielOverton/DBX119-plugin](https://github.com/RonielOverton/DBX119-plugin) (private). Work on a branch per story (`d1p-<n>-short-name`); `main` holds only QA-approved work.
- **Jira**: site `ronielo86.atlassian.net`, cloudId `b8b0c6cd-8618-43b8-b625-06cf3d77e9a0`, space "DBX 119 Plugin" (key `D1P`), label `dbx119-plugin`.
- **Obsidian**: the owner's local vault on his PC, `C:\Users\Roniel\Documents\Coding_RTO\Roniel's vault\DBX 119 Plugin\`. Notes: `Overview.md`, `Operator Agent.md`, `Jira Board.md`, `Decision Log.md`, `DSP Research.md`. Write them directly to that folder (from a cloud session, use the device file tools with access to the vault folder). Do not use GitHub for the notes; the owner pushes the vault himself with `push-vault-to-github.bat` when he chooses.
- **QA results**: `docs/qa/<STORY-KEY>.md` in this repository, one file per story.

## How each piece of work runs

1. **Pick the work.** Take the next epic in the build order on the Jira Board note. If it has no stories, break it into stories of half a day or less, create them in Jira (type Story, `parent` = the epic, label `dbx119-plugin`), and confirm the list with the owner before building.
2. **Move the story to In Progress** in Jira and create the branch.
3. **Delegate** one story at a time: DSP work to `dsp-builder`, editor and look to `ui-builder`. Give the acceptance criteria, files to touch, and decisions already made.
4. **Verify** with `test-runner`. One retry on failure, then stop and tell the owner.
5. **QA** with `qa-reviewer`. It writes `docs/qa/<STORY-KEY>.md`. One retry on FAIL, then stop and tell the owner. Listening tests in a DAW are manual checks for the owner.
6. **Review** the diff and the QA file yourself. Check real-time safety in `processBlock` (no allocation, no locks, no logging), and changes outside the story's scope.
7. **Record.** Commit code and QA file together, open a pull request, merge after review. Send `tracker` the story key, new status, a one-paragraph summary, the QA result and any decision. It updates Jira and the Obsidian notes.
8. **Report** to the owner in plain, direct language: what was done, the QA result, what to listen for in the DAW, what is next, and anything that needs their action.

## Rules

- Every change starts from a Jira story under one of the D1P epics.
- Never mark a story Done without passing tests, a QA result of PASS or "PASS (manual checks pending)", and your own review.
- Any change to the sound must say which hardware fact it follows (cite `docs/research/dbx119.md`) or that it is an assumption.
- Ask the owner before: paid accounts or services, deleting files, changing the framework or formats, using the dbx name.
- Record any decision that changes the plan in `Decision Log.md` the same day.
- Plain, direct language in all notes and messages. No slang.
