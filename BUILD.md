# Build guide (Windows)

Project root:

```text
c:\Users\TIA\Desktop\reverb_plugin
```

This document covers building the **VST3 plugin** and **Standalone app** for TiaVerb.

---

## Prerequisites

| Tool | Purpose |
|------|---------|
| **CMake 3.22+** | Configure and generate the Visual Studio solution |
| **Visual Studio 2022** (or Build Tools) with **Desktop development with C++** | MSVC compiler + Windows SDK |
| **Python 3** | Regenerates `Plugin/WebUIContent.h` from `WebUI/plugin_ui.html` at build time |
| **WebView2 Runtime** | Required at **run time** for the web UI (usually already installed on Windows 10/11) |

Optional:

- **JUCE submodule** at `JUCE/` — if missing, CMake downloads JUCE 8.0.6 automatically (needs internet on first configure).

Verify tools in PowerShell:

```powershell
cmake --version
python --version
```

If `cmake` is not recognized, either add `C:\Program Files\CMake\bin` to your PATH, or use the full path:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --version
```

---

## One-time setup (configure)

Open **PowerShell** in the project folder:

```powershell
cd c:\Users\TIA\Desktop\reverb_plugin

cmake -B build -DCMAKE_BUILD_TYPE=Release
```

This creates `build\ProReverb.sln` and all targets.

Re-run configure only when you change `CMakeLists.txt`, add/remove source files listed there, or need a clean rebuild after major changes.

### Clean reconfigure (optional)

```powershell
Remove-Item -Recurse -Force build
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

---

## Build the plugin targets

### VST3 + Standalone (recommended)

```powershell
cmake --build build --config Release --target ProReverb_VST3 ProReverb_Standalone
```

### VST3 only

```powershell
cmake --build build --config Release --target ProReverb_VST3
```

### Standalone only

```powershell
cmake --build build --config Release --target ProReverb_Standalone
```

### Everything (plugins + tools + smoke tests)

```powershell
cmake --build build --config Release
```

---

## Where the built files are

After a **Release** build:

### VST3 (install this folder in your DAW)

**Bundle folder** (copy this entire directory into your VST3 folder):

```text
c:\Users\TIA\Desktop\reverb_plugin\build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3
```

**Actual plugin binary** (inside the bundle — do not copy this file alone):

```text
c:\Users\TIA\Desktop\reverb_plugin\build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3\Contents\x86_64-win\TiaVerb.vst3
```

### Standalone app

```text
c:\Users\TIA\Desktop\reverb_plugin\build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe
```

Run directly:

```powershell
.\build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe
```

---

## Install VST3 in a DAW

1. Build the VST3 target (see above).
2. Copy the **whole** `TiaVerb.vst3` **folder** (the bundle at the path above) into your system VST3 directory, for example:

```text
C:\Program Files\Common Files\VST3\
```

3. Rescan plugins in your DAW (or restart the DAW).

> Copy the outer `TiaVerb.vst3` bundle folder, not only the inner `Contents\x86_64-win\TiaVerb.vst3` file.

---

## Web UI changes

If you edit `WebUI\plugin_ui.html` or fonts in `WebUI\fonts\`, either:

- Rebuild any plugin target (CMake runs `Tools\embed_html.py` automatically when Python is available), **or**
- Regenerate manually:

```powershell
python Tools\embed_html.py
cmake --build build --config Release --target ProReverb_VST3 ProReverb_Standalone
```

---

## Other targets

### Batch render tool (offline WAV renders)

```powershell
cmake --build build --config Release --target batch_render
```

Output:

```text
c:\Users\TIA\Desktop\reverb_plugin\build\Release\batch_render.exe
```

Run:

```powershell
.\build\Release\batch_render.exe audio.wav renders
```

### DSP smoke tests (no plugin UI)

```powershell
cmake --build build --config Release --target decay_smoke_test
.\build\Release\decay_smoke_test.exe
```

---

## Quick reference

| What | CMake target | Output |
|------|----------------|--------|
| **VST3 bundle** | `ProReverb_VST3` | `build\ProReverb_artefacts\Release\VST3\TiaVerb.vst3` |
| **Standalone** | `ProReverb_Standalone` | `build\ProReverb_artefacts\Release\Standalone\TiaVerb.exe` |
| Batch render | `batch_render` | `build\Release\batch_render.exe` |
| Decay smoke test | `decay_smoke_test` | `build\Release\decay_smoke_test.exe` |

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `cmake` not found | Install CMake or use `& "C:\Program Files\CMake\bin\cmake.exe"` |
| Configure fails: no compiler | Install Visual Studio **Desktop development with C++** workload |
| UI is blank | Install [Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/web-view2/) |
| DAW does not see plugin | Copy the full `TiaVerb.vst3` **folder** to `C:\Program Files\Common Files\VST3\` and rescan |
| HTML changes not visible | Run `python Tools\embed_html.py` then rebuild plugin targets |
