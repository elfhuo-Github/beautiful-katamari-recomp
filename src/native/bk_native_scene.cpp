// beautiful_katamari - native renderer scene builder (game-thread half).

#include "bk_native_scene.h"
#include "bk_native_render.h"
#include "bk_native_guest_read.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native {
namespace {

std::mutex g_build_mutex;
FrameScene g_scene;
uint64_t g_generation = 0;
uint64_t g_draw_sequence = 0;

// Mesh context structure offsets in guest memory.
// These are Beautiful Katamari-specific and must match the guest's actual
// MeshContext layout. Tunable via cvars for reverse-engineering.
//
// Offset values were determined by dumping the guest mesh context at
// ctx=0x40006D00:
//   +0x10: vertex buffer byte count (4992)
//   +0x30: vertex buffer address
//   +0x38: index buffer address
//   +0x3C: index count
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_vb_addr, 0x30, "Beautiful Katamari",
    "Mesh context offset for vertex buffer address.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_vb_bytes, 0x10, "Beautiful Katamari",
    "Mesh context offset for vertex buffer byte count.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_ib_addr, 0x38, "Beautiful Katamari",
    "Mesh context offset for index buffer address.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_ib_count, 0x3C, "Beautiful Katamari",
    "Mesh context offset for index count.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

inline uint32_t MeshCtxVbAddr() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_vb_addr"); }
inline uint32_t MeshCtxVbBytes() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_vb_bytes"); }
inline uint32_t MeshCtxIbAddr() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_ib_addr"); }
inline uint32_t MeshCtxIbCount() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_ib_count"); }

bool ReadCtxU32(uint8_t* guest_base, uint32_t ctx, uint32_t offset,
                uint32_t* out) {
  return GuestTryLoadU32(guest_base, ctx + offset, out);
}

bool PlausibleVB(uint32_t addr, uint32_t bytes) {
  return addr != 0 && bytes > 0 && bytes < (16u << 20);
}

bool PlausibleIB(uint32_t addr, uint32_t count) {
  return addr != 0 && count > 0 && count < (4u << 20);
}

bool ResolveSceneDrawItem(uint8_t* guest_base, DrawItem& item) {
  // Already resolved
  if (item.vb_data && item.vb_bytes > 0) return true;

  uint32_t vb_addr = 0, vb_bytes = 0, ib_addr = 0, ib_count = 0;
  bool vb_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxVbAddr(), &vb_addr);
  bool vb_bytes_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxVbBytes(), &vb_bytes);
  bool ib_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxIbAddr(), &ib_addr);
  bool ib_count_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxIbCount(), &ib_count);

  if (!vb_ok || !vb_bytes_ok || !ib_ok || !ib_count_ok) {
    return false; // guest memory fault, try again later
  }

  if (!PlausibleVB(vb_addr, vb_bytes)) {
    return false; // not a valid vertex buffer yet
  }

  item.vb_addr = vb_addr;
  item.ib_addr = ib_addr;
  item.vb_bytes = vb_bytes;
  item.ib_count = ib_count;
  item.vb_data = guest_base + vb_addr;
  item.ib_data = guest_base + ib_addr;

  static int g_debug_count = 0;
  if (g_debug_count < 5) {
    REXLOG_INFO("bk native-render: VB RESOLVED ctx=0x%08X vb_addr=0x%08X vb_bytes=%u ib_addr=0x%08X ib_count=%u",
                item.ctx, vb_addr, vb_bytes, ib_addr, ib_count);
    g_debug_count++;
  }
  return true;
}

}  // namespace

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx, bool world_path,
                             bool drew_inside) {
  if (!ctx) return 0;

  static int capture_count = 0;
  REXLOG_INFO("bk native-render: CaptureDynamicState ctx=0x%08X (#%d)",
              ctx, ++capture_count);

  DrawItem item;
  item.ctx = ctx;
  item.pending = true;

  g_scene.items.push_back(std::move(item));
  return 1;
}

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx) {
  return CaptureDynamicState(base, ctx, true, false);
}

void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<DrawItem>& items) {
  static int g_frame = 0;
  static auto g_last_fps_time = std::chrono::steady_clock::now();
  static int g_frames_since_fps = 0;

  g_frame++;
  g_frames_since_fps++;

  // FPS report every second
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_fps_time);
  if (elapsed >= std::chrono::milliseconds(1000)) {
    double fps = g_frames_since_fps * 1000.0 / elapsed.count();
    fprintf(stderr, "=== bk native-render: FPS=%.1f frame=%d items=%zu resolved=%d/%zu ===\n",
            fps, g_frame, items.size(), 0, items.size());
    g_last_fps_time = now;
    g_frames_since_fps = 0;
  }

  std::lock_guard<std::mutex> lock(g_build_mutex);

  FrameScene scene;
  scene.generation = ++g_generation;

  // Identity view/proj for now - camera extraction is TODO
  for (int i = 0; i < 16; i++)
    scene.view_proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
  scene.cam_pos[0] = scene.cam_pos[1] = scene.cam_pos[2] = 0.0f;

  // Resolve each item (read guest memory for VB/IB pointers).
  // Items that fail resolution are dropped for this frame.
  int resolved = 0;
  for (const auto& src : items) {
    DrawItem item = src; // mutable copy
    if (ResolveSceneDrawItem(base, item)) {
      scene.items.push_back(std::move(item));
      resolved++;
    }
  }

  g_scene = std::move(scene);
}

uint64_t DrawSequence() { return g_draw_sequence; }

FrameScene& CurrentScene() { return g_scene; }
}  // namespace bk::native
