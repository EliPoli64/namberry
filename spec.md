# NAMBerry: AI-Agent Development Plan

## 1. Project Overview

**NAMBerry** is a universal audio plugin that builds upon the foundation of NAM Universal but introduces a more comprehensive and better-implemented signal chain. The goal is to provide guitarists and producers with an all-in-one "amp-in-a-box" solution that rivals dedicated hardware.

### Key Differentiators from NAM Universal
- **Expanded FX Chain**: Includes a full suite of studio-quality effects (Compressor, Overdrive, dual multi-band EQs, Chorus/Phaser/Flanger, Room, Delay, Reverb) integrated directly into the plugin, rather than relying on external plugins for basic tone shaping.
- **4-Slot IR Loader with Advanced Blending**: Goes beyond the dual-IR loader of NAM Universal to allow complex cabinet mixing with individual level, pan, and phase controls.
- **Enhanced Amp Control**: Dedicated controls for Gain, Presence, Depth, and Output Volume, providing a more tactile "amp-like" feel compared to the OOMPH/SPARKLE impedance interaction model.
- **True Static Linking**: Compiled with static libraries for Linux, Windows, and macOS to ensure maximum compatibility and ease of distribution without runtime dependency issues.

---

## 2. Technical Stack

| Component | Technology | Rationale |
| :--- | :--- | :--- |
| **Framework** | **JUCE** (C++17 or later) | Industry standard for cross-platform audio plugins; supports VST3, AU, AAX, and Standalone formats on Windows, macOS, and Linux. |
| **NAM Inference** | **NeuralAmpModelerCore** or **NeuralAudio** | `NeuralAmpModelerCore` is the official high-performance DSP library for running `.nam` models (WaveNet, LSTM, ConvNet, Linear). `NeuralAudio` is an alternative that offers A1/A2 support with a simpler API and MIT license. |
| **Build System** | **CMake** (v3.22+) | Enables reproducible cross-platform builds and static linking of JUCE modules and third-party dependencies. |
| **IR Loading** | **JUCE `dsp::Convolution`** | Built-in JUCE module for zero-latency, real-time convolution with support for loading WAV/AIFF files. |
| **Drop Tuner / Pitch Stage** | **Signalsmith Stretch** | The tuner detects the fundamental for display; one shared −12…+12 semitone Stretch stage handles capo and drop tuning with a mix control. Stretch uses a 50 ms block and reports that latency while active, zero when inactive. |
| **UI** | **JUCE `Component`** / **Visage** (optional) | JUCE’s native UI components are sufficient; Visage can be used for GPU-accelerated custom rendering if needed. |

---

## 3. System Architecture

### 3.1 Signal Flow (Stereo Path)

```
Input → Tuner (detect) → Impersonator (pickup shaper) → Pitch (capo/drop) → Noise Gate → Compressor → NAM Pedal/Preamp → Pre-Amp EQ →
NAM Amp Loader (Gain, Bass, Mid, Treble, Presence, Depth, Output) →
Post-Amp EQ (Multi-Band) → 4-Slot IR Loader (Blend/Pan/Phase) →
Post Effects (Chorus/Flanger/Phaser/Room/Delay/Reverb) → Output
```

### 3.2 UI Section Order

The editor should be organized into the following five sections, in this exact order:

1. Pre Effects
   - Tuner (detect; wide horizontal panel above Impersonator and Pitch)
   - Impersonator (pickup shaper)
   - Pitch (capo/drop)
   - Gate
   - Compressor
   - Pedal
   - Pre EQ
2. Amp
   - Gain
   - Bass
   - Mid
   - Treble
   - Presence
   - Depth
   - Output
3. Post Amp EQ
   - Multi-band EQ
4. IR
   - 4 slots with blend, pan, and phase controls
5. Post Effects
   - Chorus
   - Flanger
   - Phaser
   - Room (convolution room, not a long reverb)
   - Delay
   - Reverb

This order keeps the pedal before the amp, keeps all pre-amp shaping before the core amp model, and preserves the clear signal-flow layout the plugin UI should reflect.

A persistent **ROOM / MASTER** strip sits beside the bottom tab buttons and always exposes the Room Mix and master Output knobs.

### 3.2 Class Structure (High-Level)

- `NAMBerryProcessor` (AudioProcessor)
  - Manages the entire DSP chain.
  - Handles state save/load (presets).
  - Exposes parameters via `AudioProcessorValueTreeState`.
- `NAMBerryEditor` (AudioProcessorEditor)
  - Layouts all knobs, sliders, and file loaders.
- **DSP Modules** (Each as a separate class derived from `juce::dsp::ProcessorBase` or similar):
  - `NoiseGate`
  - `Compressor` (multi-band or single-band with sidechain)
  - `NamPedalLoader` (second NAM model for pedal/preamp captures — not a waveshaper)
  - `MultiBandEQ` (at least 3 bands: Low, Mid, High, with shelving/peaking options)
  - `NamModelLoader` (wraps `NeuralAmpModelerCore` / `NeuralAudio`)
  - `IrLoader` (4 slots, each with an `dsp::Convolution` instance)
  - `ModulationLfo` (shared sine/triangle LFO)
  - `ModulatedDelay` (chorus/flanger delay core with feedback)
  - `Phaser` (selectable 4/8/12-stage all-pass cascade)
  - `Room` (convolution ambience using generated small/large room IRs, with size and dry/wet mix; not a long reverb)
  - `Delay` (stereo, with feedback, mix, time)
  - `Reverb` (algorithmic or convolution-based)
  - `DropTuner` (fundamental frequency detection for the tuner display)
  - `Impersonator` (bridge/neck pickup-shaping preamp)
  - `PitchShifter` (Signalsmith Stretch wrapper)
  - `DigitalCapo` (shared semitone pitch stage)

---

## 4. Module Implementation Guide for the AI Agent

### 4.1 NAM Amp Loader
- **Goal**: Load and run `.nam` model files in real time.
- **Library Choice**: Use `NeuralAmpModelerCore` as it is the official implementation. If static linking proves complex due to Eigen dependencies, `NeuralAudio` is a viable alternative.
- **Steps**:
  1. Add the chosen library as a submodule/CMake `FetchContent`.
  2. Implement a `loadModel(const juce::File&)` method that uses the library’s loader (e.g., `nam::get_dsp(...)` or `NeuralModelLoader::CreateFromFile(...)`).
  3. In `processBlock`, feed audio buffers to the model’s `process` function.
  4. **Critical**: Handle buffer sizes. Ensure the model’s max buffer size is set correctly, or process in chunks if the DAW block size exceeds the model’s capacity.
- **Controls**: Gain (input drive), Presence (high-frequency emphasis), Depth (low-frequency resonance), Output (level).

### 4.2 IR Loader (4 Slots)
- **Goal**: Load up to 4 cabinet IRs and blend them seamlessly.
- **Implementation**:
  - Use four instances of `juce::dsp::Convolution`.
  - Each slot has its own file loader and a mix/level parameter (0–100%).
  - **Blending**: Normalize each IR on load, then normalize the summed cabinet bus by the number of active slots. One active IR at 100 % is unity; two contribute 50 % each; three 33 % each; four 25 % each, so adding slots does not increase overall level. Mix knobs remain relative amounts within that normalized bus.
  - **Smooth Switching**: When a user loads a new IR during playback, create the new `Convolution` object on a background thread and swap it in using a `ReferenceCountedObject` or a lock-free swap to prevent audio dropouts.

### 4.3 Multi-Band EQs (Pre and Post Amp)
- **Goal**: Provide tone shaping before and after the NAM model.
- **Implementation**: Build a `MultiBandEQ` class using JUCE’s `dsp::IIR` filters.
  - **Bands**: At least 3 bands per EQ (Low Shelf, Peak (Mid), High Shelf).
  - **Parameters**: Frequency (20 Hz–20 kHz), Gain (-24 dB to +24 dB), Q (0.1–10).
  - Instantiate two separate `MultiBandEQ` objects: one placed before the NAM loader, one after the IR loader.

### 4.4 Compressor
- **Goal**: Provide dynamics control, ideally with a "Tight" mode for palm mutes (as seen in NAM Universal’s Aggravator).
- **Implementation**:
  - Use a feed-forward compressor with attack, release, threshold, ratio, and makeup gain.
  - Apply a modest automatic makeup gain derived from the threshold and ratio, in addition to the user Makeup control, so compression is audible without a distracting level drop.
  - Defaults: −24 dB threshold, 8:1 ratio, 5 ms attack, 120 ms release, 0 dB user makeup.
  - For the "Tight" mode, implement a low-band sidechain filter (e.g., band-pass around 80–150 Hz) so the compressor is triggered primarily by low-end transients.

### 4.5 Pedal / Preamp (NAM Model) & Overdrive
- **Goal**: Add a warm, pedal-like drive before the amp — implemented as a **NAM model**, not a generic waveshaper.
- **Implementation**:
  - The pedal/preamp stage is a **second `NamModelLoader` instance** that loads a `.nam` pedal or preamp capture (e.g., overdrive, distortion, boost, fuzz).
  - Exposes Gain, Bass, Mid, Treble, and Output controls plus a bypass footswitch, using the same tone-stack implementation as the amp loader.
  - Placed before the NAM Amp Loader per the signal flow (and bypassable when no pedal model is loaded).
  - A built-in `std::tanh` waveshaper is **not** used for this stage: tone and character come entirely from the loaded NAM model.

### 4.6 Modulation (Chorus/Flanger/Phaser)
- **Goal**: Provide three independent, footswitchable modulation pedals with the parameter ranges from `fx-spec.md`.
- **Implementation**:
  - A shared `ModulationLfo` class provides sine and triangle modulation with smoothed rate and optional per-channel phase offset.
  - **Chorus**: A stereo modulated delay using a sine LFO with the right channel offset by 180°. Controls: Rate 0.1–5 Hz (0.8), Depth 0–100 % (50), Feedback 0–70 % (15), Delay 5–30 ms (15), Mix 0–100 % (50).
  - **Flanger**: A shorter stereo delay using a triangle LFO and a signed feedback path. Controls: Rate 0.05–10 Hz (0.4), Depth 0–100 % (60), Manual 0.5–10 ms (2), Feedback −95…+95 % (40), Mix 0–100 % (50).
  - **Phaser**: Cascaded first-order all-pass stages with a sine LFO sweeping around 600 Hz by up to ±1.5 octaves at full depth. Controls: Rate 0.05–8 Hz (0.5), Depth 0–100 % (70), Feedback 0–90 % (30), Stages 4/8/12 (8), Mix 0–100 % (50).
  - Chorus and flanger use fractional delay lines with feedback and linear dry/wet mixing; the phaser uses 4, 8 or 12 all-pass stages and feedback.
  - Each effect has its own bypass footswitch and resets its delay/filter state when bypassed.

### 4.7 Delay
- **Goal**: Stereo delay with musical controls.
- **Implementation**: Circular buffer delay line with parameters: Time (1 ms–2 s), Feedback (0–95%), Mix (0–100%), and optionally a high-cut filter in the feedback path.

### 4.8 Room ambience
- **Goal**: Add a realistic sense of physical space and depth without turning the tone into a long reverb.
- **Implementation**: A convolution `Room` stage placed after the modulation effects and before the delay.
  - Two short stereo room impulse responses are generated at prepare time: a small room and a larger room. Each contains early reflections and a damped diffuse tail.
  - **Size** crossfades between the small and large room responses; **Room Mix** crossfades the dry signal with the convolution output.
  - The wet path has a fixed gain boost so the room is clearly audible at modest Mix settings.
  - Room Mix defaults to **0 %**; at zero the convolution is skipped and the stage is completely dry. There is no separate room bypass.
  - The generated IRs use independent stereo noise, early reflection spread and a fixed damping filter, avoiding the obvious echo pattern of a simple delay network.
- **Controls**: Size lives in the Post FX Room pedal; **Room Mix** lives in the persistent bottom strip beside the tabs.

### 4.9 Reverb
- **Goal**: Add ambience.
- **Implementation**: Use JUCE's `juce::Reverb` (algorithmic) for a lightweight, real-time-safe reverb. Parameters: Room Size, Damping, Wet, Dry, Width.

### 4.10 DropTuner
- **Goal**: Detect the input pitch for display.
- **Implementation**:
  - Autocorrelation-based fundamental frequency detection on the input signal.
  - Uses a long 80 ms analysis window, independent of the host block size, with decimation, first-peak selection, parabolic interpolation and median smoothing.
  - Detects down to ~80 Hz reliably (low E and drop tunings).
  - Displays a **horizontal strobe tuning guide**: moving bands and a needle follow the cents deviation, turn green when in tune, and show the note and cents.
  - **Reference frequency**: 400–480 Hz, default 440 Hz; note/cents calculations use this reference.
  - Detection only: the drop-tune shift is performed by the shared Pitch stage; the tuner has no audio path, offset, or bypass.
  - Runs before the pitch stage so it analyses the clean input.
- **Controls**: Horizontal tuning guide (note + cents), reference frequency field (Hz). The tuner panel sits above the Impersonator and Pitch pedals in the Pre FX tab.

### 4.11 Pitch stage (Digital Capo + Drop Tune)
- **Goal**: Transpose the guitar by a selected number of semitones, covering both capo (up) and drop-tuning (down) use.
- **Implementation**:
  - **Semitones**: a single −12…+12 control shared by capo and drop tuning.
  - **Mix/Dry-Wet**: blends the dry signal with the transposed signal (default 100% wet); the dry signal is delayed only while pitch shifting is active.
  - **Signalsmith Stretch**: a 50 ms STFT block with a 7 ms hop; no engine toggle and no automatic fallback.
  - While active, the stage reports its Stretch latency to the host via `setLatencySamples()`; while inactive it reports 0.
  - While active, the dry path is delayed by the Stretch latency so the blend stays time-aligned, and it is mono-summed so it is always panned dead centre. When inactive, the dry signal passes immediately.
  - The stage runs at the head of the chain (after the tuner detector, before the gate/compressor), so every downstream stage sees the transposed signal.
  - Stretch processing is skipped while the stage is inactive; bypassed, 0-semitone or 0% mix states output immediate dry signal.
- **Controls**: Semitones (−12…+12), mix level (0–100%), bypass footswitch.

### 4.12 Preset save/load
- **Goal**: Let users store and recall the complete rig without depending on the host’s project state.
- **Implementation**:
  - Presets use a `.namberrypreset` XML file with a `PARAMETERS` child containing the full `AudioProcessorValueTreeState` and a `FILES` child containing the current amp, pedal and four IR paths.
  - Loading applies the parameter state first, then loads any referenced model/IR files that still exist.
  - Missing files do not prevent the parameter state from being applied; the UI reports that the preset was only partially restored.
  - The default preset folder is `NAMBerry/Presets` in the user application-data directory; the last-used folder is remembered in `NAMBerry.settings`.
  - The editor’s header exposes a **PRESET** menu with Load, Save and Open Preset Folder actions.
- **Controls**: PRESET header menu (Load Preset, Save Preset, Open Preset Folder).

### 4.13 Impersonator pickup shaper
- **Goal**: Approximate the tonal shift between bridge and neck pickup positions before the pitch/amp chain.
- **Placement**: Immediately after the tuner detector and before the pitch stage.
- **Implementation**: Two fixed IIR filter chains with a short crossfade when the direction changes.
  - **Bridge → Neck**: 80 Hz high-pass, +4.5 dB low shelf at 180 Hz, +2 dB peak at 450 Hz, −4 dB peak at 3 kHz, −8 dB high shelf at 4 kHz, 5 kHz low-pass.
  - **Neck → Bridge**: 130 Hz high-pass, −5 dB peak at 400 Hz, +4 dB peak at 3 kHz, +3 dB high shelf at 5.5 kHz.
  - **Level**: manual ±12 dB output trim, default 0 dB. No automatic makeup boost.
  - The pedal starts bypassed; the bypass footswitch resets the filter state.
  - The effect is an EQ approximation, not a physical pickup/phase model.
- **Controls**: B → N / N → B switch, Level, bypass footswitch.

---

---

## 5. Cross-Platform Build & Static Linking

### 5.1 CMake Configuration
Create a root `CMakeLists.txt` that:
1. Sets `CMAKE_CXX_STANDARD 17`.
2. Adds JUCE via `add_subdirectory(JUCE)`.
3. Uses `juce_add_plugin` with `VST3`, `AU`, `AAX`, and `Standalone` formats.
4. **Static Linking of NAM Library**:
   - If using `NeuralAmpModelerCore`, ensure it is built as a static library (`STATIC` in CMake).
   - Link it privately to the plugin target: `target_link_libraries(NAMBerry PRIVATE NeuralAmpModelerCore)`.
   - **Eigen Alignment**: Apply the preprocessor macros `EIGEN_MAX_ALIGN_BYTES 0` and `EIGEN_DONT_VECTORIZE` if memory alignment issues arise, as noted in the NAM Core README.
5. **JUCE Modules Static Linking**: By default, JUCE modules are compiled into the plugin binary. No special action is needed for static linking of JUCE itself.

### 5.2 Platform-Specific Considerations
- **Windows**: Use MSVC + Ninja. Ensure the NAM library is compiled with the same runtime library (`/MT` for static CRT if desired).
- **macOS**: Use Xcode or Ninja. Code signing and notarization are required for distribution; configure in CMake via `juce_add_plugin` properties.
- **Linux**: Use Clang + Ninja. Build the VST3 target. Static linking of `libstdc++` and `libgcc` may be necessary for distribution (`-static-libstdc++ -static-libgcc`).

### 5.3 CI/CD (GitHub Actions)
Set up a matrix build for `ubuntu-latest`, `windows-latest`, and `macos-latest`. Each job runs:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Artifacts: `.vst3` (all), `.component` (AU, macOS), `.aaxplugin` (if SDK available), and Standalone executables.

---

## 6. Implementation Milestones (Agent Task List)

### Phase 1: Project Skeleton & NAM Core (Weeks 1–2)
- [x] Initialize CMake project with JUCE and `NeuralAudio` as the NAM backend.
- [x] Implement `NamModelLoader` class: load `.nam` files, process audio, expose gain/output.
- [x] Create basic `AudioProcessor` that passes audio through the NAM model.
- [ ] Verify real-time performance on all three platforms.

### Phase 2: IR Loader & EQs (Weeks 3–4)
- [x] Implement 4-slot `IrLoader` with `dsp::Convolution` and per-slot mix.
- [x] Build `MultiBandEQ` class and instantiate two (pre and post).
- [x] Integrate into signal chain.

### Phase 3: Dynamics & Drive (Weeks 5–6)
- [x] Implement `Compressor` (with optional low-band sidechain).
- [ ] Implement `Overdrive` (waveshaper + tone) — superseded by the NAM pedal design.
- [x] Implement `NoiseGate`.

### Phase 4: Time-Based & Modulation FX (Weeks 7–8)
- [x] Implement shared `ModulationLfo` and `ModulatedDelay` chorus/flanger.
- [x] Implement 4/8/12-stage all-pass `Phaser`.
- [x] Implement `Delay` (circular buffer).
- [x] Integrate `juce::Reverb`.
- [x] Implement convolution Room ambience (Size, Room Mix; default 0 %).

### Phase 5: UI & State Management (Weeks 9–10)
- [x] Design `NAMBerryEditor` with tabs or a scrollable rack layout.
- [x] Bind all parameters to `AudioProcessorValueTreeState`.
- [x] Implement preset save/load.
- [x] Add file drag-and-drop for `.nam` and `.wav` IR files.
- [x] Add in-app directory browsers with separate persisted NAM and IR roots.
- [x] Add previous/next arrows to the NAM and IR fields to step through matching files in the current directory.
- [x] Persist the selected amp model, pedal model and all four IR slot files.
- [x] Clear the delay feedback tail when the delay is switched off.

### Phase 6: Build, Test, & Release (Weeks 11–12)
- [ ] Finalize CMake static linking flags for all platforms.
- [ ] Run `pluginval` for validation.
- [ ] Set up GitHub Actions CI for automated builds.
- [ ] Write user manual and package installers.

---

## 7. Potential Risks & Mitigations

| Risk | Mitigation |
| :--- | :--- |
| **Eigen alignment issues** in NAM Core causing crashes on certain compilers. | Apply `EIGEN_MAX_ALIGN_BYTES 0` and `EIGEN_DONT_VECTORIZE` macros as recommended, or use `NeuralAudio` which may handle this more gracefully. |
| **Real-time safety** violations (memory allocation on audio thread). | Pre-allocate all buffers in `prepareToPlay`. Load NAM models and IRs on a background thread and swap them using atomic pointers. |
| **Static linking complexity** for macOS (AU) and Windows (AAX). | Start with VST3 and Standalone (which have fewer restrictions). Gradually add AU/AAX once the core is stable. |
| **IR phase cancellation** when blending multiple cabinets. | Normalize IRs to the same length and apply a phase-alignment check (e.g., cross-correlation) during load, or provide a phase invert toggle per slot. |

---

## 8. References & Resources

- **JUCE Cross-Platform Starter**: `danielraffel/JUCE-Plugin-Starter` provides a CMake-based template for macOS, Windows, and Linux builds.
- **NeuralAmpModelerCore**: Official C++ DSP library for NAM, supporting WaveNet, LSTM, ConvNet, and Linear models.
- **NeuralAudio**: Alternative MIT-licensed library with a simpler API and A1/A2 support.
- **NAM Universal Feature Reference**: Details on the Aggravator module (Tight/Smooth), Instigator tonestack, and dual-IR loader.
- **JUCE IR Loader Techniques**: Using `dsp::Convolution` with `DryWetMixer` for smooth blending and background loading.
