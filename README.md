# IRene

An impulse-response (IR) loader plugin for guitar cabinet IRs, with three blendable IR slots and an IR browser built around favourites.

Formats: **VST3** (macOS, Windows), **AU** (macOS), **Standalone**.

> **Status: milestone 1 of 8 (skeleton).** The plugin currently passes audio through unchanged. It exists to prove that the build, signing and validation pipeline works. See [Roadmap](#roadmap).

## Download

All builds are made by GitHub Actions; you don't need any developer tools.

- **Releases (recommended):** open the [Releases page](https://github.com/anschae/irene/releases) and download `IRene-macOS.zip` or `IRene-Windows.zip` from the newest release.
- **Latest development build:** open the [Actions tab](https://github.com/anschae/irene/actions), click the newest run with a green tick, scroll down to **Artifacts** and download `IRene-macOS.zip` or `IRene-Windows.zip`. You need to be signed in to GitHub for this.

## Install

### macOS

1. Unzip. You get a folder `IRene-macOS` containing `IRene.vst3`, `IRene.component` and `IRene.app`.
2. Copy the plugins (in Finder, use **Go → Go to Folder…** and paste the path):
   - `IRene.vst3` → `~/Library/Audio/Plug-Ins/VST3`
   - `IRene.component` → `~/Library/Audio/Plug-Ins/Components`
3. The builds are not notarised by Apple, so macOS quarantines them after download. Remove the quarantine flag in **Terminal**:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/IRene.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/IRene.component
   ```

   For the standalone app, do the same with the path to `IRene.app`.

### Windows

1. Unzip. You get a folder `IRene-Windows` containing `IRene.vst3` (a folder) and `IRene.exe`.
2. Copy the whole `IRene.vst3` folder to `C:\Program Files\Common Files\VST3`.
3. `IRene.exe` is the standalone version and can live anywhere. No Visual C++ redistributable is needed.

## Make your DAW find the plugin

- **Cubase:** *Studio → VST Plug-in Manager*, click the gear icon (bottom left) and then **Rescan All**. IRene appears under the vendor *anschae*.
- **Logic Pro:** quit and reopen Logic; it validates new Audio Units on start. If IRene doesn't show up, open *Logic Pro → Settings → Plug-in Manager*, select IRene and click **Reset & Rescan Selection**. It is listed under *Audio Units → anschae*.

## Making a release

Push a tag that starts with `v`:

```sh
git tag v0.1.0
git push origin v0.1.0
```

GitHub Actions then builds everything, runs the checks and creates a GitHub Release with both zips attached. You can also create the tag on the GitHub website (*Releases → Draft a new release → Choose a tag → type the new tag name*); publishing that page pushes the tag and starts the build.

## Roadmap

1. **Skeleton**: build system, CI, pass-through plugin ← *done*
2. DSP core: slots, convolution, filter blocks, delay, mix, IR loading, project state
3. Basic UI
4. IR browser with favourites
5. Presets
6. Frequency response display
7. Export as single IR
8. Polish and release `v0.1.0`

## Design decisions

- **JUCE version:** pinned to `9.0.5`. The spec said JUCE 8, but that line now only receives security patches, while JUCE 9 has the same licence terms and system requirements. The tag is set in one place at the top of `CMakeLists.txt`.
- **Licence note:** JUCE is dual-licensed (AGPLv3 or a JUCE licence). That matters only if you distribute the plugin to others.
- **Plugin identity** (name, manufacturer, codes, bundle id, version) is defined once, at the top of `CMakeLists.txt`.
- **Number of IR slots:** the constant `kNumSlots` in `src/Config.h`.
- **CI runners are pinned** to known toolchains (`macos-15`, `windows-2022`, `ubuntu-24.04`) rather than `*-latest`, so a runner update can't break the build unannounced.
- **pluginval strictness is 10** (the maximum), above the required 5.
- **Validation on Linux too:** the Linux job builds the VST3 and runs pluginval without GUI tests, as a fast extra check. No Linux binaries are published.
- **macOS builds are ad-hoc signed, not notarised**, hence the quarantine step above.

## For developers

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE is downloaded automatically. On Linux, install the packages listed in `.github/workflows/build.yml`.
