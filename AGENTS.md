# AGENTS.md

Instructions for coding agents working on **NAMBerry**. Read this before making
changes. When something here conflicts with an explicit user request, ask or
follow the user, then update this file if the project convention has changed.

## 1. Project identity

NAMBerry is a **C++20 / JUCE 8.0.6 audio plugin** for guitar:

- formats: **VST3** and **Standalone**
- stereo in / stereo out, no MIDI
- two NAM neural captures: pedal/preamp and amp
- two IRs crossfaded by one Blend control
- pitch/capo stage, tuner, gate, acoustic sim, compressor, pre/post EQ,
  modulation, room, delay, reverb
- hardware-inspired JUCE editor, not a web UI and not Qt/QML

The shipped UI is native JUCE. `ui-spec.md` is a historical Qt/QML exploration;
do not use it as an implementation guide. `spec.md` is the product/DSP
specification. `README.md` is the user-facing manual. `fx-spec.md` and
`post-eq-8band-plan.md` are feature plans.

## 2. Non-negotiable rules

1. **Do not commit unless the user explicitly asks.** Leave changes in the
   working tree.
2. **Do not edit `JUCE/` or `deps/`.** They are ignored third-party checkouts.
   Change project code, CMake wiring, or documentation instead. Update a
   dependency only when the user asks.
3. **Never break the audio thread.** No new allocation, freeing, locks, file
   I/O, logging, `setLatencySamples()`, or message-loop calls inside
   `processBlock` / DSP `processBlock` methods. The existing Post EQ loop builds
   parameter-ID `juce::String`s on the audio thread; do not add more work like
   that, and prefer caching parameter pointers if you refactor that area.
4. **Never casually change parameter IDs or state layout.** Existing DAW
   sessions and presets depend on them. Add migrations for every incompatible
   state change.
5. **Keep the existing DSP architecture and conventions** unless the task
   requires a redesign. Match surrounding code style instead of introducing a
   new framework.
6. **Build and test before reporting success.** A compile is not enough for
   DSP or UI changes.
7. **Update documentation when behavior, parameters, controls, or build steps
   change.** Update `README.md` for users and `spec.md` for behavior/DSP.
8. **Do not add generated artifacts, models, IRs, screenshots, build trees, or
   copied binaries to git.** `.gitignore` already excludes the important ones.

## 3. Repository map

```
CMakeLists.txt              build targets, source registration
CMakePresets.json           standalone / vst3 / debug presets
build.sh                    builds both formats via scripts
scripts/build-standalone.sh standalone Release + copy to ./NAMberry
scripts/build-vst3.sh       VST3 Release

src/
  PluginProcessor.*         signal chain, APVTS, state migration, presets
  PluginEditor.*            JUCE editor and custom hardware UI
  nam_model_loader.cpp      NAM inference wrapper
  impersonator.cpp          pickup-shaping EQ pedal
  pitch_shifter.cpp         Rubber Band / Signalsmith streaming engines
  digital_capo.cpp          shared pitch stage
  drop_tuner.cpp            detection-only tuner
  noise_gate.cpp
  acoustic_sim.cpp
  compressor.cpp
  multi_band_eq.cpp         three-band Pre-EQ
  post_amp_eq.cpp           eight-band Post-EQ
  ir_loader.cpp
  reverb_ir_loader.cpp      two parallel stereo Post FX reverb IRs with L/R Mix, tail Cutoff and local wet boost/cut
  modulation_effects.cpp
  chorus_ensemble.cpp       three-voice stereo chorus
  post_effects.cpp
  room_sim.cpp
  directory_browser.cpp     legacy, compiled but not reachable from UI
  main.cpp                  standalone entry point

include/namberry/           public DSP module headers (#pragma once)
tests/offline_render.cpp    offline DSP / state / persistence tests
tests/ui_snapshot.cpp       headless PNG snapshots + UI interaction checks

JUCE/                       ignored, required locally
deps/                       ignored, required locally
NAMberry                    ignored ready-to-run standalone binary
```

The test and plugin sources share the `namberry` namespace. File-local helpers
and private UI components normally live in an anonymous namespace in the `.cpp`.

## 4. Environment and build

The project builds with CMake 3.22+ and a C++20 compiler. `JUCE/` and `deps/`
must be present locally. On a fresh clone, follow `README.md` → Building to
clone them; do not vendor them into git.

### Recommended commands

```bash
# Both formats, Release. Also copies the standalone to ./NAMberry.
./build.sh

# Individually
./scripts/build-standalone.sh [--clean]
./scripts/build-vst3.sh [--clean]

# CMake presets
cmake --preset standalone
cmake --build --preset standalone
cmake --preset vst3
cmake --build --preset vst3
```

Artifacts:

```
build-standalone/NAMBerry_artefacts/Release/Standalone/NAMberry
build-vst3/NAMBerry_artefacts/Release/VST3/NAMberry.vst3
```

### Local Linux/sysroot gotcha

The helper scripts automatically detect `/tmp/sysroot` and set `CPATH`,
`LIBRARY_PATH`, `LD_LIBRARY_PATH`, `PKG_CONFIG_SYSROOT_DIR`, and
`PKG_CONFIG_PATH`. That directory is a local machine workaround, not part of the
repository. If you configure CMake directly without the scripts on such a
machine, export the same variables first:

```bash
export CPATH=/tmp/sysroot/usr/include:/tmp/sysroot/usr/include/freetype2
export LIBRARY_PATH=/tmp/sysroot/usr/lib64
export LD_LIBRARY_PATH=/tmp/sysroot/usr/lib64
export PKG_CONFIG_SYSROOT_DIR=/tmp/sysroot
export PKG_CONFIG_PATH=/tmp/pc:/tmp/sysroot/usr/lib64/pkgconfig:/tmp/sysroot/usr/share/pkgconfig
```

The README test paths use a generic `build/` tree. In this workspace the scripts
configure `build-standalone/`; use whichever tree is actually configured and
substitute its path. `build-standalone/compile_commands.json` is useful for
exact compiler flags. The presets and helper scripts assume Unix Makefiles and
bash; on Windows configure with the Visual Studio generator instead.

### Root binary while the app is running

`scripts/build-standalone.sh` copies the fresh app over `./NAMberry`. Linux
returns `Text file busy` if that binary is currently running. Close the app
first, or replace it atomically:

```bash
cp build-standalone/NAMBerry_artefacts/Release/Standalone/NAMberry NAMberry.new
mv -f NAMberry.new NAMberry
```

## 5. Tests

There is no single test-runner executable. `NAMBerryOfflineTest` is one binary
with a mode flag per test. Build the test tools first:

```bash
# Configure build-standalone once, then build the tools there.
./scripts/build-standalone.sh

cmake --build build-standalone --target NAMBerryOfflineTest NAMBerryUiSnapshot -j"$(nproc)"
```

Binaries:

```bash
./build-standalone/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest
./build-standalone/NAMBerryUiSnapshot_artefacts/Release/NAMBerryUiSnapshot
```

### Core offline tests

Run the no-argument smoke test, then the deterministic modes relevant to your
change. The complete offline mode list is:

```bash
TEST=./build-standalone/NAMBerryOfflineTest_artefacts/Release/NAMBerryOfflineTest

"$TEST"                     # dry/effect-chain smoke test
"$TEST" --reference
"$TEST" --reverb-dry
"$TEST" --reverb-ir
"$TEST" --reverb-ir-state
"$TEST" --delay-feedback
"$TEST" --delay-reset
"$TEST" --delay-ping-pong
"$TEST" --chorus-ensemble
"$TEST" --room
"$TEST" --modulation
"$TEST" --fx-state
"$TEST" --preset
"$TEST" --compressor
"$TEST" --impersonator
"$TEST" --acoustic-sim
"$TEST" --acoustic-auto-off
"$TEST" --tone-match
"$TEST" --ir-level
"$TEST" --ir-state
"$TEST" --post-eq
"$TEST" --post-eq-state
"$TEST" --dry-center
"$TEST" --detector
"$TEST" --detected-stage
"$TEST" --pitch
"$TEST" --pitch-low
"$TEST" --pitch-robust
"$TEST" --pitch-blocks
```

Fixture/diagnostic-only modes:

```bash
"$TEST" --persist    # hardcoded local model/IR paths; prints SKIP and exits 0 if missing
"$TEST" --dirs       # creates /tmp/opencode directories and prints them
```

Tests print `PASS`/`FAIL` and return a nonzero exit code on failure. Do not
ignore a failing mode because another mode passes.

### Full processor test with a model

```bash
"$TEST" /path/to/model.nam
```

The no-argument smoke test uses the dry/effect chain without a NAM model.

### UI snapshots

```bash
./build-standalone/NAMBerryUiSnapshot_artefacts/Release/NAMBerryUiSnapshot \
  /tmp/opencode/namberry-ui
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

The tool is headless and also performs interaction checks: Post EQ drag,
wheel-Q, double-click reset, and focused-band Boost/Q knobs. If you change
layout geometry or component names, update `tests/ui_snapshot.cpp` and rerun it.

## 6. Architecture and invariants

### Signal chain

Keep this order unless the task explicitly changes it:

```
Input
  -> Tuner (detection only)
  -> Impersonator
  -> Pitch (capo / drop tune)
  -> Noise Gate
  -> Acoustic Sim
  -> Compressor
  -> NAM Pedal / Preamp
  -> Pre-Amp EQ
  -> NAM Amp
  -> Post-Amp EQ
  -> IR Loader (2 IRs + Blend)
  -> Post FX (Chorus / Flanger / Phaser / Image / Delay / Reverb)
  -> Reverb IR (two parallel stereo IR slots, independent L/R Mix and tail Cutoff)
  -> Master Output
```

Important details:

- **NAM models are mono and stateful.** Sum input channels to mono, run one
  model, then copy the result to every output channel. Do not run a stateful
  model independently per channel.
- **Dry path must stay centred.** Pitch shifting delays and mono-sums the dry
  path to match latency; bypass / 0 semitones / 0 % mix pass dry immediately.
- **Pitch latency changes belong on the message thread.** The audio thread only
  stores a pending value and calls `triggerAsyncUpdate()`. Never call
  `setLatencySamples()` from `processBlock`.
- **Acoustic Sim auto-off is message-thread bookkeeping.** The audio thread
  detects the engage edge; `handleAsyncUpdate()` flips pre-EQ/post-EQ/IR/amp/
  pedal bypasses. Preserve the saved/restore logic and preset ground-truth rule.
- **IR Blend must stay unity-sum.** With both IRs loaded, gains are `1-blend`
  and `blend`. With one loaded, that IR plays at unity.
- **Master output includes a fixed +6 dB trim** on top of the saved Master
  value. Do not “correct” this.
- **Bypass is part of the contract.** Whole-section bypass (`preFxBypass`,
  `postFxBypass`) and individual module bypasses both matter. Several tests
  require exact dry pass-through.
- **Stereo only.** `isBusesLayoutSupported` rejects other layouts.

### DSP module conventions

A typical module exposes:

```cpp
void prepare (const juce::dsp::ProcessSpec& spec);
void reset();
void setSomething (...);
void setBypass (bool);
bool isBypassed() const;
void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&);
```

Rules:

- Allocate all filters, delay lines, scratch buffers, and coefficient storage in
  `prepare()`, not in `processBlock()`.
- Handle sample-rate changes in `prepare()`. Clamp frequencies below Nyquist.
- Reset filter/delay state in `reset()` and clear delay/FDN tails when the user
  expects a bypass to discard feedback.
- Use `juce::ScopedNoDenormals` at the processor `processBlock` boundary.
- Smooth gain changes and crossfade mode switches; do not hard-switch audible
  parameters. Existing code uses `juce::SmoothedValue` and short crossfades.
- Read parameters with APVTS raw atomics in the audio path:
  `apvts_.getRawParameterValue("id")->load()`. Do not call
  `getParameter(...)->getValue()` from the audio thread.
- Prefer cached raw-value pointers or parameter pointers over constructing
  parameter IDs inside `processBlock`. The current Post EQ loop is the known
  exception; improve it when touching that code.
- Keep all output finite and bounded. Tests stress extremes, sample-rate
  changes, and randomized block sizes.
- Do not change the shared Post EQ response calculations without checking both
  the audio processing and `getResponseDb()`; the UI curve and DSP must agree.

### Parameter, state, and preset rules

Parameters are created in `createParameterLayout()` in
`src/PluginProcessor.cpp`. IDs are stable API.

When adding a parameter:

1. Add it to `createParameterLayout()` with a unique stable ID, sensible range,
   default, and unit label.
2. Wire it into `processBlock` (or the relevant module).
3. Add the UI control / APVTS attachment.
4. Add or update tests.
5. Update `README.md`, `spec.md`, and any relevant plan/spec document.
6. Add a state migration if the new parameter replaces or changes existing
   state.

When removing/renaming a parameter:

- Increment the state version and migrate old children.
- Remove obsolete `PARAM` children so a later save cannot retain two
  representations.
- Add a migration test, following `--post-eq-state` / `--ir-state` /
  `--fx-state`.

Current state versions in `PluginProcessor.cpp`:

```cpp
constexpr int kPostFxStateVersion = 3;
constexpr int kIrStateVersion = 4;
constexpr int kPostEqStateVersion = 6;
constexpr int kFxStateVersion = 12;
```

`getStateInformation()` writes `fxVersion`; `setStateInformation()` runs the
migrations in order from the stored version. Preset files use a root
`NAMBERRYPRESET` version `1`, containing the plugin state plus a `FILES`
section for amp/pedal/IR paths.

Do not put model/IR paths in DAW project state. They belong to the per-user
`PropertiesFile` and to preset files:

- Linux: `~/.config/NAMBerry/NAMBerry.settings`
- macOS/Windows: standard app-data location under `NAMBerry/`

Preset save/load must preserve file paths, tolerate missing files, and warn the
user without discarding the parameter state.

## 7. UI rules

The editor is `src/PluginEditor.cpp` (large, JUCE-native). It uses:

- a custom `HardwareLookAndFeel`
- five tabs: Pre FX, Amp, Post EQ, IR, Post FX
- `SectionPanel` rack/pedal/amp containers
- APVTS `SliderAttachment` / `ButtonAttachment` for standard controls
- custom graphical editors that cache settings and poll at ~30 Hz
- the `Reduced Motion` preference for hover animation

Guidelines:

- Use `juce::` explicitly. Do not introduce Qt/QML.
- Put private UI helper classes in an anonymous namespace in
  `PluginEditor.cpp` unless they truly need a header.
- Follow the existing palette constants: `kBg`, `kPanel`, `kPanelLine`,
  `kAccent`, `kText`, `kTextDim`.
- For custom mouse/keyboard interaction, call `beginChangeGesture()` /
  `endChangeGesture()` around parameter writes so host automation is clean.
- Custom controls that are not APVTS attachments should still update when host
  automation moves the underlying parameter. Poll raw values on a timer and
  update cached UI state.
- Give components that tests need to find a stable `setName()`, and keep the
  name unless you update `tests/ui_snapshot.cpp`.
- The UI snapshot depends on layout geometry. When you change panel margins,
  the Post EQ focus strip, or graph bounds, update the coordinate calculations
  in the test.
- Keep `Reduced Motion` behavior: animations should be disabled or minimized
  when the preference is on.
- The Post EQ is a special full-width graphical panel with draggable nodes,
  HPF/LPF handles, a response curve, and a focused-band Boost/Q knob pair.
  Preserve its interaction model and its test coverage.
- `ui-spec.md` is historical; update it only to clarify current UI behavior or
  to retain the design record. Do not port its QML sketches.

## 8. Adding features: checklist

For a normal feature:

1. Read the relevant module, processor chain, UI tab, tests, and docs.
2. Check `git status` first; do not overwrite unrelated user work.
3. Write or update the plan/spec if the feature is substantial.
4. Implement DSP first, then parameter wiring, then UI.
5. Register new `.cpp` files in `CMakeLists.txt` (`target_sources(NAMBerry ...)`)
   and add headers under `include/namberry/` with `#pragma once`.
6. Add the module member to `PluginProcessor.h`, prepare/reset it, and insert
   it at the correct point in the chain.
7. If parameters changed, add migration logic and a migration test.
8. Build:
   ```bash
   cmake --build build-standalone --target \
     NAMBerry_Standalone NAMBerryOfflineTest NAMBerryUiSnapshot -j"$(nproc)"
   ```
9. Run the no-arg test, every relevant flag, and the UI snapshot if the editor
   changed.
10. Build VST3 and standalone. Keep the root `./NAMberry` copy current when
    using the standalone script.
11. Update `README.md`, `spec.md`, and `ui-spec.md` / feature plan as needed.
12. Report what changed, what passed, and any remaining risk. Do not commit.

## 9. Coding style

- C++20, JUCE style. Four spaces, no tabs. Braces on the next line for
  functions/classes.
- Namespace `namberry` for project code. No `using namespace`.
- Class names `PascalCase`; methods/variables `camelCase`; members end with `_`;
  compile-time constants start with `k` (`kAccent`, `kNumBands`).
- Prefer `juce::jlimit`, `juce::jmax`, `juce::approximatelyEqual`,
  `std::array`, `std::unique_ptr`, and RAII.
- Use `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` for owning UI components.
- Keep comments useful: explain intent, invariants, real-time constraints, and
  state migrations. Do not narrate obvious code.
- Match the existing JUCE spelling (`juce::FontOptions`, `juce::Colour`, etc.).
- Do not add a formatter config or reformat unrelated files.
- Known baseline warnings are acceptable; do not introduce new ones:
  - hidden `AudioProcessor::processBlock(double...)`
  - `RackUnit` missing in `SectionPanel::titleHeight` switch
  - float `==`/`!=` comparison in `SectionPanel::timerCallback`
  - sign-conversion in `tabPanels_.push_back`

## 10. Dependency and licensing constraints

- JUCE is dual-licensed (AGPLv3 or commercial).
- Rubber Band is optional at runtime and is GPL or commercially licensed.
- NeuralAudio, NeuralAmpModelerCore, Signalsmith Stretch/Linear have their own
  licenses.
- Do not add a new third-party dependency, model, or IR without confirming its
  license and asking the user. The project aims to remain redistributable.

## 11. Debugging and verification tips

- Use the offline tests before launching the standalone. They are much faster
  and deterministic.
- Test DSP at 44.1, 48, and 96 kHz where sample-rate-dependent behavior matters,
  and at non-power-of-two host block sizes.
- Check output bounds and `std::isfinite` for extreme parameter settings.
- For UI, run the snapshot tool and open the PNGs; the tool also returns
  nonzero on interaction failures.
- If a test depends on local files, do not “fix” it by committing fixtures.
  Use a temporary `/tmp/opencode` file or report the skip.
- Scratch files go under `/tmp/opencode`, not in the repository.
- Inspect `git diff --check` and the final `git diff` before reporting.

## 12. Current documentation ownership

- `README.md`: user-facing controls, build instructions, test commands,
  limitations.
- `spec.md`: signal flow, parameters, DSP behavior, migration rules.
- `ui-spec.md`: historical UI design plus the current JUCE UI notes.
- `fx-spec.md`: modulation FX implementation plan.
- `post-eq-8band-plan.md`: Post EQ implementation/test plan.

If a behavior or control changes, update the first two at minimum.
