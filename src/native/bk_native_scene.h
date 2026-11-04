// beautiful_katamari - native renderer frame scene.
//
// Data-driven scene renderer: consumes the per-frame MeshContext / draw
// stream collected by bk_native_render.cpp, walks the verified guest
// structures into a compact NativeFrameScene, and renders it with our own
// shaders, replacing the emulated xenos GPU output when active.
//
// Unlike skate3recomp (which targets Skate 3's specific renderengine
// structures), this is written for Beautiful Katamari's own guest render
// pipeline. Function addresses, vertex formats, and constant bank layouts
// are BK-specific and must be discovered from the recompiled guest code
// (see tools/ for discovery scripts).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bk::native {

// One drawable item captured from a guest draw call.
struct DrawItem {
  uint32_t ctx = 0;
  uint32_t vb_obj = 0;
  uint32_t ib_obj = 0;
  uint32_t vb_addr = 0;
  uint32_t ib_addr = 0;
  uint32_t vb_bytes = 0;
  uint32_t ib_count = 0;
  uint32_t diffuse_tex = 0;
  uint32_t lightmap_tex = 0;
  uint16_t pos_offset = 0;
  uint16_t normal_offset = 0;
  uint16_t uv_offset = 0;
  uint16_t color_offset = 0;
  uint16_t stride = 0;
  uint8_t pos_fmt = 0;
  uint8_t normal_fmt = 0;
  uint8_t uv_fmt = 0;
  uint8_t color_fmt = 0;
  bool skinned = false;
  bool unlit = false;
  bool transparent = false;
  std::vector<float> bones;
  float world[16] = {};
  float bbox_min[3] = {0, 0, 0};
  float bbox_max[3] = {0, 0, 0};
  struct DrawEntry {
    uint32_t prim;
    uint32_t base_vertex;
    uint32_t start_index;
    uint32_t index_count;
  };
  std::vector<DrawEntry> draws;
  uint64_t fingerprint = 0;
  bool pending = false;

  // Guest-side vertex/index buffer pointers (resolved at capture time).
  // The renderer reads these from guest memory and uploads to Vulkan.
  const uint8_t* vb_data = nullptr;
  const uint8_t* ib_data = nullptr;
};

// One frame of scene data built on the guest render thread.
struct FrameScene {
  uint64_t generation = 0;
  float view_proj[16] = {};
  float cam_pos[3] = {};
  float proj[16] = {};
  float fog_ramp[4] = {0.0f, 0.0f, 1.0f, 0.0f};
  float fog_color[4] = {};
  std::vector<DrawItem> items;
  int debug = 0;
};

// One frame submission record from the hook layer.
struct SubmitRecord {
  uint32_t kind;
  uint32_t a;
  uint32_t b;
  uint32_t c;
};

// Called from the RenderMesh-equivalent hook AFTER the original call.
// Returns a dynamic-state index (+1), 0 on failure/skip.
uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx, bool world_path = false,
                             bool drew_inside = false);

// Convenience overload.
uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx);

// Called at the swap boundary: builds the native frame scene from captured
// records, paces the guest frame rate.
void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<SubmitRecord>& records);

// Total guest draws completed (any draw function). Sampled around the
// original RenderMesh call to detect deferred meshes.
uint64_t DrawSequence();

// The current frame scene (built on the guest thread, consumed by the GPU
// thread). Callers must synchronize with g_build_mutex externally or via
// the scene's own synchronization.
FrameScene& CurrentScene();

}  // namespace bk::native
