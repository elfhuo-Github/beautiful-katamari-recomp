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

// BK mesh context offsets (TBD - discovered from ppc_recomp.116.cpp):
//   kMeshCtxVbObjOffset   - offset of VB object pointer in mesh ctx
//   kMeshCtxIbObjOffset   - offset of IB object pointer in mesh ctx
//   kMeshCtxVbAddrOffset  - offset of raw VB data address in mesh ctx
//   kMeshCtxIbAddrOffset  - offset of raw IB data address in mesh ctx
//   kMeshCtxVbBytesOffset - offset of VB byte size in mesh ctx
//   kMeshCtxIbCountOffset - offset of IB index count in mesh ctx
//
// These offsets were observed in ppc_recomp.116.cpp:
//   sub_824DF5F8 reads from ctx+13492, ctx+13492+152
//   sub_824DF8C8 reads from ctx+11816
//   sub_824E5568 reads from ctx+48, ctx+56 (ring buffer indices)
//
// Placeholder offsets; verify against actual BK guest structures.
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

  GuestReadRecoveryScope guard(base);

  std::lock_guard<std::mutex> lock(g_build_mutex);

  DrawItem item;
  item.ctx = ctx;

  // Read VB/IB objects and data addresses from the guest mesh context.
  uint32_t vb_obj = 0, ib_obj = 0, vb_addr = 0, ib_addr = 0;
  uint32_t vb_bytes = 0, ib_count = 0;

  if (GuestTryLoadU32(base, ctx + kMeshCtxVbObjOffset, &vb_obj)) {
    item.vb_obj = vb_obj;
  }
  if (GuestTryLoadU32(base, ctx + kMeshCtxIbObjOffset, &ib_obj)) {
    item.ib_obj = ib_obj;
  }
  if (GuestTryLoadU32(base, ctx + kMeshCtxVbAddrOffset, &vb_addr)) {
    item.vb_addr = vb_addr;
  }
  if (GuestTryLoadU32(base, ctx + kMeshCtxIbAddrOffset, &ib_addr)) {
    item.ib_addr = ib_addr;
  }
  if (GuestTryLoadU32(base, ctx + kMeshCtxVbBytesOffset, &vb_bytes)) {
    item.vb_bytes = vb_bytes;
  }
  if (GuestTryLoadU32(base, ctx + kMeshCtxIbCountOffset, &ib_count)) {
    item.ib_count = ib_count;
  }

  // Log captured geometry for debugging
  static bool logged_first = false;
  if (!logged_first && (item.vb_obj || item.ib_obj || item.vb_addr ||
                        item.ib_addr)) {
    REXLOG_INFO("bk native-render: captured mesh ctx=0x%08X vb_obj=0x%08X "
                "ib_obj=0x%08X vb_addr=0x%08X ib_addr=0x%08X vb_bytes=%u "
                "ib_count=%u",
                ctx, item.vb_obj, item.ib_obj, item.vb_addr, item.ib_addr,
                item.vb_bytes, item.ib_count);
    logged_first = true;
  }

  // Capture bone palette / world matrix from the guest constant bank.
  // Bank address and register TBD - search for camera setup function.
  // For now leave identity.

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
  scene.debug = rex::cvar::Query<int32_t>("bk_native_render_scene_debug");

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
