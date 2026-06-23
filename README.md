# Pro Reverb — Algorithmic Reverb VST3

A state-of-the-art algorithmic reverb plugin built in pure C++20 with a JUCE shell.  
Architecture: **TV-FDN** (Time-Varying Feedback Delay Network) + **Velvet Noise Early Reflections** + **Frequency-Dependent Decay (FDD)**.

---

## Build instructions

### Prerequisites

| Requirement | Version |
|-------------|---------|
| CMake       | ≥ 3.22  |
| C++ compiler | MSVC 2022, GCC 12, or Clang 16 with C++20 |
| JUCE        | 7.0.x (see below) |

### Getting JUCE

Option A — **Git submodule** (recommended for offline/CI builds):
```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git submodule update --init --recursive
```

Option B — **Explicit path** on the CMake command line:
```
-DJUCE_DIR=C:/path/to/JUCE
```

Option C — **Automatic FetchContent** (requires internet access at configure time):  
If neither of the above is available, CMake will download JUCE 7.0.12 automatically.

### Configure and build

```bash
# DSP tests only (no JUCE required)
cmake -B build -DBUILD_VST3=OFF
cmake --build build --config Release

# Full plugin + tests
cmake -B build
cmake --build build --config Release
```

The VST3 bundle will be output to:
```
build/ProReverb_artefacts/Release/VST3/Pro Reverb.vst3
```

### Running the smoke tests

```bash
./build/Release/tvfdn_smoke_test
./build/Release/reverb_smoke_test
./build/Release/decay_smoke_test
```

All three must print `PASS` with no `FAIL` lines.

---

## Parameter reference

### Global

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Pre-Delay | 0 – 500 ms | 0 ms | Global pre-delay applied to both ER and FDN paths. Automated with tape-style Doppler glide (Hermite FDL). |
| Distance | 0 – 1 | 0.5 | Equal-power crossfade: 0 = ER only (close), 1 = FDN tail only (far). |
| Wet Mix | 0 – 1 | 1.0 | Master dry/wet blend, per-sample smoothed. |

### FDN

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| FDN Feedback | 0 – 0.99 | 0.85 | Overall reverb length. See T60 coupling note below. |
| Mod Depth | 0 – 2 smp | 0.75 | LFO modulation depth on delay lines. Breaks modal density; higher values add chorus-like movement. |

### Decay EQ

Six parameters controlling how quickly each frequency band decays.  
These are **relative T60 targets**, not absolute decay times — see the coupling note.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Low Freq | 20 – 2000 Hz | 250 Hz | Low-shelf corner frequency |
| Low T60 | 0.1 – 20 s | 3.0 s | Decay time for bass frequencies |
| Mid Freq | 200 – 10 000 Hz | 1500 Hz | Peak/bell centre frequency |
| Mid T60 | 0.1 – 20 s | 2.0 s | Decay time for midrange frequencies |
| High Freq | 1 000 – 20 000 Hz | 5000 Hz | High-shelf corner frequency |
| High T60 | 0.1 – 10 s | 0.8 s | Decay time for high frequencies |

---

## FDN Feedback / T60 interaction

> **Important for preset designers and advanced users.**

The per-channel loop gain at any frequency is the product of two terms:

```
loop_gain(f) = FDN_Feedback × AbsorptionBank_gain(f)
```

`AbsorptionBank_gain` is computed from the T60 target assuming `FDN_Feedback = 1.0`. Because `FDN_Feedback` is also multiplying the signal, the **actual realised T60 is always shorter** than the T60 knob value:

```
actual_T60 ≈ -3·D / log10(FDN_Feedback × 10^(-3·D / target_T60))
```

where `D` is the delay line round-trip time (3–18 ms for the 16 channels).

### Practical guidance

- Use **FDN Feedback** to set the overall perceived reverb length.
- Use the **Decay EQ knobs** to shape the *spectral decay profile* (which frequencies die first), not the absolute duration.
- With the default `Feedback = 0.85`, a `Low T60 = 3 s` target yields an actual bass decay of ~1.5 s. Increase Feedback toward 0.95–0.97 for longer tails.
- **Rule of thumb:** the ratio between High T60 and Low T60 controls how "dark" or "airy" the late tail sounds. A 4:1 ratio (e.g. Low=3 s, High=0.75 s) mimics a furnished room; a 10:1 ratio (Low=5 s, High=0.5 s) mimics a stone cathedral.

---

## Architecture overview

```
Dry L/R
  │
  ├──► FractionalDelayLine (Hermite) ──► Pre-delay output
  │         (per-sample Doppler glide)
  │
  ├──► EarlyReflections (Velvet Noise, independent L/R)
  │         ──► erGain = cos(distance × π/2)
  │
  └──► TVFDNEngine (AdvancedFDN<16>)
            16 prime-length delay lines
            FWHT orthogonal mixing
            Multi-phase wavetable LFOs
            Per-channel AbsorptionBank (Low Shelf + Peak + High Shelf)
              ↑ inserted AFTER DC blocker, BEFORE dry injection
            ──► tailGain = sin(distance × π/2)
  │
  └──► masterWet blend (per-sample smoothed)
  │
  └──► Output L/R
```

---

## Development phases

| Phase | Status | Summary |
|-------|--------|---------|
| 1 | ✅ Complete | TV-FDN core: Hermite FDL, prime delays, FWHT, wavetable LFO |
| 2 | ✅ Complete | Velvet Noise ER, ReverbEngine facade, pre-delay, distance EQ |
| 3 | ✅ Complete | RBJ absorption banks inside FDN feedback loop, T60 → loop gain |
| 4 | ✅ Complete | JUCE VST3 shell, APVTS, functional knob UI |
| 5 | 🔜 Planned | Interactive decay-curve display (T60 vs frequency), preset browser |
