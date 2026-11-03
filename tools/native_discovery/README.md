# Native Renderer Discovery Tools

Scripts to discover Beautiful Katamari-specific render function addresses,
vertex formats, and constant bank layouts needed by the native renderer
(`src/native/`).

## find_render_funcs.py

Scans the recompiled guest code (BKRecomp/ppc/ppc_recomp.*.cpp) for functions
that are candidates for native renderer hooks. Looks for D3D-style name
patterns in comments.

Usage:

```sh
python3 tools/native_discovery/find_render_funcs.py \
    /mnt/sdb/bkrecomp/BKRecomp/ppc \
    tools/native_discovery/discovered_functions.toml
```

## find_render_funcs2.py (recommended)

Scans the recompiled guest code for functions with render-like patterns:

- **Indirect calls** (`PPC_CALL_INDIRECT_FUNC`) — virtual dispatch typical of
  render engine virtual calls
- **MMIO access** — GPU register ranges (0x7Fxxxxxx, 0x7Exxxxxx, 0x8000xxxx)
- **Buffer bind patterns** — SetStreamSource, SetIndices, SetShader, etc.
- **Register write density** — functions that write many guest registers

Scores each function and ranks candidates for hooking.

Usage:

```sh
python3 tools/native_discovery/find_render_funcs2.py \
    /mnt/sdb/bkrecomp/BKRecomp/ppc \
    tools/native_discovery/discovered_functions.toml
```

## Currently hooked functions

Discovered manually from `BKRecomp/ppc/ppc_recomp.116.cpp`:

| Guest function | Address/Location | Purpose |
|----------------|------------------|---------|
| `__imp__VdSwap` | Called at ppc_recomp.116.cpp:28182 | Present/swap — frame boundary |
| `__imp__VdInitializeRingBuffer` | ppc_recomp.116.cpp:6840 | GPU ring buffer init |
| `__imp__VdEnableRingBufferRPtrWriteBack` | ppc_recomp.116.cpp:6871 | Ring buffer read pointer writeback |
| `sub_824E04B0` | ppc_recomp.116.cpp | Pre-swap ring buffer processing (capture) |

## Next steps after discovery

1. Review discovered addresses and add confirmed hooks to
   `src/native/bk_native_render.cpp`
2. Determine BK's vertex formats, constant bank layouts, and texture binding
   conventions by reverse engineering the guest's draw functions
3. Implement the Vulkan rendering pipeline in
   `src/native/bk_native_scene_gpu.cpp`
4. Write BK-specific HLSL shaders in `src/shaders/bk_native_shaders.hlsl`

## Relationship to skate3recomp

The architecture is inspired by
[skate3recomp](https://github.com/mchughalex/skate3recomp)'s native renderer
(`skate3_native_render.cpp`, `skate3_native_scene.cpp`,
`skate3_native_scene_gpu.cpp`) but rewritten for Beautiful Katamari's guest
render pipeline. Skate 3 and BK use different game engines, so function
addresses, vertex formats, and constant layouts are entirely different.
