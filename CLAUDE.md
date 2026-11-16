# IRene: notes for whoever works on this next

IRene is an impulse-response loader plugin (VST3, AU, Standalone) built with JUCE 9 and CMake. The owner is a guitarist, not a developer: he has no local build toolchain, uses Cubase (main) and Logic Pro, and gets all binaries from GitHub Actions. Explain things to him in plain language.

Read `README.md` first: its "Design decisions" and "Known gaps" sections record every choice that goes beyond the original spec. Add to them when you make a new one.

## Rules that must not be broken

- **Never rename a parameter ID.** DAW projects and automation depend on them. New parameters get a higher version hint. `tests/ProcessorTests.cpp` ("Parameter IDs are stable") has the full list written out by hand on purpose.
- **Nothing per-slot is written out by hand.** All slot code (DSP, parameters, UI, state, presets) loops over `kNumSlots` in `src/Config.h`. Check a change by building once with `kNumSlots = 5`.
- **Audio thread:** no allocation, locks, file I/O or logging. Try-locks that never block are fine. Smooth every continuous parameter.
- **State compatibility:** project state and presets carry version fields. Old projects must keep loading; unknown attributes in `library.xml` must be preserved on save.
- **Don't fake features.** If something isn't finished, say so in the README and to the owner.
- **Keep CI green.** Every change is pushed to `main` and must pass on Linux, macOS and Windows before it counts as done.

## Build, test, look at the UI (Linux)

```sh
sudo apt-get install -y ninja-build xvfb libasound2-dev libx11-dev libxext-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DIRENE_BUILD_TOOLS=ON
cmake --build build --target IReneTests IRene_VST3 IReneSnapshot
ctest --test-dir build --output-on-failure

# pluginval, including editor tests (download pluginval_Linux.zip from Tracktion/pluginval releases)
xvfb-run -a ./pluginval --strictness-level 10 --validate-in-process --validate build/IRene_artefacts/Release/VST3/IRene.vst3

# render the editor to PNGs without a DAW (folders become favourite folders, files go into slots)
xvfb-run -a build/IReneSnapshot_artefacts/Release/IReneSnapshot out/ some/ir/folder a.wav b.wav
```

- A Release build with LTO takes several minutes. For quick iteration use `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DIRENE_BUILD_PLUGIN=OFF`.
- Run the tests in a **Debug** build now and then: only Debug shows JUCE assertions.
- You can't run a DAW. Use the snapshot tool to look at layouts, and ask the owner to test in Cubase and Logic.

## Where things are

| Path | What |
| --- | --- |
| `CMakeLists.txt` | Plugin identity (name, codes, version, JUCE tag) in one block at the top |
| `src/params/` | `Settings.h`: plain parameter snapshots used by engine, export and display. `Parameters.cpp`: IDs and layout from one table |
| `src/dsp/` | `Svf.h`, `FilterBlock` (HP, 3 bands, LP), `FractionalDelay`, `SlotDsp` (convolution and per-slot chain), `Engine` (whole signal path, no JUCE processor dependency) |
| `src/ir/` | `IRSource` (original audio, immutable), `IRProcessing` (resample, normalise, trim), `SlotManager` (owns the IRs, background loading, state) |
| `src/library/` | `IRLibrary`: favourites, recent, scan index, background scanner (global, shared) |
| `src/app/` | `GlobalSettings`, `PresetManager`, `Broadcaster` |
| `src/analysis/` | `Response`: frequency response maths and the spectrum worker. `TimeAlign`: cross-correlation alignment of slots (pure functions; `IReneProcessor::alignSlots` applies the result) |
| `src/export/` | "Export as single IR" |
| `src/ui/`, `src/PluginEditor.*` | Editor. Everything sits in a fixed-size `Content` component that is scaled as a whole |
| `tests/` | JUCE `UnitTest`s, one file per area. `TestHelpers.h` has IR generators, reference convolution, WAV writing |
| `tools/Snapshot.cpp` | Editor-to-PNG renderer |

## Things that were learned the hard way

- **`juce::dsp::Convolution`:** `loadImpulseResponse` must be called in step with `process()`. That's why `SlotDsp` has a mailbox that the audio thread collects. `prepare()` installs a pending IR synchronously; in non-realtime mode `SlotDsp` does the same so offline renders are deterministic. Without an IR the convolution passes audio through, so an empty slot holds a silent 1-sample IR and then goes idle.
- **Export and display must agree with the engine.** Export runs a private `Engine` on an impulse. The display uses analytic responses (`FilterBlock::response`, `FractionalDelay::response`, `SlotDsp::computeGains`). If you change DSP, change those too; `ExportTests` and `ResponseTests` compare both against the real engine.
- **Float results differ between x86 and Apple Silicon.** The first macOS run failed because a parameter default came out as -1.3e-6 instead of 0. Don't test floats for exact equality unless the value is exact by construction; dB ranges round the value itself (see `dbRange` in `Parameters.cpp`).
- **Resampling pre-ringing:** an IR that starts at sample 0 has no room for the ringing before a sharp onset, so it is cut off. Tests that measure the resampler use IRs with leading silence.
- **Shared objects and threads:** `GlobalSettings`, `IRLibrary` and `SlotManager` use `irene::Broadcaster`, not `juce::ChangeBroadcaster`, because hosts may create and destroy instances off the message thread.
- **`AudioProcessorEditor` already has a `processor` member** and `AudioProcessor` already has `getParameters()`: don't shadow them (`plugin`, `getValueTreeState()` are used instead).
- **Popup menus** are parented to the scaled content component via `IReneLookAndFeel::getParentComponentForMenuOptions`; set `menu.setLookAndFeel (&getLookAndFeel())` on menus you create.
- **Tests must not touch the user's real settings:** `TestMain.cpp` redirects the data folder. Use paths from `juce::File` (not hard-coded `/...` strings) so tests also pass on Windows.
- **Piping the snapshot tool into `head`** kills it mid-write and leaves truncated PNGs.
- **An IR sampled at twice the rate is twice as loud as a filter.** Resampling preserves the filter response, so a test that builds "the same IR" at 96 kHz and 48 kHz must halve the 96 kHz sample values.
- **Reference signals in tests must be long enough.** A comparison once looked 35 dB worse than it was because the reference IR was cut off before a 110 Hz resonance had decayed.
- **Analytic test IRs** (`makeMicIR` in `tests/TimeAlignTests.cpp`) are defined in continuous time and can be sampled with any fractional delay or sample rate without interpolation error. Prefer them over noise when timing matters.

## CI and releases

- `.github/workflows/build.yml`: Linux (tests, pluginval), macOS (universal VST3/AU/Standalone, ad-hoc signing, pluginval, auval), Windows (VST3/Standalone, static runtime check, pluginval), then a packaging job. A full run takes about 20 minutes; macOS is the slowest.
- Watch a run with `gh run list --repo anschae/irene` and `gh run view <id> --repo anschae/irene --log-failed`.
- Claude sessions have so far been able to push to `main` but **not** to push tags or create releases. The owner makes releases on the GitHub website (steps in the README). The workflow attaches the zips to a release that already exists.
- The release upload step has never run yet (no tag has been pushed). If the first release has no zips attached, look there first.

## Status and backlog

Version 0.1.0 is feature complete and green in CI. Automatic time-alignment (the Align button on each slot) was added after it. Nothing has been used by a person in a DAW yet; expect bug reports about keyboard focus for arrow-key auditioning, drag and drop from Finder/Explorer, and file dialogs.

**Time-alignment, open points** (see "Automatic time-alignment" in the README for how it behaves):

- Only tested with synthetic IRs. The owner needs to try it on real multi-mic packs. The two judgement values are in `src/analysis/TimeAlign.h/.cpp`: `kMinMatch` (0.2, below which a slot is left alone) and `kInversionMargin` (1.1, how much better a flipped fit must be).
- It compares the IRs before the slot filter blocks. If the owner wants "align as heard", run each IR through its `FilterBlock` first.
- There is no undo of its own; it relies on the host seeing the parameter changes.

Ideas discussed with the owner, in the order suggested to him:

1. ~~Automatic time-alignment of slots~~ (done)
2. A/B comparison of two complete snapshots, optionally loudness-matched
3. Undo for auditioning ("back to the previous IR" per slot)
4. Draggable EQ band handles on the frequency display (the spec's stretch goal)
5. Tags and ratings in the browser (the library file format already preserves unknown attributes)
6. Mono CPU optimisation (the engine always processes in stereo)

Explicitly out of scope unless the owner asks: CLAP, true-stereo IRs, notarised macOS builds.
