# NAMBerry: AI-Agent Development Plan

## 1. Project Overview

**NAMBerry** is a universal audio plugin that builds upon the foundation of NAM Universal but introduces a more comprehensive and better-implemented signal chain. The goal is to provide guitarists and producers with an all-in-one "amp-in-a-box" solution that rivals dedicated hardware.

### Key Differentiators from NAM Universal
- **Expanded FX Chain**: Includes a full suite of studio-quality effects (Compressor, Overdrive, dual multi-band EQs, Chorus/Phaser/Flanger, Delay, Reverb) integrated directly into the plugin, rather than relying on external plugins for basic tone shaping.
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
| **Drop Tuner / Digital Capo** | **JUCE `dsp::Process`** + custom pitch detection/transposition | Drop tuner detects fundamental frequency and provides semi-tone tuning down control; Digital capo simulates a guitar capo by transposing pitch up by specified fret count. Both are placed early/mid-chain for correct signal flow. |
| **UI** | **JUCE `Component`** / **Visage** (optional) | JUCE’s native UI components are sufficient; Visage can be used for GPU-accelerated custom rendering if needed. |

---

## 3. System Architecture

### 3.1 Signal Flow (Stereo Path)

```
Input → Input Gain → DropTuner → Noise Gate → Compressor → Overdrive →
Pre-Amp EQ (Multi-Band) → DigitalCapo → NAM Amp Loader (with Gain/Presence/Depth) →
NAM Pedal/Preamp Loader (Optional) →
4-Slot IR Loader (Blend/Mix) →
Post-Amp EQ (Multi-Band) →
Chorus/Phaser/Flanger → Delay → Reverb →
Output Volume → Output
```

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
  - `Overdrive` (waveshaping with tone control)
  - `MultiBandEQ` (at least 3 bands: Low, Mid, High, with shelving/peaking options)
  - `NamModelLoader` (wraps `NeuralAmpModelerCore` / `NeuralAudio`)
  - `IrLoader` (4 slots, each with an `dsp::Convolution` instance)
  - `ModulationFX` (Chorus/Phaser/Flanger, selectable or mixable)
  - `Delay` (stereo, with feedback, mix, time)
  - `Reverb` (algorithmic or convolution-based)
  - `DropTuner` (polyphonic/monophonic pitch detection, tuning down control)
  - `DigitalCapo` (fret position control, pitch transposition)

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
  - **Blending**: To avoid phase issues, ensure all IRs are normalized to the same length/sample rate upon load. Use a `DryWetMixer` per slot or a final summing bus with individual gains.
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
  - For the "Tight" mode, implement a low-band sidechain filter (e.g., band-pass around 80–150 Hz) so the compressor is triggered primarily by low-end transients.

### 4.5 Overdrive
- **Goal**: Add a warm, pedal-like drive before the amp.
- **Implementation**: Use a simple non-linear waveshaper (e.g., `std::tanh` or a diode curve) followed by a tone control (low-pass filter) and a level control.

### 4.6 Modulation (Chorus/Phaser/Flanger)
- **Goal**: Provide classic modulation effects.
- **Implementation**:
  - **Chorus**: Short modulated delay (5–30 ms) mixed with dry signal.
  - **Flanger**: Shorter modulated delay (0.5–10 ms) with feedback.
  - **Phaser**: Cascaded all-pass filters with LFO-modulated corner frequencies.
  - Use a single `ModulationFX` class with a `Type` parameter to switch between them, or allow mixing.

### 4.7 Delay
- **Goal**: Stereo delay with musical controls.
- **Implementation**: Circular buffer delay line with parameters: Time (1 ms–2 s), Feedback (0–95%), Mix (0–100%), and optionally a high-cut filter in the feedback path.

### 4.8 Reverb
- **Goal**: Add ambience.
- **Implementation**: Use JUCE's `juce::Reverb` (algorithmic) for a lightweight, real-time-safe reverb. Parameters: Room Size, Damping, Wet, Dry, Width.

### 4.9 DropTuner
- **Goal**: Detect input pitch and provide tuning-down control for guitar drop tunings (e.g., dropped D, dropped C#).
- **Implementation**:
  - Uses FFT-based fundamental frequency detection on the input signal.
  - Displays the detected note name (e.g., "E2", "A4") and frequency in Hz.
  - **Tuning Offset**: A control parameter (in semitones, typically 0 to -12) that transposes the output frequency down by the selected amount.
  - **True Bypass**: A switch that bypasses the pitch detection entirely, passing the input signal through unchanged.
  - The tuner is placed at the start of the signal chain (after Input Gain) so it receives a clean, unprocessed signal for accurate note detection.
  - The tuning offset can be automated via the host for real-time tuning changes during performance.
- **Controls**: Detected note name, detected frequency, tuning offset (semitones), bypass switch.

### 4.10 DigitalCapo
- **Goal**: Simulate a guitar capo by transposing the pitch up by the selected number of frets.
- **Implementation**:
  - **Fret Position**: A parameter ranging from 0 (no capo) to 12 (full octave capo). Each fret increases the pitch by one semitone.
  - **Mix/Dry-Wet**: Controls the blend between the original (dry) signal and the transposed (wet) signal, allowing users to mix the capo effect subtly or fully.
  - The digital capo is placed after the Pre-Amp EQ but before the NAM Amp Loader, so the amp model receives the capo-transposed signal—modeling the amp at the raised pitch, which is the musically correct behavior for capo usage.
  - The mix parameter allows users to blend the capo effect with the dry signal for creative tonal options.
- **Controls**: Fret position (0-12), mix level (0-100%).

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
- [ ] Initialize CMake project with JUCE and `NeuralAmpModelerCore` as static library.
- [ ] Implement `NamModelLoader` class: load `.nam` files, process audio, expose gain/output.
- [ ] Create basic `AudioProcessor` that passes audio through the NAM model.
- [ ] Verify real-time performance on all three platforms.

### Phase 2: IR Loader & EQs (Weeks 3–4)
- [ ] Implement 4-slot `IrLoader` with `dsp::Convolution` and per-slot mix.
- [ ] Build `MultiBandEQ` class and instantiate two (pre and post).
- [ ] Integrate into signal chain.

### Phase 3: Dynamics & Drive (Weeks 5–6)
- [ ] Implement `Compressor` (with optional low-band sidechain).
- [ ] Implement `Overdrive` (waveshaper + tone).
- [ ] Implement `NoiseGate`.

### Phase 4: Time-Based & Modulation FX (Weeks 7–8)
- [ ] Implement `ModulationFX` (Chorus/Phaser/Flanger).
- [ ] Implement `Delay` (circular buffer).
- [ ] Integrate `juce::Reverb`.

### Phase 5: UI & State Management (Weeks 9–10)
- [ ] Design `NAMBerryEditor` with tabs or a scrollable rack layout.
- [ ] Bind all parameters to `AudioProcessorValueTreeState`.
- [ ] Implement preset save/load.
- [ ] Add file drag-and-drop for `.nam` and `.wav` IR files.

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
