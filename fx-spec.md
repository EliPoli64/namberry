# NAMberry Modulation FX Implementation Plan

## 1. Architectural Overview

The modulation block is placed post-amp and post-IR loader (or before delay/reverb). To optimize CPU and simplify the UI, all three effects share a base modulation framework containing an LFO (Low-Frequency Oscillator), but differ in their underlying delay lines and filter topologies.

```
[Incoming Audio] ──> [Input Gain / Splitter] ──> [Modulation Core (Chorus / Flanger / Phaser)] ──> [Dry/Wet Blend] ──> [Output]
                                                     ▲
                                                     └── LFO (Sine/Triangle generator)

```

---

## 2. Chorus Effect

### 2.1 DSP Mechanics

* **Core Structure**: A modulated fractional delay line (typically 5ms to 30ms) combined with a feedback loop and dry/wet mixer.
* **LFO Waveform**: Sine wave (smooth, musical modulation).
* **Stereo Handling**: Invert the LFO phase on the right channel (180-degree offset) to create a wide, immersive stereo image.

### 2.2 Knob Ranges & Values

| Parameter | ID | Range | Unit | Default | Description |
| --- | --- | --- | --- | --- | --- |
| **Rate** | `chorus_rate` | 0.1 to 5.0 | Hz | 0.8 | Speed of the modulation LFO. |
| **Depth** | `chorus_depth` | 0.0 to 100.0 | % | 50.0 | Amount of delay time modulation (intensity). |
| **Feedback** | `chorus_fb` | 0.0 to 70.0 | % | 15.0 | Feeds wet signal back into delay line for metallic shimmer. |
| **Delay Time** | `chorus_delay` | 5.0 to 30.0 | ms | 15.0 | Base center delay time. |
| **Mix** | `chorus_mix` | 0.0 to 100.0 | % | 50.0 | Dry/Wet signal blend. |

---

## 3. Flanger Effect

### 3.1 DSP Mechanics

* **Core Structure**: Extremely short fractional delay line (0.5ms to 10ms) with high feedback and a triangular or sine LFO.
* **Comb Filtering**: Creates the classic "jet sweep" sound due to harmonic notches created by constructive/destructive interference.
* **Feedback Polarity**: Allow positive and negative feedback for hollow vs. metallic flanger sweeps.

### 3.2 Knob Ranges & Values

| Parameter | ID | Range | Unit | Default | Description |
| --- | --- | --- | --- | --- | --- |
| **Rate** | `flanger_rate` | 0.05 to 10.0 | Hz | 0.4 | Sweep speed of the flanger. |
| **Depth** | `flanger_depth` | 0.0 to 100.0 | % | 60.0 | Width of the sweep across delay times. |
| **Manual (Delay)** | `flanger_manual` | 0.5 to 10.0 | ms | 2.0 | Center base delay point of the sweep. |
| **Feedback** | `flanger_fb` | -95.0 to 95.0 | % | 40.0 | Regeneration amount (positive or negative). |
| **Mix** | `flanger_mix` | 0.0 to 100.0 | % | 50.0 | Dry/Wet signal blend. |

---

## 4. Phaser Effect

### 4.1 DSP Mechanics

* **Core Structure**: Cascaded first-order all-pass filters (APFs). The cutoff frequencies of the all-pass filters are modulated simultaneously by an LFO.
* **Notches**: Number of notches equals half the number of stages (e.g., 4 stages = 2 notches, 8 stages = 4 notches).
* **Feedback**: Feeds the output of the last all-pass stage back into the input to sharpen the notches into resonant peaks.

### 4.2 Knob Ranges & Values

| Parameter | ID | Range | Unit | Default | Description |
| --- | --- | --- | --- | --- | --- |
| **Rate** | `phaser_rate` | 0.05 to 8.0 | Hz | 0.5 | Modulation frequency of the phase notches. |
| **Depth** | `phaser_depth` | 0.0 to 100.0 | % | 70.0 | Sweep range intensity. |
| **Feedback** | `phaser_fb` | 0.0 to 90.0 | % | 30.0 | Resonance/regeneration of the filter sweeps. |
| **Stages** | `phaser_stages` | 4, 8, 12 (Choice) | Stages | 8 | Filter complexity (higher = more turbulent sweep). |
| **Mix** | `phaser_mix` | 0.0 to 100.0 | % | 50.0 | Dry/Wet signal blend. |

