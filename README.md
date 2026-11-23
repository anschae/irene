# IRene

An impulse-response (IR) loader plugin for guitar cabinet IRs, with three blendable IR slots and an IR browser built around favourites.

Formats: **VST3** (macOS, Windows), **AU** (macOS), **Standalone**.

> **Status: version 0.3.0.** Version 0.1.0 plus automatic time-alignment, step 1 (browser and workflow) and step 2 (levels, five slots, A/B) — see [Roadmap](#roadmap). The builds for macOS (universal VST3, AU, Standalone) and Windows (VST3, Standalone) pass their automated checks, including pluginval and Apple's auval. Version 0.1.0 has been used in Cubase; the features of steps 1 and 2 have not been tried in a DAW yet (see [Known gaps](#known-gaps)).

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

A release is made by creating a tag that starts with `v` (for example `v0.1.0`). GitHub Actions then builds everything, runs the checks and attaches `IRene-macOS.zip` and `IRene-Windows.zip` to the release. This takes about 20 minutes.

**On the GitHub website (no tools needed):**

1. Open the repository and click **Releases** (right-hand side), then **Draft a new release**.
2. Click **Choose a tag**, type the new tag name, e.g. `v0.1.0`, and click **Create new tag: v0.1.0 on publish**.
3. Give it a title, e.g. `IRene v0.1.0`, and click **Publish release**.
4. Wait for the run in the **Actions** tab to finish. The two zips then appear under the release's **Assets**.

**With git:**

```sh
git tag v0.1.0
git push origin v0.1.0
```

To change the version number shown in the plugin, edit `IRENE_VERSION` at the top of `CMakeLists.txt` before tagging.

## Roadmap

1. **Skeleton**: build system, CI, pass-through plugin ← *done*
2. **DSP core**: slots, convolution, filter blocks, delay, mix, IR loading, project state ← *done*
3. **Basic UI**: slot strips, filter editor, meters, settings page, scaling ← *done*
4. **IR browser**: favourite folders, favourites, recent, search, instant auditioning ← *done*
5. **Presets**: save, save as, rename, delete, prev/next, Init, portable presets, missing-file handling ← *done*
6. **Frequency response display**: combined wet-path response, per-slot curves, channel and smoothing options ← *done* (draggable EQ handles on the display, a stretch goal, are not implemented)
7. **Export as single IR**, with the equivalence test ← *done*
8. **Polish**: tooltips, README, thread-safety hardening, release `v0.1.0` ← *done*
9. **Automatic time-alignment of slots** (added after 0.1.0) ← *done*

Agreed with the owner after 0.1.0, in steps that are each released and tried in a DAW before the next:

10. **Step 1, browser and workflow** (version 0.2.0): favourite-folder display names, right-click menus, resizable browser and columns (remembered), "back to the previous IR" per slot, "Solo target" while browsing, notepad, mono export by default ← *done*
11. **Step 2, levels and slots** (version 0.3.0): dry signal when no IR is loaded, auto gain for the slot blend, 5 slots (3 shown, "+" for more), A/B comparison with loudness matching, lower CPU on mono tracks ← *done*
12. **Step 3, EQ and display:** more EQ bands, draggable EQ points with FabFilter Pro-Q-style mouse and key controls, a band switches on when its gain is changed, waveform view of the slots, "align as heard"
13. **Step 4, extras:** tags and ratings in the browser, minimum-phase option per slot

## Design decisions

- **JUCE version:** pinned to `9.0.5`. The spec said JUCE 8, but that line now only receives security patches, while JUCE 9 has the same licence terms and system requirements. The tag is set in one place at the top of `CMakeLists.txt`.
- **Licence note:** JUCE is dual-licensed (AGPLv3 or a JUCE licence). That matters only if you distribute the plugin to others.
- **Plugin identity** (name, manufacturer, codes, bundle id, version) is defined once, at the top of `CMakeLists.txt`.
- **Number of IR slots:** the constant `kNumSlots` in `src/Config.h` (5 since 0.3; `kDefaultVisibleSlots` = 3 are shown in a new instance).
- **Parameter version hints:** everything that existed in 0.1/0.2 has hint 1; slots 4 and 5 and `autogain` (0.3) have hint 2 (`versionHintFor` in `Parameters.cpp`). New parameters are added after all existing ones, so no parameter changes position.
- **CI runners are pinned** to known toolchains (`macos-15`, `windows-2022`, `ubuntu-24.04`) rather than `*-latest`, so a runner update can't break the build unannounced.
- **pluginval strictness is 10** (the maximum), above the required 5.
- **dB parameters snap by rounding the value** to 0.01 dB rather than using JUCE's range interval, so that 0 dB is exactly 0 dB on every CPU (on Apple Silicon the interval method gave −0.0000013 dB as the default).
- **Validation on Linux too:** the Linux job builds the VST3 and runs pluginval without GUI tests, as a fast extra check. No Linux binaries are published.
- **macOS builds are ad-hoc signed, not notarised**, hence the quarantine step above.

### User interface

- **Scaling.** The whole window scales between 75 % and 200 %, either from the settings page or by dragging the bottom-right corner. The size is a global setting.
- **Global settings** (favourite folders and their display names, presets folder, loading defaults, UI scale, browser width and columns) live in one XML file in IRene's settings folder and are shared by all instances and DAWs: macOS `~/Library/Application Support/IRene/`, Windows `%APPDATA%\IRene\`.
- **Dialogs and menus open inside the plugin window** rather than as separate system windows, because those behave differently from host to host.
- **Each slot has Norm and Trim buttons** in addition to the controls in the spec, to switch loudness normalisation and auto-trim for the IR in that slot.
- **The selected filter tab and the browser's target slot** are saved with the project (they are not automatable parameters).
- **Clicking anywhere in a slot strip**, including on its controls, makes that slot the browser's target.
- **More slots make the window wider.** The base width is computed from `kNumSlots`; the code was checked by building and testing with five slots.
- **Instances may be created on any thread.** Settings, library and slot notifications use a small thread-safe broadcaster instead of `juce::ChangeBroadcaster`, because some hosts create and destroy plugins off the main thread.
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

- **Display names for favourite folders.** Right-click a favourite folder in the browser (or double-click it in Settings) and choose **Rename...** to show it under another name, e.g. "Mesa 4x12" instead of "48". Only IRene's display changes; the folder on disk keeps its name. The name is stored in the global settings with the folder's path and is forgotten when the folder is removed from the list. An empty name, or the folder's own name, means "no custom name". (An older IRene version ignores the names and drops them when it saves its settings.)
- **Right-click menus.** On a favourite folder: Rename, use the folder's own name, open the folder in Finder/Explorer, rescan. On a file: load it into any slot (not just the target slot), add or remove it as a favourite, show it in Finder/Explorer. On a subfolder: open it in Finder/Explorer. Menus open inside the plugin window like all other menus.
- **Browser width.** Drag the thin strip between the browser and the frequency display. The window gets wider or narrower; the rest of the layout and the UI scale stay as they are. Double-click the strip for the default width (300 at 100 %). Range 250 to 700.
- **Columns.** Drag the borders in the list header to change column widths; right-click the header to show or hide Ch, Rate and Length (Name and the star column are always shown). The columns still fill the browser's width: widening one makes the columns to its right narrower, so for long names widen the browser itself.
- **Browser width and columns are global settings.** Every new plugin window (in any DAW) opens with the width and columns used last. A window that is already open keeps its own layout until it is closed, so that changing one instance never resizes another.
- **"Solo target"** (button at the bottom of the browser). While it is on, only the browser's target slot is heard, even if that slot is muted, so you can judge the IR you are auditioning on its own; the other slots are dimmed and marked "Not heard". It follows the target slot when you click another slot. It is not a parameter and isn't saved: it turns off when the plugin window closes or the browser is hidden, so the plugin never plays a single slot with nothing on screen to show it. The frequency display shows what you hear; "Export as single IR" ignores it.

### Slots 4 and 5

- **Five slots, three shown.** A new instance shows three slots. The **+** column after the last slot adds slot 4, then slot 5; the window gets wider, as it does when the browser opens. With four or five slots the strips are narrower (250 instead of 315 at 100 %) but keep all their controls. At 100 % with the browser open, five slots make the window about 1750 pixels wide; use the 75 % UI scale on a small screen.
- **Hiding a slot:** the last slot gets a small **×** when it is empty and more than three are shown. Hiding it puts its settings (gain, EQ, mute, solo...) back to their defaults, so a hidden slot never affects the sound. A slot with an IR can't be hidden, and a slot that holds an IR is always shown (e.g. when a project or preset uses slot 5).
- **The number of shown slots is saved** with the project and with presets. "Init" goes back to three. Projects and presets from before 0.3 open with three.
- **All five slots always exist as parameters,** so the DAW's automation list always shows five slots. That is what keeps automation and old projects safe. Hidden slots are empty and cost no CPU.
- **Filter tabs, the Swap menu and "Load into Slot..."** only list the shown slots.

### Auto gain

- **What it does:** keeps the blend about as loud as its loudest slot on its own. Two equally loud slots that add up to +6 dB are turned down by 6 dB; soloing one of them then sounds just as loud as the blend. It replaces the extra loudness that comes from adding slots together, nothing else: one slot on its own is never changed, so the slot Gain knobs still do what they say.
- **It only ever turns down** (by up to 24 dB). Slots that partly cancel each other (out of phase, misaligned) are not turned up, so you still hear when a blend loses level through cancellation.
- **How it measures:** the same loudness measure as IR normalisation (pink-noise weighted, 80 Hz – 8 kHz), worked out from the slot IRs with their gain, pan, delay, polarity and filter block; on a stereo track, left and right are averaged. Input/output filter blocks, mix and output gain don't affect it.
- **The button** sits in the right-hand column (Auto gain); below it, the correction applied at the moment. It is a parameter (ID `autogain`), saved with projects and presets and automatable.
- **On in new instances, off in old projects:** projects and presets from before 0.3 open with Auto gain off, so they sound exactly as before.
- **Timing:** the correction is worked out on a background thread when something changes and is smoothed over 80 ms, so a level change while you turn a knob follows within a few hundredths of a second. During an offline render (export/bounce) it is worked out before every block instead, so automation is followed exactly.
- **Export and display include it**, since it is part of the sound.

### A/B comparison

- **A and B are two complete setups:** all parameters, the slot IRs (with their Norm/Trim options) and the number of shown slots. The buttons **A** and **B** in the header switch between them; the first switch to B makes B a copy of A. **A > B** (or **B > A**) copies the setup you are hearing over the other one.
- **Switching** changes the parameters at once (the DAW sees them like a preset load) and crossfades the IRs in, as when loading an IR. The "back" history of the slots starts afresh.
- **Match** plays B at A's loudness, so you compare the sound and not the level. A is the reference and is never changed; B gets a correction gain (shown below Auto gain in the right-hand column, e.g. "B −2.5 dB"), up to ±24 dB. A's loudness is measured when you switch from A to B. The match gain is not part of B's settings and is never exported.
- **Saved with the project:** which setup is playing, the other setup (with its IR audio), Match and A's measured loudness. Presets only contain the setup that is playing; loading one replaces it and leaves the other alone.

### Back to the previous IR

- **The ↶ button on each slot** (left of < and >) puts back the IR that was in the slot before. Click it again to go further back. It undoes loading a file (from the browser, prev/next, drag and drop, the file dialog or the right-click menu) and **Clear**. Going back far enough empties the slot again if it started empty.
- **The IR comes back with the Norm and Trim settings it had.** Knob settings are not part of the history (use the DAW's undo for those).
- **Up to 25 steps per slot.** Loading the IR that is already in the slot, or a file that fails to load, adds no step. **Swap** moves the history along with the IR.
- **The history belongs to the open plugin, not to the project.** It is not saved; loading a project, a preset or "Init" starts a fresh history.

### Notes

- **Notes button** (top right): a notepad for free text about the sound, e.g. which cab and mics or what it's used for. The text is saved with the DAW project as you type. Presets don't store it, and loading a preset keeps the project's notes. The button shows a dot when there are notes.

### Automatic time-alignment (the Align button)

- **What it does:** click **Align** on a slot and every other loaded slot is lined up with it. IRene compares the IRs by cross-correlation, sets the **Delay** of each slot so that all of them arrive together, and flips a slot's polarity (Ø) where that matches the reference better. A dialog lists what was set.
- **Delays are only ever added.** The slot whose sound arrives last gets 0 ms and the others are delayed to meet it, so the result is the same whichever slot you click. The slot you click decides only the polarity: it keeps its own, and the others follow it.
- **It replaces the Delay values** of the slots it aligns, including any you set by hand.
- **Precision:** offsets are found to a fraction of a sample and set in steps of 0.001 ms. In the unit test, three copies of one IR at different distances (one with flipped polarity) sum to within about −69 dB of the ideal after Align.
- **Based on the IRs as played:** after resampling, auto-trim and the length setting, but before the slot's filter block. Only the first 0.5 seconds of each IR are compared.
- **Unrelated IRs are left alone.** If a slot's IR has less than a 20 % match with the reference (for example a different cabinet), its delay and polarity are not changed, because any offset would be a guess. The dialog says so. This threshold is a judgement that has not yet been checked against real multi-mic IR packs.
- **A polarity flip needs a clear win:** the flipped fit has to be at least 10 % stronger than the best non-flipped one.
- **Limits:** slots more than 10 ms apart can't be aligned (the Delay control's range); nothing is changed then. Align needs the audio engine to have been started once by the DAW.
- **Parameter changes are reported to the host** like knob movements, so the DAW's own undo can take them back where the host supports that.

### Frequency display

- **What it shows:** the magnitude response of the wet path. Each slot's IR (as played: resampled, trimmed, normalised) is combined with its filter block, delay, phase, gain and pan; the slots are added as complex values, so cancellation between slots is visible; auto gain is applied; then the input and output filter blocks. The dry/wet mix, the output gain and the A/B match gain are not included. Each slot's own curve is its share of the blend (with auto gain).
- **It matches the audio:** a unit test measures the real engine with an impulse and requires the displayed curve to agree within 0.05 dB on left, right and mid.
- **Range:** 20 Hz to 20 kHz, +24 to −60 dB, 48 points per octave.
- **Smoothing** (1/6 or 1/3 octave) averages power and only affects the picture.
- **The dashed curve** is the filter block selected in the filter editor, on its own, relative to the 0 dB line.
- **Mute and solo are respected**, so the display shows what you hear. On a mono track the channel selector makes no difference.
- **Display options** (channel, smoothing, slot curves) are saved with the project.
- **Not implemented:** dragging EQ band handles directly on the display (listed as a stretch goal in the spec).

### Export as single IR

- **How it is rendered:** a separate, private copy of the audio engine is fed a single impulse, offline, on a background thread. It runs the same DSP code as the plugin, so the exported IR reproduces the wet path: in the unit test, noise through the plugin and the same noise convolved with the exported IR differ by about −90 dB (the requirement was −60 dB).
- **Mono export** is what the plugin produces on a mono track: pan is ignored and stereo IRs are summed.
- **Auto gain is included**, the A/B match gain and "Solo target" are not: the export is the setup that is playing, as it sounds.
- **Sample rate:** every IR is converted from its original file straight to the export rate, not via the session rate.
- **Auto length** ends the IR where the tail has dropped 90 dB below the peak, looking at up to 3 seconds beyond the longest IR (filters ring on), at most 12 seconds in total. A 5 ms fade-out is always applied.
- **24-bit files and overs:** if the IR peaks above 0 dBFS, a 24-bit export is refused with a message rather than written clipped. Use peak normalisation or 32-bit float.
- **Cancel** stops the render; nothing is written. Files are written via a temporary file.
- **Mono by default.** A new plugin instance exports mono (most guitar IR loaders expect mono IRs). Choose Stereo in the panel to keep left and right separate.
- **Export options** are remembered per plugin instance while it is open, not saved with the project.

### Presets

- **One file per preset** (`.irenepreset`) in the presets folder. The default folder is `Presets` inside IRene's settings folder; it can be changed in Settings.
- **A preset stores** all parameters and, for each slot, the IR's path, file name, content hash and its Norm/Trim options. It does not store the editor state (selected tab, target slot).
- **Portable presets** additionally contain the original IR audio. "Save" keeps a preset's portable choice; "Save as" asks.
- **"Init"** is built in: all parameters at their defaults and all slots empty. It is part of the prev/next cycle and can't be overwritten.
- **Missing IR files** are looked for at the stored path, then by content hash, then by file name (hash and name need the file to be in a favourite folder). If nothing is found the slot shows "missing" and its Load button becomes "Locate...".
- **The preset name is saved with the DAW project** as a label only. The project always restores its own embedded state, even if the preset file was changed or deleted since.
- **Deleting a preset** leaves the current settings untouched.

### Sound and signal flow

- **Nothing loaded means dry signal.** While no slot holds an IR, the input passes through as if the mix were at 0 % (input and output filter blocks and output gain still apply). Loading the first IR crossfades from the dry signal to the IR (60 ms); clearing the last one fades back. A slot whose file is missing counts as empty. Muting or soloing never brings the dry signal back: muted IRs stay silent.
- **Zero latency.** The convolution runs in JUCE's zero-latency mode (a 2048-sample low-latency head, larger FFT blocks for the rest of long IRs). The plugin reports 0 samples, so the dry signal needs no compensation delay.
- **Pan law.** Mono IRs use an equal-power pan that is unity gain in the centre (so +3 dB when panned hard to one side). Stereo IRs use a balance control that only attenuates the opposite side. On a mono output, pan is ignored and a stereo IR is summed to mono (L+R at −6 dB each).
- **Stereo input with a mono IR** is processed as dual mono (left and right each convolved with the IR), so a stereo guitar bus stays stereo.
- **Slot gain** runs from −60 dB to +12 dB; the bottom of the range is "Off" (silent).
- **IR length** is a percentage of the IR's full length (1–100 %), with a 5 ms fade-out when shortened. At 100 % the IR is untouched.
- **Slot delay** uses 3rd-order Lagrange interpolation, which needs no latency. Whole-sample delays are exact. Between samples there is a slight treble roll-off, worst at exactly half a sample: about −0.5 dB at 10 kHz and −2.5 dB at 15 kHz in a 48 kHz session.
- **Filters** are state-variable (TPT) filters: high/low-pass are Butterworth (12 or 24 dB/oct); EQ bands are bell or shelf. Parameter changes ramp over 30 ms; a peak/shelf switch passes through 0 dB.
- **Mono tracks use half the CPU per slot:** with a mono input and a mono IR, only one channel is convolved, delayed and filtered, and the result is used for both outputs (the result is bit-identical). Stereo inputs and stereo IRs use two channels. When a slot switches from mono to stereo processing (a stereo IR is loaded on a mono track), the right channel continues seamlessly from the left one; a mono IR that replaces a stereo IR keeps two channels running until the next IR, so the crossfade between them stays clean.

### Impulse responses

- **Resampling** uses a windowed-sinc converter (64 zero crossings, about 100 dB stop band) and always starts from the original file audio. IRs are scaled so that their frequency response, not their sample values, is preserved at the new rate.
- **Loudness normalisation** brings the pink-noise-weighted average level of the IR between 80 Hz and 8 kHz to 0 dB, i.e. a normalised IR passes band-limited pink noise at unity. The gain is baked into the IR that is played (so it crossfades together with the IR) and never touches the slot's gain control.
- **Normalise and auto-trim are stored per slot** when an IR is loaded, taken from the global defaults at that moment. Changing the defaults later doesn't alter existing projects.
- **Auto-trim** removes leading audio that is more than 60 dB below the IR's peak, keeping 2 samples of pre-roll. It is off by default.
- **Project state** embeds each IR's original audio as losslessly compressed 32-bit float, together with its path, name and content hash.
- **Content hash** is a 64-bit FNV-1a hash of the file's bytes plus the file size. It is meant to recognise moved or renamed files, not for security.

## Known gaps

- **Steps 1 and 2 not yet tried in a DAW.** Version 0.1.0 works in Cubase. New since then and untested in a host: right-click menus, dragging the browser's edge and adding/hiding slots (the plugin resizes its own window), "Open in Finder/Explorer", the Notes text field (keyboard focus), A/B switching (many parameters change at once, like a preset load) and Auto gain/Match by ear.
- **Auto gain and Match follow changes with a short delay** in real time (worked out on a background thread every 20 ms, then smoothed). While a new IR is being crossfaded in, the level can be briefly off.
- **A very wide window with five slots** (about 1750 px at 100 % with the browser). The 75 % scale helps on laptops.
- **Still to try from 0.1.0:** keyboard focus for arrow-key auditioning, drag and drop from Finder/Explorer, and the file dialogs depend on the host; they haven't been reported as a problem in Cubase.
- **Time-alignment has only been tested with synthetic IRs.** The maths is verified, but how well it judges real microphones (the 20 % match threshold in particular) needs trying with real multi-mic IR packs.
- **Draggable EQ handles on the display** are not implemented yet (planned for step 3).

## For developers

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE is downloaded automatically. On Linux, install the packages listed in `.github/workflows/build.yml`.
