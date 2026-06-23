# TiaVerb

An algorithmic reverb plugin I'm building as part of my **Communications and Multimedia Engineering (CME) master's** at **FAU Erlangen-Nürnberg**.

This is a **work-in-progress student project**, not a finished commercial product. The DSP and UI are functional enough to load in a DAW and experiment with, but tuning, edge cases, and polish are still ongoing. Feedback and bug reports are welcome.

**Current status:** early beta — builds and runs on Windows; sound quality and UX are still being iterated.

| | |
|---|---|
| **Version** | 0.x (development) |
| **Formats** | VST3, Standalone (Windows) |
| **Stack** | C++20, JUCE 8, custom DSP (no JUCE in the audio core) |

---

## What it is (so far)

I'm trying to learn how modern algorithmic reverbs work by implementing one from scratch, inspired by papers and plugins like FabFilter Pro-R. The signal path is roughly:

- **TV-FDN** — 16-channel time-varying feedback delay network (prime delays, FWHT mixing, LFO modulation)
- **Velvet-noise early reflections** — sparse ER field for the first ~80 ms
- **Frequency-dependent decay** — per-band absorption inside the FDN feedback loop
- **Proximity** — crossfade between ER (close) and tail (far)
- **Simple web UI** — HTML/CSS/JS inside JUCE's WebView (Windows/WebView2)

There's also a small **batch_render** tool to process WAV files offline for A/B testing without opening a DAW.

---

## Quick start (if you want to try it)

### Standalone

```powershell
.\build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe
```

### VST3

Copy the bundle into your DAW's VST3 folder and rescan:

```text
build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3
→ C:\Program Files\Common Files\VST3\
```

It should show up as **TiaVerb**. If it doesn't load, check that WebView2 runtime is installed (needed for the UI on Windows).

### Offline renders

```powershell
.\build\Release\batch_render.exe audio.wav renders
```

Writes 9 WAV files (distance × stereo width grid) to `renders\`. Handy for comparing settings without real-time playback.

Full build steps: [BUILD.md](BUILD.md).

---

## Building from source

### What you need

| Requirement | Notes |
|-------------|-------|
| CMake | ≥ 3.22 |
| C++ compiler | MSVC 2022 on Windows; C++20 required |
| JUCE | 8.0.x (submodule, `JUCE_DIR`, or auto-download) |
| WebView2 SDK | NuGet package — only for the web UI on Windows |

### WebView2 (Windows, one-time)

```powershell
Register-PackageSource -provider NuGet -name nugetRepository -location https://www.nuget.org/api/v2
Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -RequiredVersion 1.0.1901.177 -Source nugetRepository
```

### JUCE

```bash
# submodule (offline-friendly)
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git submodule update --init --recursive

# or point CMake at an existing install
cmake -B build -DJUCE_DIR=C:/path/to/JUCE
```

If neither is set, CMake downloads JUCE 8.0.6 via FetchContent.

### Build

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

| Target | Output |
|--------|--------|
| `ProReverb_VST3` | `build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3` |
| `ProReverb_Standalone` | `build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe` |
| `batch_render` | `build\Release\batch_render.exe` |

### DSP tests (no plugin UI)

```powershell
cmake -B build -DBUILD_VST3=OFF
cmake --build build --config Release
.\build\Release\tvfdn_smoke_test.exe
.\build\Release\reverb_smoke_test.exe
.\build\Release\decay_smoke_test.exe
```

All three should print `PASS`.

---

## Parameters (current)

### Main

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Reverb Time | 0.1 – 20 s | 2.5 s | Drives FDN feedback + band decay targets |
| Room Size | 0 – 1 | 0.33 | Scales delay lengths / diffusion |
| Pre-Delay | 0 – 500 ms | 0 ms | With Doppler-style glide on changes |
| Proximity | 0 – 1 | 0.5 | ER ↔ tail blend |
| Wet Mix | 0 – 1 | 1.0 | Dry/wet |

### Character (advanced panel)

| Parameter | Range | Default |
|-----------|-------|---------|
| Mod Depth | 0 – 12 ms | 4 ms |
| Space (stereo width) | 0 – 2 | 1.0 |
| ER Length | 20 – 200 ms | 80 ms |
| ER Density | 500 – 8000 Hz | 3000 Hz |

### Decay curve (multipliers × Reverb Time)

| Parameter | Range | Default |
|-----------|-------|---------|
| Bass Decay | 0.5 – 3.0× | 1.4× |
| Mid Decay | 0.5 – 2.0× | 1.0× |
| HF Decay | 0.05 – 1.0× | 0.2× |

Five factory presets (Small Room, Studio Plate, Drum Room, Large Hall, Cathedral) — starting points, not final.

---

## Signal flow (simplified)

```
Dry L/R
  ├── pre-delay (Hermite fractional delay)
  ├── early reflections (velvet noise)  ── proximity → ER gain
  └── TV-FDN (16 ch, absorption in loop) ── proximity → tail gain
  └── wet mix → out
```

DSP lives in `DSP/` with no JUCE dependency. `Plugin/` is the JUCE wrapper + UI.

---

## Project layout

```text
DSP/           Reverb engine
Plugin/        JUCE processor, presets, WebUI bridge
WebUI/         HTML/CSS/JS interface
Tools/         batch_render, embed_html.py
Tests/         smoke tests
```

---

## Roadmap / known gaps

Things I'm still working on or haven't done yet:

- [ ] More listening tests and preset tuning
- [ ] macOS/Linux builds and UI testing
- [ ] Better parameter smoothing under heavy automation
- [ ] Documentation of DSP math (for the thesis write-up)
- [ ] CPU profiling and optimization pass

| Milestone | Status |
|-----------|--------|
| TV-FDN core | working |
| Early reflections + proximity | working |
| Frequency-dependent decay | working |
| JUCE shell + VST3/Standalone | working |
| Web UI + presets | working, needs polish |
| "Done" / thesis-ready | **not yet** |

---

## About

**Author:** TIA — CME master's student, FAU Erlangen-Nürnberg  
**Context:** personal learning project + thesis-related DSP work  
**License:** not released for commercial use; code shared for portfolio / academic purposes unless stated otherwise

If something breaks or sounds wrong, that's expected at this stage — but I'd still like to hear about it.
