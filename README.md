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

I'm trying to learn how modern algorithmic reverbs work by implementing one from scratch, inspired by papers and plugins like FabFilter Pro-R.

**The active reverb engine is Dattorro-based**, not FDN-based. After a long stretch of experimenting with a 16-channel time-varying FDN, I switched back to a **figure-eight plate reverb** (Dattorro topology) because it gave a smoother, more musical late tail for this project. The FDN work is still in the repo as **legacy / reference code** — useful for my thesis notes and future experiments, but it is **not wired into the plugin** anymore.

### Active signal path (production)

- **Pre-delay** — Hermite fractional delay (L/R)
- **Input decorrelation** — asymmetric R-channel offset + staggered allpass stages (breaks up mono buildup before the tank)
- **Early reflections** — lightweight multi-tap delay network (`ErTapNetwork`, 5–45 ms, exponential decay, HF shelf)
- **ER → late feed** — a fraction of the ER output is injected into the Dattorro tank input for acoustic glue
- **Dattorro late tail** — stereo input diffusers, modulated in-loop allpasses, HPF+LPF damping, 6-tap signed wet extraction
- **Wet mix** — 40 % ER + 60 % late tail (internal, hardcoded for now)
- **Web UI** — HTML/CSS/JS in JUCE WebView2 with a live damping/decay visualizer

There's also a small **batch_render** tool to process WAV files offline for A/B testing without opening a DAW.

### Legacy FDN path (not in use)

These files remain in `DSP/` but are **not linked** into `tvfdn_dsp` or the plugin:

| Component | Files | Notes |
|-----------|-------|-------|
| TV-FDN engine | `TVFDNEngine.*`, `AdvancedFDN.*` | 16-ch FDN, Householder mix, modulated in-loop APs |
| Velvet-noise ER | `EarlyReflections.*`, `VelvetNoiseGenerator.*` | Earlier ER approach |
| Absorption bank | `AbsorptionBank.*`, `BiquadFilter.*` | Per-band decay inside FDN loop |
| Support | `WavetableLFO.h`, `FeedbackAllpass.h`, `OrthogonalMatrix.h` | FDN-era utilities |

If you're browsing the repo, start with `ReverbEngine` → `DattorroReverb` + `ErTapNetwork`. The FDN folder is archival.

---

## History (FDN → Dattorro)

This section is mainly for thesis / portfolio context — it explains *why* the repo contains two different reverb architectures.

### Phase 1 — TV-FDN as the main engine

The project started with a **16-channel time-varying feedback delay network (TV-FDN)**, which is a common research path when you want flexible, “modern” algorithmic reverb:

- Prime-length delay lines with **LFO modulation** on in-loop allpasses
- **Householder matrix** mixing (after experimenting with FWHT)
- **Velvet-noise early reflections** and a **proximity** control to crossfade ER vs tail
- **Frequency-dependent decay** via an absorption filter bank inside the feedback loop

That path taught me a lot about FDN stability, modulation limits, and how much work it takes to make an FDN sound *neutral* rather than just *dense*. Smoke tests and offline renders (`batch_render`, `decay_smoke_test`) were built around this engine.

### Phase 2 — What pushed the switch

Listening tests and impulse-response checks exposed recurring issues I couldn't fully tune away in the time I had:

| Symptom | Likely cause (simplified) |
|---------|---------------------------|
| Metallic / ringing tail | Modulated delay lines + dense feedback matrix under some settings |
| Weak spatial image on mono sources | Mono sum into a single diffusion path before the tank |
| “Laboratory” late field | Strong on diffusion math, weaker on perceptual room cues (ER localization, HF roll-off on walls) |

I iterated on the FDN (absolute modulation caps, staggered diffuser coeffs, asymmetric L/R delays, multi-sinusoid LFOs). That improved things, but the **late tail still felt more academic than musical** compared to reference plugins.

### Phase 3 — Dattorro late tail + new ER stack

I **switched the production path back to a Dattorro figure-eight plate** for the diffuse tail — a topology with a long track record of smooth, plate-like decay. On top of that I rebuilt the “front end” of the reverb:

1. **Input decorrelation** before the tank (R-channel delay + asymmetric allpasses)
2. **`ErTapNetwork`** — sparse multi-tap ER (replacing velvet-noise ER in the active path)
3. **Parallel ER + late mix** (40 / 60) plus **ER → late feed** (~25 %) so reflections bleed into the diffuse field

`ReverbEngine` became the single facade; `PluginProcessor` has pointed at it ever since.

### Phase 4 — UI and visualization

The web UI moved from basic knobs to a **response visualizer** (damping high-cut curve + decay envelope) so parameters read intuitively instead of as raw Hz / seconds numbers. Presets were simplified to match the five live controls.

### What stayed in the repo (and why)

The FDN code was **not deleted**. It remains as **legacy / reference**:

- Documented design alternatives for the thesis
- A starting point if I revisit FDN-based reverb later
- Historical context for commit history and listening-test notes

**Rule of thumb:** if it's linked in `CMakeLists.txt` under `tvfdn_dsp`, it's live. If it's only under `DSP/` with names like `AdvancedFDN`, `TVFDNEngine`, or `VelvetNoiseGenerator`, it's archival.

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

Renders the input through `ReverbEngine` across a **time × size** grid and writes uniquely named WAV files to `renders\` (or `./batch_output/` by default). Handy for comparing settings without real-time playback.

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

Five knobs in the main UI:

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Time | 0.1 – 20 s | 2.5 s | Late-tail decay (Dattorro feedback gain) |
| Size | 0 – 1 | 0.33 | Scales tank delay lengths |
| Damping | 500 – 20 000 Hz | 8000 Hz | One-pole LPF in tank feedback paths |
| Pre-Delay | 0 – 200 ms | 0 ms | Shared pre-delay before ER + late path |
| Mix | 0 – 1 | 0.35 | Dry/wet |

Four factory presets: **Small Room**, **Lush Plate**, **Large Hall**, **Cathedral** — starting points, not final.

---

## Signal flow (simplified)

```
Dry L/R
  ├── pre-delay (Hermite fractional delay)
  ├── input decorrelation (R offset + asymmetric APs)
  ├── ErTapNetwork (multi-tap ER) ──────────────┐
  │                                              ├── 40 % ER + 60 % late → wet mix → out
  └── decor + 25 % ER feed → Dattorro late tail ┘
```

`Plugin/PluginProcessor.cpp` → `ReverbEngine` → `ErTapNetwork` + `DattorroReverb`.

DSP lives in `DSP/` with no JUCE dependency. `Plugin/` is the JUCE wrapper + WebUI bridge.

---

## Project layout

```text
DSP/
  ReverbEngine.*       Top-level facade (pre-delay, decorrelation, ER/late routing)
  DattorroReverb.*     Figure-eight late reverb tank
  ErTapNetwork.*       Multi-tap early reflections
  FractionalDelayLine.*  Hermite delay (pre-delay, modulated APs)
  [legacy] TVFDNEngine.*, AdvancedFDN.*, EarlyReflections.*, …  FDN era — not linked

Plugin/                JUCE processor, presets, WebUI bridge
WebUI/                 HTML/CSS/JS interface + embedded fonts
Tools/                 batch_render, embed_html.py
Tests/                 smoke tests
```

---

## Roadmap / known gaps

Things I'm still working on or haven't done yet:

- [ ] Expose ER/late balance and decorrelation strength as parameters
- [ ] More listening tests and preset tuning
- [ ] macOS/Linux builds and UI testing
- [ ] Documentation of DSP math (for the thesis write-up)
- [ ] CPU profiling and optimization pass

| Milestone | Status |
|-----------|--------|
| Dattorro late tail + modulated APs | working |
| Multi-tap ER + input decorrelation | working |
| ER → late feed + wet routing | working |
| Web UI + response visualizer | working, needs polish |
| Legacy TV-FDN codebase | kept, not in production path |
| JUCE shell + VST3/Standalone | working |
| "Done" / thesis-ready | **not yet** |

---

## About

**Author:** TIA — CME master's student, FAU Erlangen-Nürnberg  
**Context:** personal learning project + thesis-related DSP work  
**License:** not released for commercial use; code shared for portfolio / academic purposes unless stated otherwise

If something breaks or sounds wrong, that's expected at this stage — but I'd still like to hear about it.
