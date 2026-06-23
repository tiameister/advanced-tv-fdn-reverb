# Build & run commands (Windows)

Project root:

```text
c:\Users\TIA\Desktop\reverb_plugin
```

Requires **CMake** and **Visual Studio** (MSVC) on PATH. Open **PowerShell** in the project folder.

---

## One-time setup (configure)

```powershell
cd c:\Users\TIA\Desktop\reverb_plugin

cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Only run again if you change `CMakeLists.txt` or need a clean configure.

---

## Build individually

### VST3 plugin

```powershell
cmake --build build --config Release --target ProReverb_VST3
```

Output:

```text
build\ProReverb_artefacts\Release\VST3\Tia Reverb.vst3
```

Copy that `.vst3` folder into your DAW’s VST3 directory if needed.

---

### Standalone app (.exe)

```powershell
cmake --build build --config Release --target ProReverb_Standalone
```

Output:

```text
build\ProReverb_artefacts\Release\Standalone\Tia Reverb.exe
```

Run:

```powershell
.\build\ProReverb_artefacts\Release\Standalone\Tia Reverb.exe
```

---

### Batch render tool (offline WAV renders)

```powershell
cmake --build build --config Release --target batch_render
```

Output:

```text
build\Release\batch_render.exe
```

Run (default: reads `audio.wav`, writes to `renders\`):

```powershell
.\build\Release\batch_render.exe audio.wav renders
```

Example output files:

```text
renders\dist_1.0_width_0.0.wav
renders\dist_1.0_width_1.0.wav
...
```

---

## Build everything at once

```powershell
cmake --build build --config Release
```

---

## Optional: DSP smoke tests (no plugin UI)

```powershell
cmake --build build --config Release --target decay_smoke_test
.\build\Release\decay_smoke_test.exe
```

---

## Quick reference

| What        | Build target            | Output path |
|-------------|-------------------------|-------------|
| VST3        | `ProReverb_VST3`        | `build\ProReverb_artefacts\Release\VST3\Tia Reverb.vst3` |
| Standalone  | `ProReverb_Standalone`  | `build\ProReverb_artefacts\Release\Standalone\Tia Reverb.exe` |
| Batch render| `batch_render`          | `build\Release\batch_render.exe` |
