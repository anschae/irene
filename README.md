# IRene

An impulse-response (IR) loader plugin for guitar cabinet IRs, with three blendable IR slots and an IR browser built around favourites.

Formats: **VST3** (macOS, Windows), **AU** (macOS), **Standalone**.

> **Status: milestone 4 of 8 (IR browser).** Loading, blending, filtering, the IR browser with favourites, and project recall are in place. Presets, the frequency display and export are still to come. See [Roadmap](#roadmap).

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
2. **DSP core**: slots, convolution, filter blocks, delay, mix, IR loading, project state ← *done*
3. **Basic UI**: slot strips, filter editor, meters, settings page, scaling ← *done*
4. **IR browser**: favourite folders, favourites, recent, search, instant auditioning ← *done*
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

### User interface

- **Scaling.** The whole window scales between 75 % and 200 %, either from the settings page or by dragging the bottom-right corner. The size is a global setting.
- **Global settings** (favourite folders, presets folder, loading defaults, UI scale) live in one XML file in IRene's settings folder and are shared by all instances and DAWs: macOS `~/Library/Application Support/IRene/`, Windows `%APPDATA%\IRene\`.
- **Dialogs and menus open inside the plugin window** rather than as separate system windows, because those behave differently from host to host.
- **Each slot has Norm and Trim buttons** in addition to the controls in the spec, to switch loudness normalisation and auto-trim for the IR in that slot.
- **The selected filter tab and the browser's target slot** are saved with the project (they are not automatable parameters).
- A developer tool (`-DIRENE_BUILD_TOOLS=ON`, target `IReneSnapshot`) renders the editor to PNG files without a DAW.

### IR browser and library

- **Favourites, recent IRs and the scan index are global.** They live in IRene's settings folder (`library.xml`, `index.bin`) and are shared by all instances in a DAW at once; changes made in another DAW show up within about 1.5 seconds.
- **IRs are identified by content hash plus last known path.** If a starred file is moved or renamed inside a favourite folder, the star follows it after the next scan. A *copy* of a starred file doesn't take the star away from the original.
- **The browser only shows folders that contain IRs**, and never leaves the favourite folders. Empty folders are hidden.
- **Search and "★ only" also look into subfolders**, so nothing is hidden one level down.
- **"Recent" is sorted by time** (newest first); all other views sort by name, with numbers in natural order.
- **Clicking a file loads it on mouse-up**, so a file can be dragged onto another slot without first loading it into the target slot.
- **Prev/next on a slot** steps through the browser list the IR was picked from. For an IR loaded another way (file dialog, drag from the desktop), it steps through the other IR files in the same folder. Both wrap around.
- **Scanning** runs on a background thread at low priority. Files whose size and modification time are unchanged are not opened again. Unreadable `.wav`/`.aiff` files are remembered and hidden.
- **Future-proof file format:** the library file has a version number, and attributes that this version doesn't know (such as tags or ratings written by a later version) are kept when the file is saved.

### Sound and signal flow

- **Nothing loaded means silence.** The mix defaults to 100 % wet and an empty slot outputs nothing, so a fresh instance is silent until an IR is loaded.
- **Zero latency.** The convolution runs in JUCE's zero-latency mode (a 2048-sample low-latency head, larger FFT blocks for the rest of long IRs). The plugin reports 0 samples, so the dry signal needs no compensation delay.
- **Pan law.** Mono IRs use an equal-power pan that is unity gain in the centre (so +3 dB when panned hard to one side). Stereo IRs use a balance control that only attenuates the opposite side. On a mono output, pan is ignored and a stereo IR is summed to mono (L+R at −6 dB each).
- **Stereo input with a mono IR** is processed as dual mono (left and right each convolved with the IR), so a stereo guitar bus stays stereo.
- **Slot gain** runs from −60 dB to +12 dB; the bottom of the range is "Off" (silent).
- **IR length** is a percentage of the IR's full length (1–100 %), with a 5 ms fade-out when shortened. At 100 % the IR is untouched.
- **Slot delay** uses 3rd-order Lagrange interpolation, which needs no latency. Whole-sample delays are exact. Between samples there is a slight treble roll-off, worst at exactly half a sample: about −0.5 dB at 10 kHz and −2.5 dB at 15 kHz in a 48 kHz session.
- **Filters** are state-variable (TPT) filters: high/low-pass are Butterworth (12 or 24 dB/oct); EQ bands are bell or shelf. Parameter changes ramp over 30 ms; a peak/shelf switch passes through 0 dB.
- **Everything runs in stereo internally**, also for a mono source with mono IRs. This costs some CPU that could be saved later.

### Impulse responses

- **Resampling** uses a windowed-sinc converter (64 zero crossings, about 100 dB stop band) and always starts from the original file audio. IRs are scaled so that their frequency response, not their sample values, is preserved at the new rate.
- **Loudness normalisation** brings the pink-noise-weighted average level of the IR between 80 Hz and 8 kHz to 0 dB, i.e. a normalised IR passes band-limited pink noise at unity. The gain is baked into the IR that is played (so it crossfades together with the IR) and never touches the slot's gain control.
- **Normalise and auto-trim are stored per slot** when an IR is loaded, taken from the global defaults at that moment. Changing the defaults later doesn't alter existing projects.
- **Auto-trim** removes leading audio that is more than 60 dB below the IR's peak, keeping 2 samples of pre-roll. It is off by default.
- **Project state** embeds each IR's original audio as losslessly compressed 32-bit float, together with its path, name and content hash.
- **Content hash** is a 64-bit FNV-1a hash of the file's bytes plus the file size. It is meant to recognise moved or renamed files, not for security.

## For developers

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE is downloaded automatically. On Linux, install the packages listed in `.github/workflows/build.yml`.
