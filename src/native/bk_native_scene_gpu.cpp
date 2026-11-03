// beautiful_katamari - native renderer GPU half (implementation).

// Renders NativeFrameScene natively via Vulkan. This is a skeleton: the
// concrete pipeline layouts, shader bindings, vertex formats, and texture
// formats are BK-specific and TBD. The architecture follows skate3recomp's
// skate3_native_scene_gpu.cpp but is adapted to BK's guest render pipeline
// and the rexglue SDK's presenter interface.

#include "bk_native_scene_gpu.h"
#include "bk_native_scene.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

// Cvar defined in bk_native_render.cpp.
REXCVAR_DECLARE(bool, bk_native_render_scene);

namespace bk::native_gpu {
namespace {

std::mutex g_mutex;
bool g_initialized = false;
bool g_failed = false;

// Vulkan handles TBD. In the skate3recomp approach, these wrap the SDK's
// existing Vulkan context or create a dedicated device. Since the rexglue
// SDK v0.10.0 prebuilt libraries don't expose a native-guest-renderer API,
// this skeleton uses the presenter's RefreshGuestOutput callback to present
// our rendered image.

struct NativeRenderer {
  bool valid = false;
};
NativeRenderer g_renderer;

}  // namespace

void Install() {
  if (!REXCVAR_GET(bk_native_render_scene)) {
    return;
  }
  if (g_initialized) return;
  REXLOG_INFO("bk native GPU renderer: install (skeleton - no Vulkan yet)");
  g_initialized = true;
  g_renderer.valid = true;
}

bool RenderFrame(uint8_t* guest_base) {
  if (!g_initialized || !g_renderer.valid || g_failed) {
    return false;
  }

  // Lock and read the current scene.
  std::lock_guard<std::mutex> lock(g_mutex);
  // const FrameScene& scene = bk::native::CurrentScene();

  // TODO: Implement Vulkan rendering:
  //   1. Acquire presenter guest output image via RefreshGuestOutput callback
  //   2. Upload vertex/index buffers from guest memory (guest_base + addr)
  //   3. Upload bone palettes / world matrices
  //   4. Bind pipelines and draw each DrawItem
  //   5. Present

  (void)guest_base;
  return true;  // placeholder: no-op render, fall through to emulated
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_initialized = false;
  g_renderer.valid = false;
  g_failed = false;
}

}  // namespace bk::native_gpu
