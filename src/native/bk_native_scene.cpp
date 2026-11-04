// beautiful_katamari - native renderer scene builder (game-thread half).

#include "bk_native_scene.h"
#include "bk_native_render.h"
#include "bk_native_guest_read.h"

#include <algorithm>
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

constexpr uint32_t kMeshCtxVbObjOffset = 0x20;
constexpr uint32_t kMeshCtxIbObjOffset = 0x24;
constexpr uint32_t kMeshCtxVbAddrOffset = 0x28;
constexpr uint32_t kMeshCtxIbAddrOffset = 0x2C;
constexpr uint32_t kMeshCtxVbBytesOffset = 0x30;
constexpr uint32_t kMeshCtxIbCountOffset = 0x34;

// Read a big-endian u32 from guest memory at the given offset from ctx.
bool ReadCtxU32(uint8_t* guest_base, uint32_t ctx, uint32_t offset,
                uint32_t* out) {
  return GuestTryLoadU32(guest_base, ctx + offset, out);
}

// Read vertex/index data pointers from guest mesh context.
// Resolves vb_data / ib_data host pointers from guest addresses.
bool ResolveGeometry(uint8_t* guest_base, DrawItem& item) {
  // Read VB address and size from guest context
  uint32_t vb_addr = 0, vb_bytes = 0, ib_addr = 0, ib_count = 0;
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxVbAddrOffset, &vb_addr)) {
    REXLOG_WARN("bk native-render: failed to read VB addr from ctx 0x%08X",
                item.ctx);
    return false;
  }
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxVbBytesOffset, &vb_bytes)) {
    REXLOG_WARN("bk native-render: failed to read VB bytes from ctx 0x%08X",
                item.ctx);
    return false;
  }
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxIbAddrOffset, &ib_addr)) {
    REXLOG_WARN("bk native-render: failed to read IB addr from ctx 0x%08X",
                item.ctx);
    return false;
  }
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxIbCountOffset, &ib_count)) {
    REXLOG_WARN("bk native-render: failed to read IB count from ctx 0x%08X",
                item.ctx);
    return false;
  }

  item.vb_addr = vb_addr;
  item.ib_addr = ib_addr;
  item.vb_bytes = vb_bytes;
  item.ib_count = ib_count;

  // Resolve host pointers (big-endian data, will be byte-swapped by renderer)
  item.vb_data = guest_base + vb_addr;
  item.ib_data = guest_base + ib_addr;

  return true;
}

}  // namespace

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx, bool world_path,
                             bool drew_inside) {
  if (!ctx) return 0;

  DrawItem item;
  item.ctx = ctx;

  // Resolve actual geometry pointers from guest memory
  if (!ResolveGeometry(base, item)) {
    // If we can't read guest memory yet, still record the ctx for later
    item.pending = true;
  }

  g_scene.items.push_back(std::move(item));
  return 1;
}

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx) {
  return CaptureDynamicState(base, ctx, true, false);
}

void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<SubmitRecord>& records) {
  (void)frame_index;
  (void)records;

  std::lock_guard<std::mutex> lock(g_build_mutex);

  FrameScene scene;
  scene.generation = ++g_generation;

  for (int i = 0; i < 16; i++)
    scene.view_proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
  scene.cam_pos[0] = scene.cam_pos[1] = scene.cam_pos[2] = 0.0f;

  // Resolve any pending items that couldn't be resolved earlier
  for (auto& item : g_scene.items) {
    if (item.pending) {
      ResolveGeometry(base, item);
      item.pending = false;
    }
  }

  scene.items = std::move(g_scene.items);
  g_scene.items.clear();

  g_scene = std::move(scene);
}

uint64_t DrawSequence() { return g_draw_sequence; }

FrameScene& CurrentScene() { return g_scene; }

}  // namespace bk::native
