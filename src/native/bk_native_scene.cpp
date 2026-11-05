// beautiful_katamari - native renderer scene builder (game-thread half).

#include "bk_native_scene.h"
#include "bk_native_render.h"
#include "bk_native_guest_read.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
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
constexpr uint32_t kMeshCtxVbBytesOffset = 0x30;
constexpr uint32_t kMeshCtxIbAddrOffset = 0x2C;
constexpr uint32_t kMeshCtxIbCountOffset = 0x34;

bool ReadCtxU32(uint8_t* guest_base, uint32_t ctx, uint32_t offset,
                uint32_t* out) {
  return GuestTryLoadU32(guest_base, ctx + offset, out);
}

bool ResolveSceneDrawItem(uint8_t* guest_base, DrawItem& item) {
  uint32_t vb_addr = 0, vb_bytes = 0, ib_addr = 0, ib_count = 0;
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxVbAddrOffset, &vb_addr)) {
    return false;
  }
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxVbBytesOffset, &vb_bytes)) {
    return false;
  }
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxIbAddrOffset, &ib_addr)) {
    return false;
  }
  if (!ReadCtxU32(guest_base, item.ctx, kMeshCtxIbCountOffset, &ib_count)) {
    return false;
  }

  item.vb_addr = vb_addr;
  item.ib_addr = ib_addr;
  item.vb_bytes = vb_bytes;
  item.ib_count = ib_count;
  item.vb_data = guest_base + vb_addr;
  item.ib_data = guest_base + ib_addr;

  static int g_debug_count = 0;
  if (g_debug_count < 20 && item.vb_data && vb_bytes > 0) {
    REXLOG_INFO("bk native-render: VB[ctx=0x%08X] addr=0x%08X bytes=%u ib_count=%u",
                item.ctx, vb_addr, vb_bytes, ib_count);
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

  if (!ResolveSceneDrawItem(base, item)) {
    item.pending = true;
  }

  g_scene.items.push_back(std::move(item));
  return 1;
}

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx) {
  return CaptureDynamicState(base, ctx, true, false);
}

void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<DrawItem>& items) {
  (void)frame_index;

  std::lock_guard<std::mutex> lock(g_build_mutex);

  FrameScene scene;
  scene.generation = ++g_generation;

  for (int i = 0; i < 16; i++)
    scene.view_proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
  scene.cam_pos[0] = scene.cam_pos[1] = scene.cam_pos[2] = 0.0f;

  for (const auto& item : items) {
    if (!item.pending) {
      ResolveSceneDrawItem(base, const_cast<DrawItem&>(item));
    }
  }

  scene.items = items;
  g_scene.items.clear();

  g_scene = std::move(scene);

  REXLOG_INFO("bk native-render: OnFrameEnd items=%zu", scene.items.size());
}

uint64_t DrawSequence() { return g_draw_sequence; }

FrameScene& CurrentScene() { return g_scene; }
}  // namespace bk::native
