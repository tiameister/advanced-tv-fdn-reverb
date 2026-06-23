# TiaVerb

**Professional algorithmic reverb** by [Tia Audio](https://tiaaudio.com)

TiaVerb is a high-end algorithmic reverb available as a **VST3 plugin** and **standalone application**. It combines a time-varying feedback delay network (TV-FDN), velvet-noise early reflections, frequency-dependent decay shaping, and proximity modelling in a modern web-based UI.

| | |
|---|---|
| **Version** | 1.0.0 |
| **Formats** | VST3, Standalone |
| **Platforms** | Windows (macOS/Linux build supported via CMake) |
| **License** | Proprietary — Copyright © 2026 Tia Audio |

---

## Features

- **TV-FDN engine** — 16-channel feedback delay network with prime-length lines, FWHT mixing, and multi-phase LFO modulation for dense, natural tails
- **Velvet-noise early reflections** — decorrelated stereo ER field without metallic colouration
- **Unified reverb time** — one control drives FDN feedback and per-band T60 targets together
- **Interactive decay curve** — three-node bass / mid / HF shaping relative to reverb time
- **Proximity control** — equal-power crossfade between early reflections (close) and late tail (far)
- **Factory presets** — Small Room, Studio Plate, Drum Room, Large Hall, Cathedral
- **Modern UI** — HTML/CSS/JS interface embedded via JUCE WebView2 (Windows)

---

## Quick start

### Run standalone (Windows)

```powershell
.\build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe
```

### Install VST3

Copy the bundle to your DAW's VST3 folder:

```text
build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3
→ C:\Program Files\Common Files\VST3\
```

Rescan plugins in your DAW. The plugin appears as **TiaVerb** by **Tia Audio**.

### Batch render (offline testing)

```powershell
.\build\Release\batch_render.exe audio.wav renders
```

Renders the input through the DSP engine across distance × stereo-width configurations. See [BUILD.md](BUILD.md) for full build instructions.

---

## Build from source

### Prerequisites

| Requirement | Notes |
|-------------|-------|
| CMake | ≥ 3.22 |
| C++ compiler | MSVC 2022 (Windows), GCC 12+, or Clang 16+ with **C++20** |
| JUCE | **8.0.x** — submodule, `JUCE_DIR`, or automatic FetchContent |
| WebView2 SDK | Required on Windows for the web UI (NuGet package — see below) |

### Windows: WebView2 (one-time)

The UI requires the WebView2 static loader. Install via PowerShell:

```powershell
Register-PackageSource -provider NuGet -name nugetRepository -location https://www.nuget.org/api/v2
Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -RequiredVersion 1.0.1901.177 -Source nugetRepository
```

The Edge WebView2 **runtime** must also be installed on the target machine (usually already present on Windows 10/11).

### Getting JUCE

**Option A — Git submodule** (recommended for offline/CI):

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git submodule update --init --recursive
```

**Option B — Explicit path:**

```bash
cmake -B build -DJUCE_DIR=C:/path/to/JUCE
```

**Option C — Automatic download** (requires internet at configure time):  
CMake fetches JUCE 8.0.6 if no local copy is found.

### Configure and build

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Build individual targets:

| Target | Output |
|--------|--------|
| `ProReverb_VST3` | `build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3` |
| `ProReverb_Standalone` | `build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe` |
| `batch_render` | `build\Release\batch_render.exe` |

See [BUILD.md](BUILD.md) for a Windows-focused command reference.

### DSP smoke tests (no JUCE UI)

```powershell
cmake -B build -DBUILD_VST3=OFF
cmake --build build --config Release
.\build\Release\tvfdn_smoke_test.exe
.\build\Release\reverb_smoke_test.exe
.\build\Release\decay_smoke_test.exe
```

All three must print `PASS`.

---

## Parameters

### Main

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Reverb Time | 0.1 – 20 s | 2.5 s | Unified tail length — drives FDN feedback and band T60 targets |
| Room Size | 0 – 1 | 0.33 | Scales internal delay-line lengths and diffusion character |
| Pre-Delay | 0 – 500 ms | 0 ms | Global pre-delay with tape-style Doppler glide |
| Proximity | 0 – 1 | 0.5 | 0 = early reflections only (close), 1 = late tail only (far) |
| Wet Mix | 0 – 1 | 1.0 | Master dry/wet blend, per-sample smoothed |

### Character (advanced panel)

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Mod Depth | 0 – 12 ms | 4 ms | LFO modulation on delay lines — breaks modal density |
| Space | 0 – 2 | 1.0 | Stereo width of the late tail |
| ER Length | 20 – 200 ms | 80 ms | Early reflection cluster duration |
| ER Density | 500 – 8000 Hz | 3000 Hz | Velvet-noise tap density in the ER stage |

### Decay curve

Three multipliers shape per-band T60 **relative to Reverb Time**:

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| Bass Decay | 0.5 – 3.0× | 1.4× | Low-frequency tail length |
| Mid Decay | 0.5 – 2.0× | 1.0× | Midrange reference |
| HF Decay | 0.05 – 1.0× | 0.2× | High-frequency rolloff (air vs warmth) |

**Example:** Reverb Time = 3 s, Bass Decay = 2.0× → bass T60 ≈ 6 s. HF Decay = 0.2× → treble T60 ≈ 0.6 s.

---

## Factory presets

| Preset | Character |
|--------|-----------|
| Small Room | Tight, bright, lively — hard surfaces, short tail |
| Studio Plate | Classic plate: dense, smooth, moderate length |
| Drum Room | Punchy ER field, strong transients |
| Large Hall | Concert hall with pronounced early reflections |
| Cathedral | Maximum size, very long tail, rolling treble cut |

---

## Architecture

```
Dry L/R
  │
  ├──► FractionalDelayLine (Hermite) ──► pre-delay
  │
  ├──► EarlyReflections (velvet noise, independent L/R)
  │         └── erGain = cos(proximity × π/2)
  │
  └──► TV-FDN (16 channels)
            prime-length delay lines
            FWHT orthogonal mixing
            multi-phase wavetable LFOs
            per-channel AbsorptionBank (inside feedback loop)
            └── tailGain = sin(proximity × π/2)
  │
  └──► wet mix (per-sample smoothed) ──► Output L/R
```

The DSP core (`tvfdn_dsp`) is JUCE-free and testable without the plugin shell. The JUCE layer (`Plugin/`) provides APVTS parameter management, preset loading, and the WebView UI.

---

## Project structure

```text
DSP/           Core reverb engine (no JUCE dependency)
Plugin/        JUCE processor, editors, presets, WebUI bridge
WebUI/         HTML/CSS/JS plugin interface (embedded at build time)
Tools/         batch_render CLI, embed_html.py
Tests/         DSP smoke tests
```

---

## Support

- **Website:** https://tiaaudio.com
- **Email:** info@tiaaudio.com

---

## Development status

| Phase | Status | Summary |
|-------|--------|---------|
| 1 | ✅ | TV-FDN core: Hermite FDL, prime delays, FWHT, wavetable LFO |
| 2 | ✅ | Velvet-noise ER, ReverbEngine facade, pre-delay, proximity |
| 3 | ✅ | Absorption banks inside FDN loop, unified reverb time |
| 4 | ✅ | JUCE VST3/Standalone shell, APVTS, web UI, factory presets |
| 5 | ✅ | Interactive decay curve, native fallback editor |
