# Beautiful Katamari — Native Renderer

A data-driven native GPU renderer for Beautiful Katamari (Xbox 360), written
as a replacement for the emulated xenos GPU path in BKReXGlue. Heavily
inspired by [skate3recomp](https://github.com/mchughalex/skate3recomp)'s
native renderer architecture, but rewritten entirely for Beautiful Katamari's
guest render pipeline.

## Architecture

The renderer sits alongside the existing rexglue SDK + xenos GPU plugin. It
hooks guest render functions, captures per-draw state, builds a compact
native scene graph, and renders it with Vulkan — either alongside or in place
of the emulated output.

```
┌─────────────────────────────────────────────────────────────┐
│  Guest render thread (recompiled PPC)                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ VdSwap hook │  │ sub_824E04B0│  │ Original guest code │ │
│  │ (frame end) │  │ (capture)   │  │ runs unmodified     │ │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘ │
│         │                │                     │            │
│         └────────────────┼─────────────────────┘            │
│                          ▼                                   │
│              Captured draw records (SubmitRecord)            │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  Native scene builder (bk_native_scene.cpp)                  │
│  • Fault-guarded guest memory reads                          │
│  • Bone palette / world matrix capture                       │
│  • FrameScene assembly                                       │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  Native GPU renderer (bk_native_scene_gpu.cpp)               │
│  • Vulkan pipelines (TODO: implement)                        │
│  • Vertex/index buffer upload                                │
│  • Draw via rex::ui::Presenter guest output                  │
└─────────────────────────────────────────────────────────────┘
```

## Files

| File | Purpose |
|------|---------|
| `src/native/bk_native_render.h` | Public interface + `PaceGuestFrame()` |
| `src/native/bk_native_render.cpp` | Guest render hooks + cvars + frame pacing |
| `src/native/bk_native_scene.h` | Scene data structures (`DrawItem`, `FrameScene`, `SubmitRecord`) |
| `src/native/bk_native_scene.cpp` | Scene builder — captures guest state into `NativeFrameScene` |
| `src/native/bk_native_scene_gpu.h` | GPU renderer interface |
| `src/native/bk_native_scene_gpu.cpp` | GPU renderer skeleton (Vulkan TBD) |
| `src/native/bk_native_guest_read.h` | Fault-guarded guest memory access interface |
| `src/native/bk_native_guest_read.cpp` | SIGSEGV-recovery implementation (Linux) / memcpy (Windows) |
| `src/native/bk_native_palette.h` | Bone palette / world matrix capture interface |
| `src/native/bk_native_palette.cpp` | Palette/matrix capture implementation |
| `src/native/bk_config_dialog.h` | ImGui configuration dialog header |
| `src/native/bk_config_dialog.cpp` | ImGui configuration dialog (toggle with F1) |
| `src/shaders/bk_native_shaders.hlsl` | Placeholder HLSL shaders |
| `tools/native_discovery/find_render_funcs.py` | Basic function discovery |
| `tools/native_discovery/find_render_funcs2.py` | Pattern-based render function discovery |
| `tools/native_discovery/README.md` | Discovery tool documentation |
| `NATIVE_RENDERER.md` | This document |

## Hooked Guest Functions

Discovered from `BKRecomp/ppc/ppc_recomp.116.cpp`:

| Guest function | Address | Purpose |
|----------------|---------|---------|
| `__imp__VdSwap` | Called at ppc_recomp.116.cpp:28182 | Present/swap — frame boundary hook |
| `__imp__VdInitializeRingBuffer` | ppc_recomp.116.cpp:6840 | GPU ring buffer init |
| `__imp__VdEnableRingBufferRPtrWriteBack` | ppc_recomp.116.cpp:6871 | Ring buffer read pointer writeback |
| `sub_824E04B0` | ppc_recomp.116.cpp | Pre-swap ring buffer processing (capture) |

### VdSwap hook (frame boundary)

The `VdSwap` hook runs on every guest frame:

1. **Pre-swap**: Paces the guest frame rate (`bk_guest_fps_cap`)
2. Calls the original `VdSwap` (emulated path still runs)
3. **Post-swap**: Builds the native scene from captured records; if `bk_native_render_scene` is enabled, renders via the native GPU renderer

### sub_824E04B0 hook (capture)

Called just before `VdSwap`. Captures a `SubmitRecord` for the frame's draw submission.

## Configuration Utility (GUI)

A built-in ImGui configuration dialog is available, toggled with **F1**. It
lists all registered cvars grouped by category (Graphics, Native Renderer,
Audio, etc.) with live value editing.

### Features

- **Browse all cvars**: Every registered cvar is shown with its name, description, current value, and type
- **Live editing**: Boolean (checkbox), integer, double, and string cvars can be edited in-place; changes take effect immediately for hot-reload cvars
- **Search**: Filter cvars by name or description using the search bar
- **Category grouping**: Cvars are grouped by their category (e.g. "Beautiful Katamari", "Skate 3")
- **Restart-required flagging**: Cvars with `Lifecycle::kRequiresRestart` are marked with `[restart]` and shown in orange
- **Constraints**: Range constraints (min/max) are displayed when available
- **Save to config**: Writes all current cvar values to `beautiful_katamari.toml` next to the executable
- **Reset to defaults**: Resets all cvars to their compiled-in default values

### Usage

1. Launch the game: `./beautiful_katamari --game_data_root="..."`
2. Press **F1** to open the configuration dialog
3. Edit cvars as needed
4. Click "File → Save to config" to persist changes to `beautiful_katamari.toml`
5. Press **F1** again to close

### Cvars Added by the Native Renderer

| Cvar | Type | Default | Description |
|------|------|---------|-------------|
| `bk_native_render` | bool | `true` | Enable the native renderer hook layer |
| `bk_native_render_scene` | bool | `true` | Actually render natively (replaces emulated output) |
| `bk_native_render_scene_debug` | int32 | `0` | Debug viz: 0=normal, 1=clear, 2=solid/item, 3=limit 32, 4=no depth |
| `bk_guest_fps_cap` | double | `0.0` | Cap guest frame rate (0 = uncapped) |

## CVars

| Cvar | Type | Default | Description |
|------|------|---------|-------------|
| `bk_native_render` | bool | `false` | Enable the native renderer hook layer |
| `bk_native_render_scene` | bool | `false` | Actually render natively (replaces emulated output) |
| `bk_native_render_scene_debug` | int32 | `0` | Debug: 0=normal, 1=clear, 2=solid/item, 3=limit 32, 4=no depth |
| `bk_guest_fps_cap` | double | `0.0` | Cap guest frame rate (0 = uncapped) |

Enable at runtime:
```sh
./beautiful_katamari --game_data_root="..." --cvar bk_native_render=1 --cvar bk_native_render_scene=1
```

Or set in `out/build/linux-amd64-release/beautiful_katamari.toml`:
```toml
bk_native_render = true
bk_native_render_scene = true
```

## Build

```sh
cd /mnt/sdb/bkrecomp/BKReXGlue
cmake -S . -B out/build/linux-amd64-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
      -DCMAKE_PREFIX_PATH=/mnt/sdb/bkrecomp/sdk/linux-amd64
cmake --build out/build/linux-amd64-release -j3
```

The native renderer hooks are compiled in but **inactive by default**.

## What's Implemented vs. TBD

### ✅ Implemented

- Guest render hook layer with `REX_FUNC` overrides for `__imp__VdSwap`, `__imp__VdInitializeRingBuffer`, `__imp__VdEnableRingBufferRPtrWriteBack`, `sub_824E04B0`
- Frame pacing (`bk_guest_fps_cap`) with absolute-schedule timing
- Fault-guarded guest memory reads (SIGSEGV recovery on Linux)
- Scene data structures (`DrawItem`, `FrameScene`, `SubmitRecord`)
- Scene builder skeleton with capture hooks
- GPU renderer skeleton (install/render/shutdown interface)
- CMake integration (native sources added to build)
- Discovery tools for finding BK render functions

### 🔲 TBD (needs BK reverse engineering)

- **Vulkan pipeline creation** in `bk_native_scene_gpu.cpp` — create pipelines, descriptor sets, render passes
- **Vertex/index buffer upload** — read guest VB/IB addresses and upload to GPU
- **Bone palette / world matrix serving** — implement `ServeEntityWorldMatrix`, `ReadItemFade` with BK-specific entity offsets
- **BK-specific shaders** — port BK's Xenos shaders to HLSL/SPIR-V in `bk_native_shaders.hlsl`
- **Texture binding** — map guest texture addresses to Vulkan sampler descriptors
- **Render target selection** — choose which guest renders to replace natively vs. emulate

## Discovery

Run the discovery tool to find more render function candidates:

```sh
python3 tools/native_discovery/find_render_funcs2.py \
    /mnt/sdb/bkrecomp/BKRecomp/ppc \
    tools/native_discovery/discovered_functions.toml
```

Review the output, then add confirmed hooks to `src/native/bk_native_render.cpp`.

## Relationship to skate3recomp

The architecture (hook layer → scene builder → GPU renderer → presenter) is
inspired by [skate3recomp](https://github.com/mchughalex/skate3recomp)'s
native renderer (`skate3_native_render.cpp`, `skate3_native_scene.cpp`,
`skate3_native_scene_gpu.cpp`). However:

- **Function addresses are different**: BK and Skate 3 use different engines
- **Vertex formats differ**: BK's Xenos vertex declarations need re-discovery
- **Constant bank layouts differ**: BK's VS/PS constant registers are TBD
- **Shader code differs**: BK's shaders must be ported from its own Xenos binaries

Treat skate3recomp as an architectural reference, not a code donor.
