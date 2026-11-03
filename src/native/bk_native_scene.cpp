// beautiful_katamari - native renderer scene builder (game-thread half).

#include "bk_native_scene.h"
#include "bk_native_render.h"

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

}  // namespace

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx, bool world_path,
                             bool drew_inside) {
  if (!ctx) return 0;

  // NOTE: GuestReadRecoveryScope and GuestTryLoadU32 are not available
  // (bk_native_guest_read.cpp was removed). Store ctx without reading guest
  // memory for now. Actual geometry capture is TBD.
  (void)base;
  (void)world_path;
  (void)drew_inside;

  DrawItem item;
  item.ctx = ctx;

  g_scene.items.push_back(std::move(item));
  return 1;
}

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx) {
  return CaptureDynamicState(base, ctx, true, false);
}

void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<SubmitRecord>& records) {
  (void)base;
  (void)frame_index;
  (void)records;

  std::lock_guard<std::mutex> lock(g_build_mutex);

  FrameScene scene;
  scene.generation = ++g_generation;

  for (int i = 0; i < 16; i++)
    scene.view_proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
  scene.cam_pos[0] = scene.cam_pos[1] = scene.cam_pos[2] = 0.0f;

  scene.items = std::move(g_scene.items);
  g_scene.items.clear();

  g_scene = std::move(scene);
}

uint64_t DrawSequence() { return g_draw_sequence; }

FrameScene& CurrentScene() { return g_scene; }

}  // namespace bk::native
