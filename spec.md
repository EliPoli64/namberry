# NAMBerry: AI-Agent Development Plan

## 1. Project Overview

**NAMBerry** is a universal audio plugin that builds upon the foundation of NAM Universal but introduces a more comprehensive and better-implemented signal chain. The goal is to provide guitarists and producers with an all-in-one "amp-in-a-box" solution that rivals dedicated hardware.

### Key Differentiators from NAM Universal
- **Expanded FX Chain**: Includes a full suite of studio-quality effects (Compressor, Overdrive, 3-band pre-EQ, 8-band graphical post-EQ, Chorus/Phaser/Flanger, Room, Delay, Reverb) integrated directly into the plugin, rather than relying on external plugins for basic tone shaping.
- **2-IR Loader with one Blend knob**: the same dual-cabinet idea as NAM Universal's loader, reduced to a single crossfading control instead of per-slot level/pan/phase mixers.
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
| **Drop Tuner / Pitch Stage** | **Rubber Band / Signalsmith Stretch** | The tuner detects the fundamental for display; one shared −12…+12 semitone stage handles capo and drop tuning with a mix control. Rubber Band is used when available at runtime, with a self-contained Stretch fallback. Both engines use a fixed-size streaming wrapper and report a measured active latency, zero when inactive. |
| **UI** | **JUCE `Component`** / **Visage** (optional) | JUCE’s native UI components are sufficient; Visage can be used for GPU-accelerated custom rendering if needed. |

---

## 3. System Architecture

### 3.1 Signal Flow (Stereo Path)

```
Input → Input Gain (−24…+24 dB, default 0 dB) → Tuner (detect) → Impersonator (pickup shaper) → Pitch (capo/drop) → Noise Gate → Acoustic Sim → Compressor → NAM Pedal/Preamp → Pre-Amp EQ →
NAM Amp Loader (Gain, Bass, Mid, Treble, Presence, Depth, Output) →
Post-Amp EQ (8-band graphical) → IR Loader (IR 1 / IR 2 + Blend) →
Post Effects (Chorus/Flanger/Phaser/Delay/Reverb) → Reverb IR (two parallel stereo convolution slots with independent Mix, tail Cutoff and a local wet boost/cut Master) → Room (short dark ambience) → Master (+6 dB fixed trim) → Output
```

### 3.2 UI Section Order

The editor should be organized into the following five sections, in this exact order:

1. Pre Effects (the tuner stays in the editor header on every tab)
   - Impersonator (pickup shaper)
   - Pitch (capo/drop)
   - Gate
   - Acoustic Sim
   - Compressor
   - Pedal
   - Pre EQ (3 bands plus a Volume trim)
2. Amp
   - Tap-to-list model LCD (opens the folder popup), **LOAD** (system file browser) and arrows
   - Gain
   - Bass
   - Mid
   - Treble
   - Presence
   - Depth
   - Output
3. Post Amp EQ
   - Full-width, full-height graphical 8-band EQ
   - Draggable nodes for frequency and gain, mouse wheel for Q
   - Focused-band Boost and Q knobs that retarget to the selected band; the Q knob also follows the selected HPF/LPF
   - Adjustable high-pass and low-pass filters with draggable cutoff handles
   - Live magnitude spectrum of the post-EQ signal drawn behind the response curve
   - Flat and bypass controls
4. IR
   - 2 IR rows (filename screen, **LOAD**, arrows), one Blend knob, section bypass
5. Post Effects
   - Chorus
   - Flanger
   - Phaser
   - Delay
   - Reverb
   - Reverb IR (two long IR filename rows + L/R Mix and Cutoff, accented Mix knobs, local boost/cut Master, styled as a footswitchable pedal)

This order keeps the pedal before the amp, keeps all pre-amp shaping before the core amp model, and preserves the clear signal-flow layout the plugin UI should reflect.

Each tab uses a hardware icon (pedal, amp, EQ, speaker, sparkle) with a small label. Right-click any tab to bypass that section; the icon dims and shows a red LED while bypassed.

A persistent bottom strip groups **Input Gain** (−24…+24 dB, default 0 dB), the tab buttons and **Room** / **Master** (master Output) into one centred row, with the knobs sitting immediately beside the tab bar. The tab buttons are a fixed, generous size with a larger icon and label.

### 3.2 Class Structure (High-Level)

- `NAMBerryProcessor` (AudioProcessor)
  - Manages the entire DSP chain.
  - Handles state save/load (presets).
  - Exposes parameters via `AudioProcessorValueTreeState`.
- `NAMBerryEditor` (AudioProcessorEditor)
  - Layouts all knobs, sliders, and file loaders.
- **DSP Modules** (Each as a separate class derived from `juce::dsp::ProcessorBase` or similar):
  - `NoiseGate` (default release 20 ms; threshold, attack and bypass are also exposed)
  - `Compressor` (multi-band or single-band with sidechain)
  - `NamPedalLoader` (second NAM model for pedal/preamp captures — not a waveshaper)
  - `MultiBandEQ` (3-band Low/Mid/High pre-amp EQ)
  - `PostAmpEQ` (8-band graphical post-amp EQ plus high-pass/low-pass)
  - `SignalSpectrum` (lock-free post-EQ signal tap: audio thread copies samples into a FIFO, the editor runs the FFT and draws the live magnitude trace)
  - `NamModelLoader` (wraps `NeuralAmpModelerCore` / `NeuralAudio`)
  - `IrLoader` (2 slots, each with an `dsp::Convolution` instance, blended by one crossfade)
  - `ReverbIrLoader` (two parallel `dsp::Convolution` Post FX slots, each with a smoothed wet Mix and a message-thread tail Cutoff that trims the loaded IR)
  - `ModulationLfo` (shared sine/triangle LFO)
  - `ChorusEnsemble` (three-voice stereo chorus) and `ModulatedDelay` (flanger delay core with feedback)
  - `Phaser` (selectable 4/8/12-stage all-pass cascade)
  - `RoomSim` (two short, offset filtered delays with low feedback and a single amount control)
  - `Delay` (ping-pong stereo or centred mono, with feedback, mix, time)
  - `Reverb` (algorithmic or convolution-based)
  - `DropTuner` (fundamental frequency detection for the tuner display)
  - `Impersonator` (bridge/neck pickup-shaping preamp)
  - `PitchShifter` (Rubber Band / Signalsmith streaming pitch engines)
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

### 4.2 IR Loader (2 IRs + Blend)
- **Goal**: Load two cabinet IRs and crossfade between them with one knob.
- **Implementation**:
  - Use two instances of `juce::dsp::Convolution`.
  - Each row owns its filename screen (tap → folder popup), **LOAD** button and previous/next arrows. There are no per-slot mix, pan or phase controls; Blend is the only knob on the tab.
  - **Blending**: the `irBlend` parameter (0–100 %) sets gains `1 − blend` and `blend`, which sum to unity, so the cabinet level stays constant across the range. With only one IR loaded that IR plays at unity; with none loaded the section passes the dry signal through.
  - **Smooth Switching**: IR loading stays asynchronous — the new IR is prepared off the audio thread and swapped in, so playback never drops out.
  - **State migration**: projects saved with the legacy four-slot model drop `irMix*`/`irPan*`/`irPhase*` and gain an `irBlend` derived from which slots were up (second slot alone → Blend 100 %, otherwise 0 %).

### 4.3 EQs (Pre and Post Amp)
- **Goal**: Provide tone shaping before and after the NAM model.
- **Pre-EQ**: `MultiBandEQ`, built from JUCE’s `dsp::IIR` filters.
  - Three bands: low shelf, mid peak, high shelf.
  - Frequency 20 Hz–20 kHz, gain −24…+24 dB, Q 0.1–10.
  - A footer **Volume** trim adds −12…+12 dB after the bands.
- **Post-EQ**: `PostAmpEQ`, also built from `dsp::IIR` biquads.
  - Eight bands with independent frequency, gain, Q and curve type. The type is chosen per band in the editor's focus strip: Low Pass (low shelf), High Pass (high shelf) or Band (peaking bell). Shelves are used for Low Pass/High Pass so the gain knob keeps its boost/cut meaning.
  - Defaults: band 0 Low Pass, bands 1–6 Band, band 7 High Pass.
  - Band frequency 20 Hz–20 kHz, gain −24…+24 dB, Q 0.1–10.
  - A 12 dB/octave high-pass (20 Hz–2 kHz) and low-pass (2–20 kHz), each with adjustable Q.
  - The editor draws the summed response and lets the user drag nodes and filter handles. The same static response helper drives the graph, so the display matches the audio filters.
  - Parameters: `postEqBand0Freq/Gain/Q/Type` … `postEqBand7Freq/Gain/Q/Type`, `postEqHpfFreq`, `postEqHpfQ`, `postEqLpfFreq`, `postEqLpfQ`, `postEqBypass`. The `Type` choice indices are `0 = Low Pass`, `1 = High Pass`, `2 = Band`.
  - State version 5 migrates the old three-band post-EQ: low → band 0, mid → band 1, high → band 7. The old HPF/LPF cutoff values carry over and their Q defaults to 0.707. State version 6 adds the per-band `Type` parameter with the legacy curve for each position, so existing projects keep their sound.
  - The processor caches raw Post EQ parameter pointers at construction; `processBlock` reads the atomics directly instead of building parameter-ID strings.

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
  - **Chorus**: A Boss-style three-voice stereo ensemble. One mono delay buffer is read at three taps with 0°/120°/240° LFO phases panned left/centre/right. Each voice sums a slow sine LFO (the Rate knob) with a faster shallow sine (6× Rate, clamped to 2–8 Hz). Controls: Rate 0.1–5 Hz (0.8), Depth 0–100 % (50), Feedback 0–70 % (15), Delay 5–30 ms (15), Mix 0–50 % (50).
  - **Flanger**: A shorter stereo delay using a triangle LFO and a signed feedback path. Controls: Rate 0.05–10 Hz (0.4), Depth 0–100 % (60), Manual 0.5–10 ms (2), Feedback −95…+95 % (40), Mix 0–50 % (50).
  - **Phaser**: Cascaded first-order all-pass stages with a sine LFO sweeping around 600 Hz by up to ±1.5 octaves at full depth. Controls: Rate 0.05–8 Hz (0.5), Depth 0–100 % (70), Feedback 0–90 % (30), Stages 4/8/12 (8), Mix 0–50 % (50).
  - The mix range ends at the old 50 % maximum; the wet path gets a 2× boost so maximum mix is both capped and louder. Chorus uses Lagrange3rd fractional delay taps with a 120 Hz high-pass, a 5.5 kHz low-pass and gentle saturation for a BBD-like wet voice; flanger uses a linear fractional delay line with feedback. The phaser uses 4, 8 or 12 all-pass stages and feedback.
  - Each effect has its own bypass footswitch and resets its delay/filter state when bypassed.

### 4.7 Delay
- **Goal**: Musical delay that can bounce echoes between stereo channels or keep them centred.
- **Implementation**: Two `juce::dsp::DelayLine` channels with Time (10 ms–2 s), Feedback (0–10, where 0 is one repeat and 10 is a near-endless tail), Mix (0–100%), and a Ping-Pong/Mono toggle (default Ping-Pong).
  - Ping-Pong sums the input to mono, feeds the left delay, then feeds the right delay from the left echo. The right echo is scaled by Feedback and returned to the left, so repeats alternate L/R once per feedback loop.
  - Mono feeds one delay from the summed input and copies the centred echo to both outputs. The dry signal stays stereo in both modes.
  - Switching mode or bypassing the delay clears both delay lines so stale cross-fed repeats cannot leak through.

### 4.8 Room
- **Goal**: Make a lone guitar sound bigger with short, audible stereo early reflections, without changing the dry level or touching the Reverb effect.
- **Implementation**: A `RoomSim` stage after `PostEffects` and before the master trim.
  - Input channels are summed to mono, then fed to two filtered feedback delays with 28 ms and 34 ms lengths. The offset produces a wide stereo image from the mono source.
  - Feedback is intentionally low: 3 % at minimum amount and 15 % at full amount. The delayed guitar is the dominant sound; repeats decay almost immediately rather than becoming a reverb tail.
  - The wet path is 120 Hz high-passed and 6 kHz low-passed so the reflections stay dark but remain audible. Maximum wet gain is 0.8 (about -2 dB). Dry stays at unity.
  - **Room** (0–100 %, default 0 %) fades the wet path and increases feedback. 0 % is an exact passthrough; stale delay state is cleared when the room is turned back on.
  - All delay lines and filters are allocated in `prepare()` and cleared by `reset()`; the parameter is read from a cached raw APVTS atomic on the audio thread.
  - The parameter is `roomAmount`. State version 8 migrates `imageMix` (or older `roomBlend`) to `roomAmount`, removes `imageWidth`, `imageMix`, `roomSize`, `roomBlend` and `roomBypass`, and adds `delayPingPong` with a default-on value for older states.
- **Controls**: Room lives in the persistent bottom strip, beside the master Output next to the tab bar; Reverb is unchanged. Input Gain sits on the other side of the same bar.

### 4.9 Reverb
- **Goal**: Add ambience.
- **Implementation**: Use JUCE's `juce::Reverb` (algorithmic) for a lightweight, real-time-safe reverb. Parameters: Room Size, Damping, Wet, Dry, Width. Compensate for JUCE's internal 2× dry and 3× wet scaling so the dry guitar stays at unity instead of getting louder when reverb is mixed in. The separate convolution-based `ReverbIrLoader` is described in §4.15.

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
- **Controls**: Horizontal tuning guide (note + cents), reference frequency field (Hz). The tuner has no panel in the tabs; it stays in the editor header on every tab.

### 4.11 Pitch stage (Digital Capo + Drop Tune)
- **Goal**: Transpose the guitar by a selected number of semitones, covering both capo (up) and drop-tuning (down) use.
- **Implementation**:
  - **Semitones**: a single −12…+12 control shared by capo and drop tuning.
  - **Tone Match**: auto-corrects the formant envelope to match the source guitar (0–100%). At 100% the original formants are held in place so detuning down does not darken the instrument; at 0% the formants shift naturally with the pitch. Blends continuously on both engines.
  - **Mix/Dry-Wet**: blends the dry signal with the transposed signal (default 100% wet); the dry signal is delayed only while pitch shifting is active.
  - **Quality mode**: Low Latency and Quality modes select different analysis windows/engines.
  - **Pitch engines**: a real-time Rubber Band engine is preferred when its runtime library is available, with a self-contained Signalsmith Stretch fallback. Both use fixed-size input/output FIFOs so arbitrary host block sizes and sample rates cannot reallocate or corrupt engine state. Low Latency and Quality modes are selectable.
  - While active, the stage reports its Stretch latency to the host via `setLatencySamples()`; while inactive it reports 0.
  - While active, the dry path is delayed by the Stretch latency so the blend stays time-aligned, and it is mono-summed so it is always panned dead centre. When inactive, the dry signal passes immediately.
  - The stage runs at the head of the chain (after the tuner detector, before the gate/compressor), so every downstream stage sees the transposed signal.
  - Stretch processing is skipped while the stage is inactive; bypassed, 0-semitone or 0% mix states output immediate dry signal.
- **Controls**: Semitones (−12…+12), Tone Match (0–100%), mix level (0–100%), bypass footswitch.

### 4.12 Preset save/load
- **Goal**: Let users store and recall the complete rig without depending on the host’s project state.
- **Implementation**:
  - Presets use a `.namberrypreset` XML file with a `PARAMETERS` child containing the full `AudioProcessorValueTreeState` and a `FILES` child containing the current amp, pedal, both cabinet IR paths and both reverb IR paths. A v9 preset with one `reverbIr` path loads into the left slot.
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

### 4.14 Acoustic Simulator
- **Goal**: Turn a magnetic-pickup electric guitar signal into a real steel-string acoustic character, with body support on the standard low E/A/D strings and a plucked attack rather than electric sustain.
- **Placement**: Immediately after the gate and before the compressor, so the sim shapes the raw pickup before dynamics.
- **Implementation**: Modal filter bank with four fixed IIR mode curves (Standard, Jumbo, Enhanced, Piezo) crossfaded with a smoothed weight vector, followed by three parallel sections whose mix gains are smoothed per sample so continuous automation stays click-free:
  - **Body**: resonances at 85 Hz (Q 2.2, +4.5 dB), 180 Hz (Q 1.8, +4 dB) and 360 Hz (Q 1.5, +3 dB) to excite low E (~82 Hz), A (~110 Hz) and D (~147 Hz); a −3.5 dB dip at 700 Hz removes electric honk and a −2 dB cut at 2.5 kHz tames pickup presence. The Body knob crossfades toward this curve.
  - **Top**: −2 dB at 2.4 kHz, +2 dB at 3.5 kHz, +5 dB high shelf at 5.5 kHz and +3 dB at 9 kHz. The Top knob crossfades toward it.
  - **Piezo**: +4 dB peak at 2.4 kHz (Q 1.3) plus a +3 dB high shelf at 4 kHz, with a mild `tanh` soft clipper on the 2–5 kHz band only (drive 1.5, unity-normalised). The Piezo knob crossfades toward it.
  - **Attack envelope**: one mono fast/slow envelope pair (1 ms attack, 70 ms fast release, 250 ms slow follow) derives a transient amount from the dry input. It briefly lifts the Body section by up to 25 % and the Top section by up to 60 %, then settles to zero during sustain. The envelope gain lives inside the wet paths, so a knob at 0 stays bit-transparent. All envelope state is plain floats and all coefficients are computed in `prepare()`; the audio thread allocates nothing.
  - Mode high-passes sit between 55 and 90 Hz; Level is a manual ±12 dB trim with no automatic makeup.
- **Defaults**: Top 60 %, Body 65 %, Piezo 20 %. Changing a default only affects new instances; saved DAW state and presets keep their stored values.
- **Auto-off of distortion/EQ/IR**: Engaging the pedal force-bypasses the NAM Pedal, NAM Amp, Pre-EQ, Post-EQ and IR Loader by flipping their bypass parameters (so the UI reflects it), and disengaging restores each stage the sim switched off — unless the user changed it while the sim was on. A loaded preset is treated as ground truth.
- **Controls**: Mode (Standard/Jumbo/Enhanced/Piezo), Top, Body, Piezo, Level, bypass footswitch. The pedal starts bypassed.

### 4.15 Reverb IR Loader (Post FX pedal)
- **Goal**: Offer convolution reverb from two user-supplied WAV/AIFF stereo IRs on the Post FX pedalboard, independent of the algorithmic Reverb and the cabinet IR loader.
- **Placement**: After `PostEffects` and before `RoomSim`; the whole Post FX tab bypass also bypasses this stage.
- **Implementation**:
  - `ReverbIrLoader` owns two parallel `juce::dsp::Convolution` slots, each with `Stereo::yes`, `Trim::no` and `Normalise::yes`. On the message thread it reads the IR file, applies the Cutoff fade, and hands an `AudioBuffer` to Convolution, which swaps it in without blocking playback. JUCE trimming is disabled so the IR's pre-delay is preserved; mono files are expanded to stereo.
  - Both slots read the same dry block. Each wet result is scaled by its own smoothed Mix gain. A separate smoothed dry gain is `1 - max(Mix L, Mix R)`, so any Mix at 100 % removes the dry signal entirely while the two IRs still sound in parallel; there is no Blend knob. All scratch buffers are sized in `prepare()`; the audio thread only reads cached raw APVTS atomics and calls `getNextValue()` on those gains.
  - The Master knob is a local wet-output boost/cut in dB, stored as a linear gain and folded into each slot's smoothed Mix gain. It never changes the global Master Output parameter and does not touch the dry path.
  - Cutoff is a tail-length trim, not a tone filter. The audio thread only stores the new seconds value and triggers the processor's async update; `applyPendingCutoffChanges()` then rereads the file on the message thread, cosine-fades the last 50 ms before the cutoff and zeroes the rest. No file I/O or buffer allocation happens in `processBlock()`.
  - With no IR loaded, both Mix knobs at 0 %, or while bypassed the stage is an exact passthrough. Engaging bypass clears both convolution tails so stale reverb cannot leak through. `reset()` also snaps the Mix gains without unloading the IRs.
  - The editor reuses the cabinet loader's filename screen, folder popup, **LOAD** button, arrows and drag-and-drop, but gives the screen more width by narrowing the file controls. It presents two rows in a `UnitStyle::Pedal` with accented **Mix L** / **Mix R** knobs, **Cutoff L** / **Cutoff R** knobs, a **Master** knob bound to `reverbIrLevel` for a local wet boost/cut, and a shared bypass footswitch.
  - Each slot has its own last-used folder (`lastReverbIrLeftDirectory` / `lastReverbIrRightDirectory`) and persisted path (`reverbIrLeftPath` / `reverbIrRightPath`), separate from the cabinet IRs. Presets store them as the `FILES` attributes `reverbIrLeft` / `reverbIrRight`; missing files warn without discarding parameter state. A v9 preset's single `reverbIr` path loads into the left slot.
- **Parameters**: `reverbIrBypass` (default on), `reverbIrLeftMix` and `reverbIrRightMix` (0–100 %, default 25 %), `reverbIrLeftCutoff` and `reverbIrRightCutoff` (0.05–3 s, default 3 s), and `reverbIrLevel` (-24 to +12 dB, default 0 dB). State version 9 added the single `reverbIrMix`; version 10 replaces it with the two slot Mix parameters and seeds both from the old value. Version 11 adds the two Cutoff parameters with the maximum default. Version 12 replaces the pedal's global-Master link with the local Level parameter at unity gain.

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
- [x] Implement the 2-slot `IrLoader` with `dsp::Convolution` and a unity-summing Blend crossfade.
- [x] Build the 3-band `MultiBandEQ` for the pre-amp and the 8-band `PostAmpEQ` for after the amp.
- [x] Integrate into signal chain.

### Phase 3: Dynamics & Drive (Weeks 5–6)
- [x] Implement `Compressor` (with optional low-band sidechain).
- [ ] Implement `Overdrive` (waveshaper + tone) — superseded by the NAM pedal design.
- [x] Implement `NoiseGate`.

### Phase 4: Time-Based & Modulation FX (Weeks 7–8)
- [x] Implement shared `ModulationLfo`, three-voice `ChorusEnsemble` and `ModulatedDelay` flanger.
- [x] Implement 4/8/12-stage all-pass `Phaser`.
- [x] Implement `Delay` (ping-pong stereo / centred mono circular buffers).
- [x] Integrate `juce::Reverb`.
- [x] Implement the separate `ReverbIrLoader` Post FX pedal with two parallel stereo IR slots, independent L/R Mix and tail Cutoff, accented Mix knobs, a local wet boost/cut Master control and v12 migration.
- [x] Implement `RoomSim` (Room 0–100 %; default 0 %; two short low-feedback delays; v8 migration from Image Mix).

### Phase 5: UI & State Management (Weeks 9–10)
- [x] Design `NAMBerryEditor` with tabs or a scrollable rack layout.
- [x] Bind all parameters to `AudioProcessorValueTreeState`.
- [x] Implement preset save/load.
- [x] Add file drag-and-drop for `.nam` and `.wav` IR files.
- [x] Add tap-away filename popups (tap a screen → folder list, touch elsewhere → close) with separate amp, pedal, cabinet IR and both reverb IR starting folders, plus a **LOAD** button per loader.
- [x] Add previous/next arrows to the amp, pedal, cabinet IR and both reverb IR fields to step through matching files in the current directory.
- [x] Remember the folder left in as the next starting folder for each loader (no `SET ROOT...` button).
- [x] Persist the selected amp model, pedal model, both cabinet IR slot files and both reverb IR slot files.
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
| **IR phase cancellation** when blending multiple cabinets. | Only two IRs are ever blended, and the unity-summing crossfade is normalized on load; Blend at either end mutes one row outright, which is the escape hatch when two captures fight each other. |

---

## 8. References & Resources

- **JUCE Cross-Platform Starter**: `danielraffel/JUCE-Plugin-Starter` provides a CMake-based template for macOS, Windows, and Linux builds.
- **NeuralAmpModelerCore**: Official C++ DSP library for NAM, supporting WaveNet, LSTM, ConvNet, and Linear models.
- **NeuralAudio**: Alternative MIT-licensed library with a simpler API and A1/A2 support.
- **NAM Universal Feature Reference**: Details on the Aggravator module (Tight/Smooth), Instigator tonestack, and dual-IR loader.
- **JUCE IR Loader Techniques**: Using `dsp::Convolution` with `DryWetMixer` for smooth blending and background loading.
