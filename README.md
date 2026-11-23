<p align="center">
  <img src="src/logoNameColored.png" alt="NAMberry logo" width="420">
</p>

<p align="center">
  <a href="https://github.com/EliPoli64/namberry/releases/latest"><img src="https://img.shields.io/badge/download-latest%20release-2ea44f" alt="Download the latest release"></a>
  <a href="https://github.com/EliPoli64/namberry/actions/workflows/build.yml"><img src="https://github.com/EliPoli64/namberry/actions/workflows/build.yml/badge.svg?branch=master" alt="Build and release (main)"></a>
  <img src="https://img.shields.io/badge/version-0.1.0-blue" alt="Version 0.1.0">
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT">
  <img src="https://img.shields.io/badge/formats-VST3%20%7C%20Standalone-9cf" alt="Formats: VST3 and Standalone">
  <img src="https://img.shields.io/badge/platform-Linux%20x86__64-lightgrey" alt="Platform: Linux x86_64">
</p>

<p align="center">
  <b>Two NAM captures, two IRs, and the whole pedalboard in one stereo plugin.</b><br>
  amp · pedal/preamp · cabinet · EQ · modulation · delay · reverb · capo · tuner
</p>

<p align="center">
  <code>two NAM models</code> <code>two IRs + blend</code> <code>capo / drop tune</code>
  <code>strobe tuner</code> <code>acoustic sim</code> <code>impersonator</code>
  <code>3-band + 8-band EQ</code> <code>chorus / flanger / phaser</code>
  <code>delay</code> <code>room</code> <code>reverb</code> <code>presets</code>
</p>

<p align="center">
  <a href="https://github.com/EliPoli64/namberry/releases/latest"><b>Download</b></a> ·
  <a href="#install-the-vst3">Install</a> ·
  <a href="#building">Build from source</a> ·
  <a href="#signal-chain">Signal chain</a> ·
  <a href="#using-namberry">Manual</a> ·
  <a href="#known-limitations">Limitations</a>
</p>

# NAMberry

NAMberry is an all-in-one NAM-based guitar amp and effects plugin. It combines a NAM amp capture, a second NAM pedal/preamp capture, two blendable IRs with a single Blend knob, dynamics, a 3-band pre-EQ plus an 8-band graphical post-EQ, modulation, ping-pong/mono delay, room, reverb, and a low-latency pitch stage for capo and drop tuning.

The goal is to stop treating an amp capture as a single plugin in a long external chain. Load two captures, two cabinets, and a complete studio-style effects chain in one stereo plugin, with fast model browsing and a hardware-inspired interface.

> The pedal is not a generic waveshaper. It is a **second NAM model**, loaded independently from the amp.

**Current version:** `0.1.0` — prebuilt Linux Standalone and VST3 are on the [latest release](https://github.com/EliPoli64/namberry/releases/latest).

---

## Building

### Prerequisites

- CMake 3.22 or newer.
- A C++20 compiler.
- Git.
- JUCE 8.0.6 cloned into `JUCE/`.
- NeuralAudio cloned into `deps/NeuralAudio` with submodules.
- Signalsmith Stretch and Linear cloned into `deps/`.

```bash
git clone --depth 1 --branch 8.0.6 \
  https://github.com/juce-framework/JUCE.git JUCE

git clone --depth 1 --recurse-submodules \
  https://github.com/mikeoliphant/NeuralAudio.git deps/NeuralAudio

git clone --depth 1 \
  https://github.com/signalsmith-audio/signalsmith-stretch.git deps/signalsmith-stretch

git clone --depth 1 \
  https://github.com/signalsmith-audio/linear.git deps/signalsmith-linear
```

`JUCE/` and `deps/` are ignored by git; they must be present locally for a build.

#### Linux packages

On Fedora:

```bash
sudo dnf install cmake gcc-c++ git alsa-lib-devel \
  freetype-devel fontconfig-devel \
  libX11-devel libXext-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel libXcomposite-devel libXrender-devel \
  libXfixes-devel libXau-devel libXdmcp-devel libxcb-devel
```

On Debian/Ubuntu:

```bash
sudo apt install build-essential cmake git libasound2-dev \
  libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxcomposite-dev libxrender-dev \
  libxfixes-dev libxau-dev libxdmcp-dev libxcb1-dev
```

macOS needs the Xcode command-line tools. Windows needs Visual Studio 2022 and CMake.

> The helper scripts detect a local `/tmp/sysroot` used on machines without the Linux `-devel` packages. That directory is a local workaround, not part of the repository. On a fully provisioned machine the sysroot branch is skipped.

### Configure and build

Using presets:

```bash
cmake --preset standalone
cmake --build --preset standalone
```

```bash
cmake --preset vst3
cmake --build --preset vst3
```

Using scripts:

```bash
./scripts/build-standalone.sh
./scripts/build-vst3.sh
```

Add `--clean` to either script to wipe its build directory first.

Artifacts:

| Target | Path |
|---|---|
| Standalone | `build-standalone/NAMBerry_artefacts/Release/Standalone/NAMberry` |
| VST3 | `build-vst3/NAMBerry_artefacts/Release/VST3/NAMberry.vst3` |

The `build/` tree is used by the direct CMake/agent build and for the test targets:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target NAMBerry_VST3 NAMBerry_Standalone
```

The presets and helper scripts use the Unix Makefiles generator. On Windows, configure with the Visual Studio generator (or adjust the preset) instead.

### Install the VST3

Copy `NAMberry.vst3` to:

- Linux: `~/.vst3/`
- macOS: `~/Library/Audio/Plug-Ins/VST3/`
- Windows: `C:\Program Files\Common Files\VST3\`

Then rescan plugins in the DAW.

### Prebuilt releases (CI)

`.github/workflows/build.yml` builds both targets on every push to the mainline
branch (`main`, or `master` on the current remote) on Ubuntu, and refreshes the
rolling **[latest](https://github.com/EliPoli64/namberry/releases/latest)** GitHub
release with:

- `NAMberry-linux-standalone.zip` — unzip and run the `NAMberry` binary.
- `NAMberry-linux-vst3.zip` — copy `NAMberry.vst3` into `~/.vst3`.
- `SHA256SUMS.txt` — checksums.

The release is rebuilt in place, so the asset URLs stay stable. Pull requests and
feature branches do not publish anything.

---

## Why this exists

NAM captures sound excellent, but the surrounding workflow is usually fragmented:

- one plugin for the amp capture,
- another for a pedal capture,
- another for IRs,
- separate gate/compressor/EQ plugins,
- separate modulation/delay/reverb plugins,
- manual file browsing for every model,
- and no simple capo/drop-tune stage.

NAMberry packages all of that into one plugin:

- **Two NAM captures** — one amp, one pedal/preamp.
- **Two IRs and one Blend knob** — crossfade between two cabinets, or run just one, with a section bypass.
- **Impersonator pickup shaper** — a bridge/neck EQ preamp immediately after the tuner.
- **Acoustic simulator** — a steel-string acoustic voicing before the compressor that gives magnetic pickups low-string body and a plucked attack; engaging it switches the distortion, EQ and IR stages off automatically.
- **A complete chain** — tuner, pickup shaper, pitch stage, gate, acoustic sim, compressor, pedal, pre-amp EQ, amp, post-amp EQ, IR, post effects, room, master.
- **Room** — one knob adds short, audible stereo early reflections that make the guitar sound bigger when playing alone, separate from the Reverb effect.
- **Fast model management** — tap any filename screen to list its folder in place, a **LOAD** button for the system file browser, previous/next arrows, and drag-and-drop.
- **Persistent choices** — the selected amp, pedal and both IR files are restored on the next session.
- **Preset save/load** — store the complete parameter state together with the current amp, pedal and IR file paths.
- **Studio-quality pitch** — a real-time Rubber Band engine handles capo and drop tuning when the optional runtime library is available, with a self-contained Stretch fallback. Low-latency and Quality modes are selectable.
- **A usable tuner** — a horizontal strobe guide with cents deviation and an editable reference frequency (440, 432, etc.).
- **Stereo correctness** — the NAM stages are mono models, summed and copied to both channels; the dry path is always centred.

---

## Signal chain

The chain runs left to right through the five editor tabs:

```
Input
  → Input Gain (−24…+24 dB)
  → Tuner (detection only)
  → Impersonator (pickup shaper)
  → Pitch (capo / drop tune)
  → Noise Gate
  → Acoustic Sim
  → Compressor
  → NAM Pedal / Preamp
  → Pre-Amp EQ
  → NAM Amp
  → Post-Amp EQ
  → IR Loader (2 IRs + Blend)
  → Post FX (Chorus / Vibrato / Flanger / Phaser / Delay / Reverb)
  → Reverb IR (two parallel stereo IRs with L/R Mix + tail Cutoff, and a local boost/cut Master)
  → Room (short dark ambience)
  → Master Output
```

### Tabs

| Tab | Contents |
|---|---|
| **Pre FX** | Impersonator, Pitch, Gate, Acoustic Sim, Compressor, NAM Pedal, Pre-Amp EQ (the tuner stays in the editor header) |
| **Amp** | NAM amp model, Power, Gain, Bass, Mid, Treble, Presence, Depth, Output |
| **Post EQ** | Full-width 8-band graphical EQ with draggable nodes, per-band curve type, adjustable Q, high-pass/low-pass filters, and a live spectrum of the post-EQ signal behind the curve |
| **IR** | Two IR rows (filename screen, **LOAD**, arrows) plus one **Blend** knob and section bypass |
| **Post FX** | Chorus / Vibrato, Flanger, Phaser, Delay, Reverb, Reverb IR (two IR rows + L/R Mix/Cutoff + boost/cut Master) |

Each tab shows a hardware icon (pedal, amp, EQ, speaker, sparkle). **Right-click any tab** to bypass that whole section; the icon dims and shows a red LED while bypassed.

The bottom strip exposes **Input Gain** (−24…+24 dB, default 0 dB), the tab buttons, and **Room** and **Master** (master volume) as one centred group, with the knobs sitting immediately beside the tab bar rather than at the window edges. NAMBerry applies a fixed +6 dB output trim in addition to the saved Master value. The header contains the **Performance** toggle and the **PRESET** menu.

---

## Implementation

### NAM inference

`NamModelLoader` wraps `NeuralAudio::NeuralModel` and is used twice:

- `namModel_` — the amp,
- `namPedal_` — the pedal/preamp.

NAM models are mono and stateful. Each loader sums all input channels to mono, runs one model, applies the tone stack, and copies the result to every output channel. This avoids the distortion that occurs when L and R are fed through the same stateful model separately.

Both loaders use the same tone-stack implementation (bass, mid and treble); the amp additionally exposes presence and depth:

- bass low shelf,
- mid peak,
- treble high shelf,
- presence high shelf (amp),
- depth low shelf (amp).

### Impersonator pickup shaper

`Impersonator` is a pre-amp EQ pedal placed immediately after the tuner and before the pitch stage. It approximates the broad tonal shift between pickup positions using two fixed IIR filter chains, with a short crossfade when the direction switch changes.

**Bridge → Neck**
- 80 Hz high-pass
- +4.5 dB low shelf at 180 Hz
- +2 dB peak at 450 Hz
- −4 dB peak at 3 kHz
- −8 dB high shelf at 4 kHz
- 5 kHz low-pass

**Neck → Bridge**
- 130 Hz high-pass
- −5 dB peak at 400 Hz
- +4 dB peak at 3 kHz
- +3 dB high shelf at 5.5 kHz

Controls are the **B → N / N → B** switch, a manual **Level** trim (default 0 dB), and a bypass footswitch. The pedal starts bypassed. There is no automatic +3 dB compensation. EQ changes cannot reproduce the physical pick-attack geometry or the full phase relationship between pickup positions, so this is a tone-shaping approximation.

### Pitch stage (capo + drop tune)

The capo and drop tune share one pitch stage:

- **One semitones control**: −12…+12.
- **Mix**: 0–100 % wet.
- **Selectable pitch engine quality**: `Low Latency` keeps the response tight; `Quality` uses a longer analysis window for more stable low notes and chords.
- **Measured active latency**, reported to the host; zero latency when bypassed or at 0 semitones.

The pitch stage uses a real-time Rubber Band engine when its runtime library is available, with a self-contained Signalsmith Stretch fallback. Both engines use fixed-size FIFOs and process fixed internal chunks, so host sample-rate and block-size changes cannot reallocate or corrupt the shifter state. The fallback Stretch engine uses a 50 ms STFT block with a 5 ms hop in Low Latency mode and a 100 ms block with a 10 ms hop in Quality mode.

The pitch detector still runs an 80 ms analysis window with decimated autocorrelation, first-peak selection, parabolic interpolation, confidence and median smoothing, but it is now only used for the tuner display; the pitch engine does not need the detected period.

While pitch shifting is active, the dry path is delayed by the engine latency and **mono-summed**, so the dry signal is always centred. When the stage is bypassed, at 0 semitones, or at 0 % mix, it outputs the dry signal immediately with no latency compensation delay.

### Tuner

`DropTuner` is detection-only:

- 80 ms analysis window,
- first-peak autocorrelation with confidence,
- note name and cents deviation,
- editable **reference frequency** (400–480 Hz, default 440 Hz).

The reference affects the note/cents display only. The tuner lives permanently in the editor header, so it is visible on every tab: a horizontal strobe guide with moving bands and a cents needle that turn green near zero, amber within ±10 cents, and red beyond that.

### IR loader

`IrLoader` holds exactly two `juce::dsp::Convolution` instances, blended by one crossfade:

- both IRs loaded: gains `1 − Blend` and `Blend`, which always sum to unity,
- only one IR loaded: that IR plays at unity, whatever the knob says,
- no IR loaded: the dry signal passes through untouched,
- section bypass,
- pre-allocated scratch buffers, so the audio thread never allocates.

### Reverb IR pedal

`ReverbIrLoader` is a separate convolution stage on the Post FX tab with two full stereo IR slots, **L** and **R**. Both slots run in parallel as wet sends; there is no Blend knob. **Mix L** and **Mix R** set each send level and also crossfade the shared dry signal out: at 0 % the stage is an exact passthrough, and at 100 % the dry signal is fully removed. With two slots, the higher Mix decides how much dry remains. Each row has a long filename screen, **LOAD** button, arrows and drag-and-drop like the cabinet loader. **Mix L** and **Mix R** are highlighted with an accent ring because they are the main tone-shaping controls; **Cutoff L** / **Cutoff R** trim an overly long IR tail with a short, click-free fade. **Master** is a local wet-output boost/cut (-24 to +12 dB) and does not affect the global Master Output control. With no IR loaded, or while bypassed, the stage is an exact passthrough. Loading a new IR applies the current Cutoff and discards that slot's previous tail. Each slot has its own last-used folder and preset path.

### Dynamics and EQ

- `NoiseGate` — linked peak detector, attack/release, bypass.
- `Compressor` — attack, release, threshold, ratio, makeup, optional Tight low-band sidechain, and automatic makeup gain so compression remains audible without a level drop.
- `MultiBandEQ` — low shelf, mid peak, high shelf; used for the Pre-EQ.
- `PostAmpEQ` — eight-band graphical EQ with a selectable low-pass shelf, high-pass shelf or bell curve on every band, plus 12 dB/octave high-pass and low-pass filters, used after the amp.

### Acoustic simulator

`AcousticSim` sits immediately before the compressor, after the gate, so it shapes the raw pickup before dynamics. It aims at a real steel-string acoustic rather than a vintage AC-2/AC-3 voicing: four fixed modal-EQ curves approximate different body sizes, and Top/Body/Piezo knobs crossfade additive parallel sections, so continuous automation stays click-free. A mono envelope adds a short pick attack and low-string bloom, so plucks settle instead of sustaining like a magnetic pickup.

A gentle 55–90 Hz high-pass (depending on mode) conditions the input, then the four mode chains run in parallel with a smoothed weight vector (a mode switch is a 20 ms crossfade, never a hard switch). Each knob crossfades toward its own EQ curve — at 0 the section contributes exactly nothing:

- **Body** — resonances at 85 Hz (Q 2.2, +4.5 dB), 180 Hz (Q 1.8, +4 dB) and 360 Hz (Q 1.5, +3 dB) to excite low E, A and D, a −3.5 dB dip at 700 Hz for electric honk, and a −2 dB cut at 2.5 kHz. The envelope briefly lifts this section on attack.
- **Top** — −2 dB at 2.4 kHz, +2 dB at 3.5 kHz, a +5 dB high shelf at 5.5 kHz and +3 dB air at 9 kHz. The same envelope opens this section on the pick and closes it again during sustain.
- **Piezo** — a gentler +4 dB peak at 2.4 kHz and +3 dB high shelf at 4 kHz, with a mild `tanh` soft clipper on the 2–5 kHz band only (drive 1.5, unity-normalised) for piezo-preamp edge without turning the whole signal into distortion.

**Mode curves**

| Mode | Character |
|---|---|
| Standard | balanced steel-string: 90/190/380 Hz body modes, 700 Hz honk dip, +4 dB high shelf at 4.5 kHz |
| Jumbo | big air mode at 85 Hz, 175 Hz top, warmer 4 kHz shelf, 11 kHz low-pass |
| Enhanced | "recorded" acoustic: 100/200 Hz low support, 2.4 kHz presence, +6 dB shelf at 5.5 kHz, 9 kHz air |
| Piezo | thin and direct: scooped 500 Hz, modest 2.2/3.2 kHz peaks, 10 kHz low-pass |

**Level** is a manual ±12 dB trim with no automatic makeup. The pedal starts **bypassed**.

Engaging the pedal also force-bypasses the **NAM Pedal, NAM Amp, Pre-EQ, Post-EQ and IR Loader** by flipping their bypass parameters, so the UI reflects the change. Disengaging restores each stage the sim switched off — unless the user changed it while the sim was on, which is left alone. A freshly loaded preset is treated as ground truth and clears this bookkeeping. There is no internal reverb; ambience comes from the Post FX Reverb as usual.

### Post effects

`PostEffects` combines custom modulation DSP with JUCE effects:

- a custom three-voice `ChorusEnsemble`: 120°-spread sine voices, a slow wide LFO plus a fast shallow LFO, 5–30 ms centre delay, stereo panning, a mono/stereo switch, BBD-style wet filtering, feedback and a 0–100 % mix (100 % is pure vibrato with no dry),
- a custom `ModulatedDelay` for flanger: triangle LFO, 0.5–10 ms manual delay, signed feedback and mix,
- a custom `Phaser` with 4/8/12 first-order all-pass stages, sine LFO and feedback,
- `juce::dsp::DelayLine` with feedback, mix, a tone tilt and tape-style fuzz on the repeats; echoes alternate left/right in Ping-Pong mode or stay centred in Mono mode,
- `juce::dsp::Reverb`.

The flanger uses the shared modulated-delay core; the chorus uses its own three-voice ensemble core. All three modulation effects share the LFO and parameter-smoothing framework, but use different delay structures and filter topologies.

The separate `ReverbIrLoader` pedal described above runs after `PostEffects`, so it can be dialled independently of the algorithmic `juce::dsp::Reverb`.

A separate `RoomSim` stage runs after `ReverbIrLoader` and before the master trim. It feeds a mono-derived guitar signal into two slightly offset, dark-filtered short delays (28 ms left, 34 ms right). Feedback is deliberately low (3–15 %), so the delays repeat the guitar once or twice and die away almost immediately instead of becoming a tail. **Room** ranges from 0–100 %: 0 % is an exact passthrough and the default, while higher values fade the wet signal in to a maximum of 0.8 (about -2 dB) and gently increase feedback. The wet path is high-passed at 120 Hz and low-passed at 6 kHz, the dry path stays at unity, and stale tails are discarded when the room is turned back on.

Switching the delay off or changing between Ping-Pong and Mono clears its delay lines, so old feedback cannot reappear in the wrong mode. Bypassing the whole post-FX block also clears the delay buffer. The **Tone** knob tilts the repeats from dark (negative) through neutral to bright (positive), and **Fuzz** adds tape-like saturation; both are smoothed and shape the delay-line input, so the character compounds on every repeat.

### File management and persistence

- Six separate starting folders: one for amps, one for pedals, one for cabinet IRs, and one for each of the two reverb IR slots.
- Tapping a filename screen opens `FilePopup`, a folder list over the editor: `..` first, then folders, then matching files. Tap a folder to enter it, tap a file to load it.
- Touch anywhere else on the UI — or press `Esc` — to dismiss the popup without loading anything.
- Each loader also has a **LOAD** button that opens the asynchronous system file browser.
- When the popup closes, the folder you were browsing becomes that loader's starting folder.
- Previous/next arrows on the amp, pedal, cabinet IR and both reverb IR fields step through matching files in the current file’s folder.
- Drag-and-drop works for `.nam` and IR files.
- Selected amp, pedal, both cabinet IR paths and both reverb IR paths are persisted and restored automatically.
- The older `DirectoryBrowser` component is still compiled but is no longer reachable from the UI, so there is no `SET ROOT...` button anymore.
- Settings are stored in a per-user `PropertiesFile`:
  - Linux: `~/.config/NAMBerry/NAMBerry.settings`
  - macOS/Windows: under the standard user application-data directory, in `NAMBerry/NAMBerry.settings`.

DAW project state stores all parameter values through `AudioProcessorValueTreeState`; the model/IR paths and starting folders live in the per-user settings file. Existing projects with the old 0–1 modulation ranges are migrated once to the new percentage values when loaded, and projects from the four-slot IR era are converted to the two-IR Blend model once (the legacy `irMix`/`irPan`/`irPhase` parameters are dropped and `irBlend` is derived from which slots were up). Version 8 state migrates the old `imageMix` value to `roomAmount`, drops `imageWidth`/`imageMix` and the older room parameters, and defaults the new `delayPingPong` toggle on. Version 9 added `reverbIrBypass` (on) and a single `reverbIrMix`. Version 10 replaces that Mix with `reverbIrLeftMix` and `reverbIrRightMix`, seeded from the old value so older projects keep their wet level. Version 11 adds `reverbIrLeftCutoff` and `reverbIrRightCutoff`, defaulting to 3 s so existing IR tails are unchanged. Version 12 replaces the pedal's global-Master link with `reverbIrLevel` (default 0 dB) so the knob is a local wet boost/cut. Version 13 adds the chorus **Stereo** switch (on by default), and version 14 adds the delay **Tone** and **Fuzz** knobs (both 0 by default).

### UI

The editor is JUCE-native and hardware-inspired:

- left-to-right signal chain in a horizontally scrollable viewport,
- a persistent centred bottom strip with **Input Gain**, the tabs, and **Room** / **Output** grouped together,
- pedal enclosures, an amp head and rack units,
- large 48 px hardware controls (the browser list is desktop-sized),
- footswitch LEDs show **active** (not bypassed) state,
- the Performance toggle stops the hover-glow animation, the live Post EQ spectrum and the remaining decorative timers,
- a numeric reference field for the tuner.

At the default 1280×640 editor size, all pedals on every tab fit without horizontal scrolling; narrower windows still use the scrollable viewport.

`ui-spec.md` records an earlier Qt 6/QML exploration, but the shipped implementation uses JUCE native components. Qt Quick was not available in the build environment, and embedding a second event loop in a plugin was not practical.

---

## Technologies

| Area | Technology |
|---|---|
| Language | C++20 |
| Plugin framework | JUCE **8.0.6** |
| Formats | VST3, Standalone |
| NAM inference | **NeuralAudio** (with NeuralAmpModelerCore, RTNeural and its submodules) |
| IR convolution | `juce::dsp::Convolution` |
| EQ / dynamics | JUCE `dsp::IIR`, custom compressor/gate |
| Post FX | Custom three-voice stereo chorus ensemble and modulated-delay flanger; custom 4/8/12-stage phaser; JUCE `dsp::DelayLine` with ping-pong/mono modes, `dsp::Reverb` and a separate two-slot `dsp::Convolution` reverb IR pedal with per-slot tail Cutoff |
| Room | Custom two-tap `RoomSim` short-delay room with low feedback and dark wet filtering |
| Pitch | **Rubber Band** when available, with **Signalsmith Stretch** fallback |
| Settings | JUCE `PropertiesFile` |
| Build | CMake 3.22+, CMake presets, helper scripts |
| Tests | JUCE console apps: offline render and headless UI snapshots |

Third-party components:

- JUCE — https://juce.com
- NeuralAudio — https://github.com/mikeoliphant/NeuralAudio
- NeuralAmpModelerCore — https://github.com/sdatkinson/NeuralAmpModelerCore
- Rubber Band — https://breakfastquay.com/rubberband/ (optional runtime dependency; GPL or commercial licensing)
- Signalsmith Stretch — https://github.com/signalsmith-audio/signalsmith-stretch
- Signalsmith Linear — https://github.com/signalsmith-audio/linear

---

## Project layout

```
CMakeLists.txt
CMakePresets.json
README.md
spec.md                 # product / DSP specification
ui-spec.md              # earlier Qt/QML UI exploration (not the shipped UI)

src/
  PluginProcessor.*     # signal chain, parameters, persistence/restore
  PluginEditor.*        # hardware-style editor
  directory_browser.*   # legacy folder browser (compiled, no longer in the UI)
  nam_model_loader.cpp
  impersonator.cpp      # pickup-shaping preamp
  acoustic_sim.cpp      # Boss-style acoustic simulator (modal body model)
  pitch_shifter.cpp     # Rubber Band / Signalsmith streaming pitch engines
  digital_capo.cpp      # shared pitch stage
  drop_tuner.cpp
  modulation_effects.cpp
  ir_loader.cpp
  multi_band_eq.cpp
  post_amp_eq.cpp
  compressor.cpp
  noise_gate.cpp
  post_effects.cpp
  room_sim.cpp

include/namberry/
  NamModelLoader.h
  Impersonator.h
  AcousticSim.h
  PitchShifter.h
  DigitalCapo.h
  DropTuner.h
  ModulationEffects.h
  IrLoader.h
  MultiBandEQ.h
  PostAmpEQ.h
  Compressor.h
  NoiseGate.h
  RoomSim.h
  PostEffects.h
  DirectoryBrowser.h

tests/
  offline_render.cpp    # DSP / detector / persistence tests
  ui_snapshot.cpp       # renders the UI tabs and the file popups to PNG

scripts/
  build-standalone.sh
  build-vst3.sh

JUCE/                   # cloned, ignored by git
deps/                   # cloned, ignored by git
```

---

## Using NAMberry

### First run

1. Open the **Standalone** app, or insert the VST3 on a stereo track.
2. In the Standalone, choose the audio device, sample rate and buffer size.
3. Load an amp model in the **Amp** tab.
4. Optionally load a pedal model in **Pre FX → PEDAL**.
5. Load one or more IRs in the **IR** tab.
6. Play.

The plugin is stereo in / stereo out. The NAM stages are mono internally and copied to both channels; the dry path is centred.

### Loading models and IRs

Every loader offers the same two ways to pick a file: tap its filename screen to list the folder right there, or press **LOAD** to open the system file browser.

- **NAM amp**: Amp tab → tap the model display on the grille, or **LOAD** underneath it. The `<` / `>` arrows on either side of the display step through the folder.
- **NAM pedal**: Pre FX → **PEDAL** → tap the pedal's filename screen, or **LOAD**.
- **IR rows**: IR tab → tap the row's filename screen, or **LOAD**.

The popup:

1. Opens at the folder of the loaded file, or at that loader's starting folder when nothing is loaded.
2. Shows `..` first, then folders, then matching files.
3. Tap a folder to enter it; tap a file to load it.
4. Touch anywhere else on the UI — or press `Esc` — to dismiss it without loading.
5. When it closes, the folder you were browsing becomes that loader's starting folder.

The three starting folders are completely separate:

- Amp folder for amp models.
- Pedal folder for pedal/preamp models.
- IR folder for both IR rows.

**Previous/next arrows** step through the matching files in the currently loaded file’s folder. The selection wraps at the ends. The amp, pedal and each IR row remember their own selected path.

You can also drag `.nam` and IR files directly onto the corresponding loader/field.

The plugin restores the last selected amp, pedal and both IRs when it next starts, provided the files still exist.

### Tuner

The tuner is a permanent compact panel in the editor header, visible on every tab:

- A horizontal strobe guide shows the cents error.
- Green: within ±2 cents.
- Amber: within ±10 cents.
- Red: further out.
- The left readout shows the note and cents, e.g. `A3 23.4 c`.
- **Ref Hz** is an editable numeric field on the right:
  - Allowed range 400–480 Hz.
  - Default 440.0 Hz.
  - Type a value (432, 440, 441, 442, etc.), press **Enter** or click away.
  - **Escape** restores the current value.

The reference affects the note and cents display. It does not transpose audio.

### Impersonator

The **IMPERSONATOR** pedal sits between the tuner and pitch stage:

- **B → N** softens bridge-pickup bite and adds body/warmth.
- **N → B** tightens neck-pickup lows and adds bridge-like attack and presence.
- **Level** is a manual −12…+12 dB trim, default 0 dB.
- The pedal starts **bypassed** on the footswitch.

There is no automatic +3 dB makeup gain; use the Level knob only if the shaped tone needs matching.

### Pitch (capo / drop tune)

The shared **PITCH** pedal controls both capo and drop tuning:

| Control | Meaning |
|---|---|
| **Semitones** | −12…+12; 0 = no shift |
| **Tone Match** | 0–100 % formant matching; 100 % keeps the source brightness when detuning |
| **Mix** | 0–100 % wet (default 100 %) |
| **Quality** | Toggle between Low Latency and Quality engine modes |
| **Footswitch** | Bypass |

The pitch stage uses a real-time Rubber Band engine when `librubberband` is available at runtime, with a self-contained Signalsmith Stretch fallback. Two modes are available on the Pitch pedal: **Low Latency** and **Quality**. The engine accepts arbitrary host block sizes and sample rates through a fixed-size streaming layer, so block-size changes cannot reallocate or corrupt the shifter state. While pitch shifting is active it reports its measured latency to the host and delays the dry path by the same amount; when bypassed, at 0 semitones, or at 0 % mix it reports **0 ms** and passes the dry signal immediately. The dry path is always mono-centred.

**Tone Match** auto-corrects the formant envelope to match the original guitar, so detuning down does not turn the instrument dark. At 100 % the source formants are held in place; at 0 % the formants shift naturally with the pitch. The control blends continuously on both engines.

### Amp

- Load the amp `.nam` from the model display on the grille: tap it to list the folder, use `<` / `>` to step, or **LOAD** for the file browser.
- Power, Gain, Bass, Mid, Treble, Presence, Depth, Output.
- The tone stack runs after the model.

### Pedal

- Load the pedal/preamp `.nam` with **PEDAL MODEL**.
- Bass, Mid and Treble (the EQ) sit on the top row; Gain and Volume on the bottom row.
- Bypass footswitch.
- The pedal is a second NAM model, not a waveshaper.

### Gate and compressor

- **Gate**: threshold, attack, release, bypass. Default release is **20 ms**.
- **Compressor**: threshold, ratio, attack, release, makeup, Tight, bypass. Tight derives the detector from a low-passed sidechain. Automatic makeup gain is applied on top of the Makeup control, so lowering the threshold or raising the ratio produces obvious compression without simply lowering the level. Defaults are −24 dB threshold, 8:1 ratio, 5 ms attack and 120 ms release.

### Acoustic sim

The **ACOUSTIC** pedal sits between the gate and the compressor:

| Control | Meaning |
|---|---|
| **Mode** | Standard / Jumbo / Enhanced / Piezo — four fixed body curves, crossfaded on switch |
| **Top** | 0–100 % brilliance, air and pick attack (default 60 %) |
| **Body** | 0–100 % modal body resonance, honk removal and low-string bloom (default 65 %) |
| **Piezo** | 0–100 % piezo quack and 2–5 kHz saturation (default 20 %) |
| **Level** | −12…+12 dB manual trim, default 0 dB |
| **Footswitch** | Bypass; the pedal starts **bypassed** |

Engaging the pedal automatically switches the NAM Pedal, NAM Amp, Pre-EQ, Post-EQ and IR Loader off; disengaging restores what it switched off, unless you changed a stage by hand while it was on.

### EQs

The Pre-EQ has three bands: low shelf, mid peak, high shelf with frequency, gain and Q, plus a **Volume** trim in its footer (−12…+12 dB, up to +12 dB of boost).

The Post-EQ is a full-width **8-band graphical EQ**. Drag a node horizontally to change frequency and vertically to change gain; use the mouse wheel for Q. A label above the graph names the focused band and its frequency (or the focused HPF/LPF). Double-click a node to disable that band: its gain flattens to 0 dB and the node turns into a hollow outline marked **OFF**. Double-click the hollow node again to restore the previous gain. Right-click for reset options. A fixed pair of **Boost** and **Q** knobs on the right retargets to whichever band or filter is focused. For HPF/LPF, the Q knob adjusts the filter Q and Boost is inactive. The **Flat** button zeroes all gains and resets the filters.

Every point has its own selectable curve type in the focus strip above the knobs: **Low Pass** (low shelf), **High Pass** (high shelf) or **Band** (peaking bell). All three types use the Boost knob for boost/cut, so the type changes the shape rather than muting the control. The default curves are Low Pass on band 1, Band on bands 2–7 and High Pass on band 8. The Post-EQ also has a separate 12 dB/octave high-pass and low-pass filter (defaults 20 Hz and 20 kHz), with draggable cutoff handles on the graph and adjustable Q.

Older presets migrate automatically: the previous low/mid/high post-EQ bands become the new low shelf, first bell and high shelf, and the old high-pass/low-pass cutoff frequencies are retained. Projects from the eight-band version keep their curves because each band's saved type defaults to the shape it had before the selector was added.

### IR

Two IR rows plus a single **Blend** knob:

- each row shows its loaded filename, with a tap-to-list filename screen, **LOAD** and previous/next arrows,
- **Blend** crossfades between them: 0 % is IR 1, 100 % is IR 2, and it is the only knob on the tab,
- with only one IR loaded that IR plays at unity regardless of the knob,
- with nothing loaded the section passes the dry signal through,
- the IR section keeps its own bypass.

The two IRs are gained so they always sum to unity, so the cabinet level stays constant across the whole Blend range.

### Post FX

- Chorus: rate, depth, feedback, delay, mix, a **Stereo** switch and bypass. It uses a Boss-style three-voice stereo ensemble with a slow wide LFO, a fast shallow LFO, 120° voice phases and BBD-style wet tone shaping. Mix now runs 0–100 %: 50 % is the classic chorus blend, and 100 % removes the dry signal for a pure three-voice vibrato without a level jump. **Stereo** spreads the voices across the field; turning it off folds them to mono.
- Flanger: rate, depth, manual, feedback, mix, bypass. Mix tops out at 50 %, with a 2× wet boost.
- Phaser: rate, depth, feedback, stages, mix, bypass. Mix tops out at 50 %, with a 2× wet boost.
- Room: **Room** in the bottom strip. 0 % is an exact passthrough and the default; higher values add a short, dark stereo tail without changing the dry level. There is no separate bypass.
- Delay: mix, time, feedback, **Tone**, **Fuzz**, Ping-Pong/Mono toggle, bypass. Ping-Pong bounces the echoes between left and right; Mono keeps them centred. Feedback is 0–10: 0 is one repeat and 10 is an almost endless tail. Tone tilts the repeats between dark and bright at 0 % (neutral); Fuzz drives them into tape-like saturation at 100 %.
- Reverb: mix, decay, width, bypass. Its dry level is normalized so increasing reverb never boosts the dry guitar.
- Reverb IR: two long filename rows (L and R), each with **LOAD**, arrows and drag-and-drop, plus accented **Mix L** / **Mix R**, **Cutoff L** / **Cutoff R**, a **Master** knob that boosts or cuts the wet output locally (-24 to +12 dB), and one bypass footswitch. The stereo IRs run in parallel as wet sends; each Mix sets its send level and crossfades the dry guitar out, with 100 % giving no dry signal and 0 % in both slots giving an exact passthrough. Cutoff fades that slot's IR tail after the chosen time. The pedal starts bypassed and each slot keeps its own last-used folder.
- Master **Output** is in the bottom strip beside the tabs.

Use **Room** in the bottom strip to add a little ambience; there is no separate room bypass. Switching the delay off discards its feedback tail.

### Performance mode

The header **Performance** toggle makes the editor lighter: it snaps the panel hover-glow animation instead of animating it, stops the decorative section timers, skips the live Post EQ spectrum FFT and its repaints, skips the audio-thread spectrum tap, and draws the tuner without the moving strobe bands. Audio quality is unchanged. The setting is saved with the plugin state.

### Presets

Use the **PRESET** button in the header:

- **Save Preset...** writes a `.namberrypreset` file containing every parameter value plus the current amp, pedal, both cabinet IR paths and both reverb IR paths.
- **Load Preset...** restores the parameters and loads any referenced model/IR files that still exist.
- **Open Preset Folder** opens the default preset directory.

The default preset folder is `NAMBerry/Presets` under the user application-data directory. The last-used preset folder is remembered. If a preset references a missing model or IR, the parameters are applied and NAMberry warns that one or more files could not be loaded.

### State and settings

- DAW projects save all parameter values with the plugin.
- The per-user settings file saves:
  - amp folder,
  - pedal folder,
  - cabinet IR folder and a folder for each reverb IR slot,
  - last amp model path,
  - last pedal model path,
  - last IR path for each of the two cabinet slots,
  - last reverb IR path for each of the two slots,
  - last preset directory.

If a saved file has moved, the loader shows no model/empty until it is reloaded; the path remains in the settings for when it returns.

---

## Tests

Build the test tools:

```bash
cmake --build build --target NAMBerryOfflineTest NAMBerryUiSnapshot
```

### Offline DSP tests

```bash
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --detector
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --pitch
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --pitch-low
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --pitch-robust
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --pitch-blocks
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --detected-stage
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --dry-center
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --delay-feedback
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --delay-reset
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --delay-ping-pong
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --delay-character
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --chorus-ensemble
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --reverb-dry
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --reverb-ir
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --reverb-ir-state
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --room
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --modulation
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --fx-state
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --preset
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --compressor
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --impersonator
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --acoustic-sim
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --acoustic-auto-off
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --tone-match
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --ir-level
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --ir-state
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --post-eq
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --post-eq-state
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --spectrum
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --reference
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --persist
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --dirs
```

Run the full chain with a model:

```bash
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest /path/to/model.nam
```

What they cover:

- `--detector`: pitch detection across 82.41–440 Hz.
- `--pitch`: pitch accuracy and dry/bypass behaviour.
- `--pitch-low`: low-note tracking at guitar low E and drop tuning.
- `--pitch-robust`: sample-rate and randomized host-block-size robustness in both quality modes.
- `--pitch-blocks`: Stretch at 128, 192, 256 and 512-sample host blocks.
- `--delay-feedback`: one-repeat at 0 and a long tail at 10.
- `--reverb-dry`: confirms the reverb does not boost the dry signal.
- `--reverb-ir`: verifies passthrough with no IR, while bypassed and at 0 % Mix; checks that a 100 % Mix removes the dry signal, that the two stereo slots sum independently with their own Mix knobs, that the local Master boosts and cuts the wet signal, that Cutoff removes a late IR tap, that loaded-file metadata is per slot, and that oversized host blocks are handled without resizing scratch buffers.
- `--reverb-ir-state`: verifies version-12 migration from the old single Mix to both slot Mix knobs plus Cutoff and Level defaults, and that stored Reverb IR bypass/Mix/Cutoff/Level values survive a state round-trip.
- `--detected-stage`: detector → Stretch pitch stage.
- `--dry-center`: verifies both channels are identical when fully dry.
- `--delay-reset`: verifies the delay tail is discarded on bypass.
- `--delay-ping-pong`: verifies alternating left/right echoes, feedback repeats, and centred Mono mode.
- `--delay-character`: verifies the Tone tilt darkens/brightens the repeats and Fuzz adds harmonics without excessive level.
- `--chorus-ensemble`: verifies that three-voice modulation is centred at 0 % depth and stereo-decorrelated at full depth, that 100 % mix removes the dry signal, that the full-wet level stays bounded, and that the Stereo switch folds the wet path to mono.
- `--room`: verifies passthrough, short-delay timing and low-feedback decay, stereo decorrelation from a mono source, dark and audible wet level, odd block sizes, sample-rate robustness and stale-tail clearing.
- `--modulation`: verifies finite chorus, flanger and phaser output, all phaser stage counts, and bypass.
- `--fx-state`: verifies migration of old post-FX parameter ranges/defaults, that the version-13 chorus stereo switch defaults to on for old projects and persists when saved off, and that the version-14 delay Tone/Fuzz defaults to 0.
- `--preset`: verifies preset save/load, including amp, cabinet IR and both reverb IR file paths.
- `--compressor`: verifies gain reduction, automatic makeup gain and bypass.
- `--impersonator`: verifies both pickup-direction EQ curves, manual Level and bypass.
- `--acoustic-sim`: verifies bypass pass-through, the Body/Top/Piezo knob curves, Level, every mode's output bounds and click-free mode/knob changes.
- `--acoustic-auto-off`: verifies the sim force-bypasses the distortion, EQ and IR stages, restores them on disengagement while respecting manual changes, and treats a loaded preset as ground truth.
- `--tone-match`: verifies Tone Match keeps the source brighter than a natural detune.
- `--ir-level`: verifies unity level across the whole Blend range (0, 0.25, 0.5, 1) and untouched dry passthrough with no IR loaded.
- `--ir-state`: verifies migration of legacy four-slot IR parameters to `irBlend`.
- `--post-eq`: verifies the 8-band boost/cut, per-band curve types, HPF/LPF attenuation, bypass pass-through and the shared response calculation.
- `--post-eq-state`: verifies version-5 migration of the old three-band post-EQ into the 8-band layout and version-6 migration that adds each band's curve type.
- `--spectrum`: verifies the live Post EQ spectrum tap finds a 1 kHz tone in the correct bin at the expected level and decays to silence.
- `--reference`: verifies 432/440 Hz reference behaviour.
- `--persist`: restores selected amp, pedal and IR paths; expects local sample files and skips if they are missing.
- `--dirs`: sets the amp, pedal and IR root directories.
- `<model.nam>`: full processor with a NAM model.

### UI snapshots

```bash
./build/NAMBerryUiSnapshot_artefacts/Release/NAMBerryUiSnapshot /tmp/namberry-ui
```

The snapshot run also drives a Post EQ drag, wheel-Q change, double-click
disable/re-enable, the focused-band Boost/Q knobs and the curve-type switch,
and fails if the parameters do not follow.

Writes:

```
pre-fx.png
amp.png
post-eq.png
ir.png
post-fx.png
browser.png
browser-ir.png
```

The snapshot tool is headless and does not need a DAW.

---

## Known limitations

- **50 ms pitch latency is reported only while pitch shifting is active** in Low Latency mode. Bypassed, 0-semitone or 0 % mix states report 0 ms and pass the dry signal immediately; the host latency changes when the pitch stage is toggled. Quality mode reports a longer measured latency.
- The **Standalone app** does not apply host-style plugin delay compensation; the active pitch latency is audible there. DAWs compensate it.
- The pitch stage uses an optional runtime Rubber Band engine and a self-contained Stretch fallback. Missing Rubber Band affects quality, not correctness.
- The **Impersonator** is an EQ approximation. It cannot reproduce the physical pick-attack geometry or the full phase relationship between pickup positions.
- The **Acoustic Sim** is a filter-bank plus envelope approximation. It gives low strings body support and a plucked attack, but it cannot add true body radiance or the room projection of a real acoustic guitar.
- NAM model latency is not separately reported by the plugin; only the pitch-stage latency is reported.
- The **Room** is a short algorithmic ambience, not a convolution of a real space. It is intentionally dark and subtle; use Reverb for longer or more prominent tails.
- The plugin is **stereo in / stereo out only**.
- Model and IR paths are **per-user settings**, not embedded in the DAW project. Moving a project to another machine may require reloading them.
- `ui-spec.md` is an earlier Qt/QML exploration; the shipped UI is JUCE-native.
- The repository contains no NAM models or IR files. Use your own `.nam` captures and WAV/AIFF IRs.
- NAMberry is released under the **MIT licence** (see `LICENSE`). JUCE and the
  optional Rubber Band engine keep their own licensing terms; see
  [Third-party licences](#third-party-licences).

---

## Third-party licences

- **JUCE 8.0.6** — see `JUCE/LICENSE.md`; JUCE is available under AGPLv3 or a commercial licence.
- **NeuralAudio** — MIT (Mike Oliphant). Its submodules (NeuralAmpModelerCore, RTNeural, etc.) retain their own licences.
- **Rubber Band Library** — GPL or commercial licence (Breakfast Quay). It is an optional runtime dependency loaded dynamically when present.
- **Signalsmith Stretch / Linear** — MIT (Signalsmith Audio).
- NAM model files and IR captures are supplied by the user and are not part of this repository.

Check the relevant upstream repositories before distributing binaries.

---

## Credits

- NAM / NeuralAmpModelerCore by Steven Atkinson and contributors.
- NeuralAudio by Mike Oliphant.
- JUCE by Raw Material Software.
- Rubber Band Library by Breakfast Quay.
- Signalsmith Stretch/Linear by Geraint Luff / Signalsmith Audio.
- The guitar community who create and share NAM captures and IRs.
