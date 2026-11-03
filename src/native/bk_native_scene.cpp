// beautiful_katamari - native renderer scene builder (game-thread half).

#include "bk_native_scene.h"
#include "bk_native_render.h"

#include "bk_native_guest_read.h"
#include "bk_native_palette.h"

#include "generated/default/beautiful_katamari_init.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

// Cvars defined in bk_native_render.cpp.
REXCVAR_DECLARE(bool, bk_native_render_scene_debug);

namespace bk::native {
namespace {

std::mutex g_build_mutex;
FrameScene g_scene;
uint64_t g_generation = 0;
uint64_t g_draw_sequence = 0;

constexpr uint32_t kMeshContextSize = 0x50;
constexpr uint32_t kConstantBankBase = 0x83000000u;

}  // namespace

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx, bool world_path,
                             bool drew_inside) {
  if (!ctx) return 0;

  GuestReadRecoveryScope guard(base);

  std::lock_guard<std::mutex> lock(g_build_mutex);

  DrawItem item;
  item.ctx = ctx;

  // TODO: Read VB/IB objects and data addresses from the guest mesh context.
  // Offsets TBD from BK reverse engineering.

  // TODO: Capture bone palette / world matrix from the guest constant bank.
  // Bank address and register TBD.

  g_scene.items.push_back(std::move(item));
  return 1;
}

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx) {
  return CaptureDynamicState(base, ctx, /*world_path=*/true,
                             /*drew_inside=*/false);
}

void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<SubmitRecord>& records) {
  // PaceGuestFrame() is already called in the VdSwap hook before this;
  // do not pace again here.

  GuestReadRecoveryScope guard(base);

  std::lock_guard<std::mutex> lock(g_build_mutex);

  FrameScene scene;
  scene.generation = ++g_generation;
  scene.debug = REXCVAR_GET(bk_native_render_scene_debug);

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
