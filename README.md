# BKReXGlue — Beautiful Katamari Native Recompilation

**Beautiful Katamari** (Xbox 360, Title ID `4E4D07DC`) statically recompiled with the
[rexglue-sdk](https://github.com/rexglue/rexglue-sdk) toolkit.

This project is a sibling rewrite of the original `BKRecomp/` project (which used
XenonRecomp plus a hand-written runtime). `BKRecomp/` is left untouched as the
reference; everything here lives under `BKReXGlue/`.

## What this is

| | BKRecomp (old) | BKReXGlue (this) |
|---|---|---|
| Recompiler | XenonRecomp (block-based) | rexglue `codegen` |
| Runtime (kernel/threads/imports) | hand-written (`runtime/*.cpp`) | rexglue runtime library |
| GPU | hand-written swapchain (`runtime/gpu.cpp`, `display.cpp`) | rexglue **xenos** GPU plugin (Vulkan) + presenters |
| Native renderer | none | `src/native/` (Vulkan, inspired by skate3recomp) |
| App entry | custom `main.cpp` | `rex::ReXApp` subclass (`src/beautiful_katamari_app.h`) |
| Game-specific artifact | block dump + runtime patches | `codegen` config (`bk_*.toml`) |

## Native Renderer (new)

A data-driven native GPU renderer for Beautiful Katamari, located in
`src/native/`. Heavily inspired by
[skate3recomp](https://github.com/mchughalex/skate3recomp)'s native renderer
architecture, but rewritten entirely for Beautiful Katamari's guest render
pipeline.

### Features

- **Guest render hooks**: Intercepts `__imp__VdSwap` (frame boundary),
  `sub_824E04B0` (pre-swap capture), and ring buffer init functions
- **Frame pacing**: Caps guest FPS to smooth VRR judder (`bk_guest_fps_cap`)
- **Fault-guarded guest reads**: SIGSEGV recovery on Linux so the renderer
  can safely read guest memory that may be revoked mid-frame
- **Scene capture**: Builds a compact `NativeFrameScene` from hooked draw calls
- **Vulkan GPU renderer skeleton**: Ready for pipeline/shader implementation
- **Hot-toggle**: Switch between native and emulated renderers at runtime
- **GUI configuration utility**: ImGui config dialog toggled with **F1** for live cvar editing and save-to-config

### Cvars

| Cvar | Type | Default | Description |
|------|------|---------|-------------|
| `bk_native_render` | bool | `true` | Enable the native renderer hook layer |
| `bk_native_render_scene` | bool | `true` | Actually render natively (replaces emulated output) |
| `bk_native_render_scene_debug` | int32 | `0` | Debug viz: 0=normal, 1=clear, 2=solid/item, 3=limit 32, 4=no depth |
| `bk_guest_fps_cap` | double | `0.0` | Cap guest frame rate (0 = uncapped) |

Native rendering is enabled by default (`bk_native_render = true`,
`bk_native_render_scene = true`). To fall back to the emulated xenos GPU path:

```sh
./beautiful_katamari --game_data_root="..." \
  --cvar bk_native_render=0 --cvar bk_native_render_scene=0
```

See [`NATIVE_RENDERER.md`](NATIVE_RENDERER.md) for full documentation on the
native renderer, including hooked function addresses, discovery tools, and
what remains to be implemented.

## Build & run

### Prerequisites

- `cmake >= 3.25`, `ninja`, `clang-18`
- The rexglue SDK (prebuilt at `sdk/linux-amd64/`)
- Game data: `Beautiful Katamari (USA) (En,Ja)/` directory

### Configure

```sh
cd /mnt/sdb/bkrecomp/BKReXGlue
cmake -S . -B out/build/linux-amd64-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
      -DCMAKE_PREFIX_PATH=/mnt/sdb/bkrecomp/sdk/linux-amd64
```

### Build

```sh
cmake --build out/build/linux-amd64-release -j3
```

### Run

```sh
cd out/build/linux-amd64-release
./beautiful_katamari --game_data_root="/path/to/Beautiful Katamari (USA) (En,Ja)"
```

### Run with native renderer

```sh
./beautiful_katamari --game_data_root="..." \
  --cvar bk_native_render=1 --cvar bk_native_render_scene=1
```

Detailed logs are written per run to `out/build/linux-amd64-release/logs/*.log`.

## Project layout

```
BKReXGlue/
├── beautiful_katamari_manifest.toml   # rexglue project manifest
├── bk_config.toml                     # manual function-boundary overrides
├── bk_gapfill_thunks.toml             # auto-recovered GapFill-swallowed functions
├── bk_jumptable_fix.toml              # jump-table register fixes
├── bk_switch_tables.toml              # reference only (not wired in)
├── CMakeLists.txt                     # host target + xenos plugin + runtime staging
├── CMakePresets.json                  # stock rexglue presets
├── NATIVE_RENDERER.md                 # native renderer documentation
├── README.md                          # this file
├── src/
│   ├── main.cpp                       # REX_DEFINE_APP
│   ├── beautiful_katamari_app.h       # ReXApp subclass (installs native renderer hooks)
│   ├── window_title.cpp               # live FPS/title bar
│   ├── window_title.h
│   ├── audio_fix.cpp                  # XMA loop_count workaround
│   └── video_fix.cpp                  # overscan workaround
├── src/native/                        # Native renderer (new)
│   ├── bk_config_dialog.h             # ImGui configuration dialog header
│   ├── bk_config_dialog.cpp           # Config dialog (toggle with F1)
│   ├── bk_native_render.h
│   ├── bk_native_render.cpp           # Guest hooks + cvars + frame pacing
│   ├── bk_native_scene.h              # Scene data structures
│   ├── bk_native_scene.cpp            # Scene builder
│   ├── bk_native_scene_gpu.h          # GPU renderer interface
│   ├── bk_native_scene_gpu.cpp        # GPU renderer (Vulkan skeleton)
│   ├── bk_native_guest_read.h         # Fault-guarded guest memory access
│   ├── bk_native_guest_read.cpp       # SIGSEGV recovery (Linux)
│   ├── bk_native_palette.h            # Bone palette / world matrix capture
│   └── bk_native_palette.cpp          # Palette/matrix capture
├── src/shaders/
│   └── bk_native_shaders.hlsl         # Placeholder HLSL shaders
├── generated/                         # rexglue codegen output (auto-generated)
├── tools/
│   └── native_discovery/              # Render function discovery scripts
│       ├── find_render_funcs.py
│       ├── find_render_funcs2.py
│       └── README.md
└── out/build/linux-amd64-release/     # build tree
```

## Status

- Codegen recovers **37k+ functions**, no unresolved fatal errors
- Game boots and runs: SDL/Vulkan, 1920×1080 swapchain, XEX loads, kernel initializes, GPU pipelines created
- 124 graphics pipelines created at boot
- Audio, video, and input workarounds applied
- Native renderer enabled **by default** (`bk_native_render = true`, `bk_native_render_scene = true`)
- Vulkan pipeline implementation pending (skeleton in place)

## Repository

This project is published separately from
[skate3recomp](https://github.com/mchughalex/skate3recomp), which served as
the architectural inspiration for the native renderer. The native renderer
architecture (hook layer → scene builder → GPU renderer → presenter) follows
skate3recomp's approach but is rewritten entirely for Beautiful Katamari's
guest render pipeline — function addresses, vertex formats, constant bank
layouts, and shaders are all BK-specific.

## GitHub Setup

To publish this repository:

```sh
# Create a new repository on GitHub (e.g. beautiful-katamari-recomp),
# then add it as a remote:
git remote add origin https://github.com/YOUR_USERNAME/beautiful-katamari-recomp.git
git branch -M main
git push -u origin main
```

## License

BSD 3-Clause. See LICENSE in the rexglue-sdk project root.

## License

BSD 3-Clause. See LICENSE in the rexglue-sdk project root.