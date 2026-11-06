# NAMberry

NAMberry is an all-in-one NAM-based guitar amp and effects plugin. It combines a NAM amp capture, a second NAM pedal/preamp capture, four blendable IR slots, dynamics, dual multi-band EQs, modulation/room/delay/reverb, and a low-latency pitch stage for capo and drop tuning.

The goal is to stop treating an amp capture as a single plugin in a long external chain. Load two captures, four cabinets, and a complete studio-style effects chain in one stereo plugin, with fast model browsing and a hardware-inspired interface.

> The pedal is not a generic waveshaper. It is a **second NAM model**, loaded independently from the amp.

---

## Why this exists

NAM captures sound excellent, but the surrounding workflow is usually fragmented:

- one plugin for the amp capture,
- another for a pedal capture,
- another for IRs,
- separate gate/compressor/EQ plugins,
- separate modulation/room/delay/reverb plugins,
- manual file browsing for every model,
- and no simple capo/drop-tune stage.

NAMberry packages all of that into one plugin:

- **Two NAM captures** — one amp, one pedal/preamp.
- **Four IR slots** with per-slot mix, pan and phase, plus automatic level normalization across active slots.
- **Impersonator pickup shaper** — a bridge/neck EQ preamp immediately after the tuner.
- **A complete chain** — tuner, pickup shaper, pitch stage, gate, compressor, pedal, pre-amp EQ, amp, post-amp EQ, IR, post effects, master.
- **Room ambience** — a convolution room blend for a realistic sense of physical space, separate from the reverb.
- **Fast model management** — in-app NAM/IR browsers, two separate persisted roots, previous/next arrows, and drag-and-drop.
- **Persistent choices** — the selected amp, pedal and all four IR files are restored on the next session.
- **Preset save/load** — store the complete parameter state together with the current amp, pedal and IR file paths.
- **Studio-quality pitch** — Signalsmith Stretch handles capo and drop tuning; the 50 ms latency is only applied while pitch shifting is active.
- **A usable tuner** — a horizontal strobe guide with cents deviation and an editable reference frequency (440, 432, etc.).
- **Stereo correctness** — the NAM stages are mono models, summed and copied to both channels; the dry path is always centred.

---

## Signal chain

The chain runs left to right through the five editor tabs:

```
Input
  → Tuner (detection only)
  → Impersonator (pickup shaper)
  → Pitch (capo / drop tune)
  → Noise Gate
  → Compressor
  → NAM Pedal / Preamp
  → Pre-Amp EQ
  → NAM Amp
  → Post-Amp EQ
  → 4-Slot IR Loader
  → Post FX (Chorus / Flanger / Phaser / Room / Delay / Reverb)
  → Master Output
```

### Tabs

| Tab | Contents |
|---|---|
| **Pre FX** | Tuner, Impersonator, Pitch, Gate, Compressor, NAM Pedal, Pre-Amp EQ |
| **Amp** | NAM amp model, Power, Gain, Bass, Mid, Treble, Presence, Depth, Output |
| **Post EQ** | 3-band post-amp EQ |
| **IR** | Four IR slots with mix, pan, phase, browse and arrows |
| **Post FX** | Chorus, Flanger, Phaser, Room (Size), Delay, Reverb |

The bottom strip beside the tabs always exposes **Room Mix** and **Output** (master volume). The header contains the **Reduced Motion** preference and the **PRESET** menu. The engine is user-selected; there is no automatic engine switching.

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
- **Signalsmith Stretch** pitch shifting.
- **50 ms latency while active**, reported to the host; zero latency when bypassed or at 0 semitones.
- **No engine toggle**: Stretch is the only pitch engine.

Signalsmith Stretch uses a 50 ms STFT block with a 7 ms hop. This is a quality-over-latency trade-off: unlike the previous DualTap/PSOLA engines, it cannot run at 5 ms.

The pitch detector still runs an 80 ms analysis window with decimated autocorrelation, first-peak selection, parabolic interpolation, confidence and median smoothing, but it is now only used for the tuner display; Stretch does not need the detected period.

While pitch shifting is active, the dry path is delayed by the Stretch latency and **mono-summed**, so the dry signal is always centred. When the stage is bypassed, at 0 semitones, or at 0 % mix, it outputs the dry signal immediately with no latency compensation delay.

### Tuner

`DropTuner` is detection-only:

- 80 ms analysis window,
- first-peak autocorrelation with confidence,
- note name and cents deviation,
- editable **reference frequency** (400–480 Hz, default 440 Hz).

The reference affects the note/cents display only. The UI draws a horizontal strobe guide: moving bands and a cents needle turn green near zero, amber within ±10 cents, and red beyond that. The wide tuner panel sits above the Impersonator and Pitch pedals to save horizontal space.

### IR loader

`IrLoader` uses four `juce::dsp::Convolution` instances:

- per-slot mix,
- per-slot stereo pan,
- per-slot phase invert,
- dry/wet blend,
- bypass.

### Dynamics and EQ

- `NoiseGate` — linked peak detector, attack/release, bypass.
- `Compressor` — attack, release, threshold, ratio, makeup, optional Tight low-band sidechain, and automatic makeup gain so compression remains audible without a level drop.
- `MultiBandEQ` — low shelf, mid peak, high shelf; used for Pre-EQ and Post-EQ.

### Post effects

`PostEffects` combines custom modulation DSP with JUCE effects:

- a custom `ModulatedDelay` for chorus: sine LFO, right-channel 180° phase, 5–30 ms delay, feedback and mix,
- a custom `ModulatedDelay` for flanger: triangle LFO, 0.5–10 ms manual delay, signed feedback and mix,
- a custom `Phaser` with 4/8/12 first-order all-pass stages, sine LFO and feedback,
- a convolution `Room` stage using generated short stereo room impulse responses,
- `juce::dsp::DelayLine` with feedback and mix,
- `juce::dsp::Reverb`.

The chorus and flanger share the modulated-delay core; all three share the LFO and parameter-smoothing framework, but use different delay ranges, waveforms and filter topologies. The Room is a built-in convolution room, not a long reverb; its **Size** control crossfades between generated small and large room impulse responses, and its **Room Mix** control crossfades dry with room. The separate Reverb provides the long tail.

Switching the delay off clears its delay line, so old feedback cannot reappear when it is switched back on. Bypassing the whole post-FX block also clears the delay and room buffers.

### File management and persistence

- Two separate roots: one for NAM models, one for IRs.
- In-app `DirectoryBrowser` supports folder traversal, `SET ROOT...`, `LOAD`, double-click, and extension filtering.
- Previous/next arrows on the amp, pedal and IR fields step through matching files in the current file’s folder.
- Drag-and-drop works for `.nam` and IR files.
- Selected amp, pedal and all four IR paths are persisted and restored automatically.
- Settings are stored in a per-user `PropertiesFile`:
  - Linux: `~/.config/NAMBerry/NAMBerry.settings`
  - macOS/Windows: under the standard user application-data directory, in `NAMBerry/NAMBerry.settings`.

DAW project state stores all parameter values through `AudioProcessorValueTreeState`; the model/IR paths and roots live in the per-user settings file. Existing projects with the old 0–1 modulation ranges are migrated once to the new percentage values when loaded.

### UI

The editor is JUCE-native and hardware-inspired:

- left-to-right signal chain in a horizontally scrollable viewport,
- a persistent bottom strip with **Room Mix** and **Output** beside the tabs,
- pedal enclosures, an amp head and rack units,
- large 48 px hardware controls (the browser list is desktop-sized),
- footswitch LEDs show **active** (not bypassed) state,
- the Reduced Motion preference disables the hover-glow animation,
- a numeric reference field for the tuner.

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
| Post FX | Custom LFO/modulated-delay chorus and flanger; custom 4/8/12-stage phaser; generated-IR convolution `Room`; JUCE `dsp::DelayLine` and `dsp::Reverb` |
| Pitch | **Signalsmith Stretch** (50 ms block / 7 ms hop) |
| Settings | JUCE `PropertiesFile` |
| Build | CMake 3.22+, CMake presets, helper scripts |
| Tests | JUCE console apps: offline render and headless UI snapshots |

Third-party components:

- JUCE — https://juce.com
- NeuralAudio — https://github.com/mikeoliphant/NeuralAudio
- NeuralAmpModelerCore — https://github.com/sdatkinson/NeuralAmpModelerCore
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
  directory_browser.*   # in-app NAM / IR browser
  nam_model_loader.cpp
  impersonator.cpp      # pickup-shaping preamp
  pitch_shifter.cpp     # Signalsmith Stretch wrapper
  digital_capo.cpp      # shared pitch stage
  drop_tuner.cpp
  modulation_effects.cpp
  ir_loader.cpp
  multi_band_eq.cpp
  compressor.cpp
  noise_gate.cpp
  room.cpp
  post_effects.cpp

include/namberry/
  NamModelLoader.h
  Impersonator.h
  PitchShifter.h
  DigitalCapo.h
  DropTuner.h
  ModulationEffects.h
  IrLoader.h
  MultiBandEQ.h
  Compressor.h
  NoiseGate.h
  Room.h
  PostEffects.h
  DirectoryBrowser.h

tests/
  offline_render.cpp    # DSP / detector / persistence tests
  ui_snapshot.cpp       # renders the UI tabs and browsers to PNG

scripts/
  build-standalone.sh
  build-vst3.sh

JUCE/                   # cloned, ignored by git
deps/                   # cloned, ignored by git
```

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

Each loader has a **BROWSE** button, a filename, and previous/next arrows.

- **NAM amp**: Amp tab → **AMP MODEL** → **BROWSE .NAM**.
- **NAM pedal**: Pre FX → **PEDAL** → **BROWSE .NAM**.
- **IR slots**: IR tab → **BROWSE** on the required slot.

The browser:

1. Opens at the saved NAM or IR root.
2. Shows folders first, then matching files.
3. Double-click a folder to enter; `UP` goes to the parent; `ROOT` returns to the root.
4. `SET ROOT...` chooses and persists a new root folder.
5. Select a file and press `LOAD`, or double-click it.

The two roots are completely separate:

- NAM root for amp and pedal models.
- IR root for all four IR slots.

**Previous/next arrows** step through the matching files in the currently loaded file’s folder. The selection wraps at the ends. The amp, pedal and each IR slot remember their own selected path.

You can also drag `.nam` and IR files directly onto the corresponding loader/field.

The plugin restores the last selected amp, pedal and all four slots when it next starts, provided the files still exist.

### Tuner

The tuner is a wide horizontal panel above the Impersonator and Pitch pedals:

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
| **Mix** | 0–100 % wet (default 100 %) |
| **Footswitch** | Bypass |

The pitch stage uses **Signalsmith Stretch**. While pitch shifting is active it reports **50 ms latency** to the host and delays the dry path by the same amount; when bypassed, at 0 semitones or 0 % mix it reports **0 ms** and passes the dry signal immediately. The dry path is always mono-centred. There is no engine toggle or automatic fallback.

### Amp

- Load the amp `.nam` with **AMP MODEL**.
- Power, Gain, Bass, Mid, Treble, Presence, Depth, Output.
- The tone stack runs after the model.

### Pedal

- Load the pedal/preamp `.nam` with **PEDAL MODEL**.
- Gain, Bass, Mid, Treble, Output.
- Bypass footswitch.
- The pedal is a second NAM model, not a waveshaper.

### Gate and compressor

- **Gate**: threshold, attack, release, bypass.
- **Compressor**: threshold, ratio, attack, release, makeup, Tight, bypass. Tight derives the detector from a low-passed sidechain. Automatic makeup gain is applied on top of the Makeup control, so lowering the threshold or raising the ratio produces obvious compression without simply lowering the level. Defaults are −24 dB threshold, 8:1 ratio, 5 ms attack and 120 ms release.

### EQs

Both Pre-EQ and Post-EQ use three bands: low shelf, mid peak, high shelf with frequency, gain and Q.

### IR

Four independent slots, each with:

- loaded IR filename,
- **BROWSE** and previous/next arrows,
- mix,
- pan,
- phase invert.

The slots are summed with automatic level compensation. One active IR at 100 % is unity gain; two active IRs contribute 50 % each; three contribute 33 % each; four contribute 25 % each. This keeps the cabinet level consistent as slots are enabled. The default IR mix is 100 % wet; the IR section has its own bypass.

### Post FX

- Chorus: rate, depth, feedback, delay, mix, bypass.
- Flanger: rate, depth, manual, feedback, mix, bypass.
- Phaser: rate, depth, feedback, stages, mix, bypass.
- Room: **Size** in the Post FX pedal; **Room Mix** in the bottom strip. This is a convolution room, not a long reverb: Size crossfades between generated small and large room impulse responses, and Room Mix sets the amount of room signal. Room Mix defaults to 0 %.
- Delay: mix, time, feedback, bypass.
- Reverb: mix, decay, width, bypass.
- Master **Output** is in the bottom strip beside the tabs.

Use **Room Mix** in the bottom strip to add ambience; there is no separate room bypass. Switching the delay off discards its feedback tail.

### Reduced Motion

The header toggle disables the panel hover-glow animation for users who prefer reduced motion. It is saved with the plugin state.

### Presets

Use the **PRESET** button in the header:

- **Save Preset...** writes a `.namberrypreset` file containing every parameter value plus the current amp, pedal and four IR paths.
- **Load Preset...** restores the parameters and loads any referenced model/IR files that still exist.
- **Open Preset Folder** opens the default preset directory.

The default preset folder is `NAMBerry/Presets` under the user application-data directory. The last-used preset folder is remembered. If a preset references a missing model or IR, the parameters are applied and NAMberry warns that one or more files could not be loaded.

### State and settings

- DAW projects save all parameter values with the plugin.
- The per-user settings file saves:
  - NAM root,
  - IR root,
  - last amp model path,
  - last pedal model path,
  - last IR path for each of the four slots,
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
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --pitch-blocks
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --detected-stage
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --dry-center
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --delay-reset
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --room
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --modulation
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --fx-state
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --preset
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --compressor
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --impersonator
./build/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest --ir-level
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
- `--pitch`: Signalsmith Stretch pitch accuracy and dry/bypass behaviour.
- `--pitch-blocks`: Stretch at 128, 192, 256 and 512-sample host blocks.
- `--detected-stage`: detector → Stretch pitch stage.
- `--dry-center`: verifies both channels are identical when fully dry.
- `--delay-reset`: verifies the delay tail is discarded on bypass.
- `--room`: verifies the convolution room, IR decay, and dry 0 % mix.
- `--modulation`: verifies finite chorus, flanger and phaser output, all phaser stage counts, and bypass.
- `--fx-state`: verifies migration of old post-FX parameter ranges/defaults.
- `--preset`: verifies preset save/load, including amp/IR file paths.
- `--compressor`: verifies gain reduction, automatic makeup gain and bypass.
- `--impersonator`: verifies both pickup-direction EQ curves, manual Level and bypass.
- `--ir-level`: verifies equal output level with 1, 2, 3 and 4 loaded IRs at 100 %.
- `--reference`: verifies 432/440 Hz reference behaviour.
- `--persist`: restores selected amp, pedal and IR paths; expects local sample files and skips if they are missing.
- `--dirs`: sets the two root directories.
- `<model.nam>`: full processor with a NAM model.

### UI snapshots

```bash
./build/NAMBerryUiSnapshot_artefacts/Release/NAMBerryUiSnapshot /tmp/namberry-ui
```

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

- **50 ms pitch latency is reported only while pitch shifting is active**. Bypassed, 0-semitone or 0 % mix states report 0 ms and pass the dry signal immediately; the host latency changes when the pitch stage is toggled.
- The **Standalone app** does not apply host-style plugin delay compensation; the 50 ms latency while pitch shifting is audible there. DAWs compensate it.
- Signalsmith Stretch is an STFT shifter and cannot meet the previous 5 ms target. That is the deliberate quality/latency trade-off.
- The **Impersonator** is an EQ approximation. It cannot reproduce the physical pick-attack geometry or the full phase relationship between pickup positions.
- NAM model latency is not separately reported by the plugin; only the pitch-stage latency is reported.
- The **Room** is a convolution ambience with a short controlled decay, not a long reverb; use the Reverb effect for a long tail.
- The plugin is **stereo in / stereo out only**.
- Model and IR paths are **per-user settings**, not embedded in the DAW project. Moving a project to another machine may require reloading them.
- `ui-spec.md` is an earlier Qt/QML exploration; the shipped UI is JUCE-native.
- No NAM models or IR files are included. Use your own `.nam` captures and WAV/AIFF IRs.
- The repository contains no project `LICENSE` file yet; add one before redistributing.

---

## Third-party licences

- **JUCE 8.0.6** — see `JUCE/LICENSE.md`; JUCE is available under AGPLv3 or a commercial licence.
- **NeuralAudio** — MIT (Mike Oliphant). Its submodules (NeuralAmpModelerCore, RTNeural, etc.) retain their own licences.
- **Signalsmith Stretch / Linear** — MIT (Signalsmith Audio).
- NAM model files and IR captures are supplied by the user and are not part of this repository.

Check the relevant upstream repositories before distributing binaries.

---

## Credits

- NAM / NeuralAmpModelerCore by Steven Atkinson and contributors.
- NeuralAudio by Mike Oliphant.
- JUCE by Raw Material Software.
- Signalsmith Stretch/Linear by Geraint Luff / Signalsmith Audio.
- The guitar community who create and share NAM captures and IRs.
