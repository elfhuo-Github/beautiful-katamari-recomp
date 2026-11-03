// beautiful_katamari - native renderer GPU half (implementation).

// Renders the NativeFrameScene (built by bk_native_scene.cpp on the guest
// thread) natively with Vulkan, replacing the emulated xenos GPU output when
// bk_native_render_scene is active.

#include "bk_native_scene_gpu.h"
#include "bk_native_scene.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_gpu {
namespace {

std::mutex g_mutex;
bool g_initialized = false;
bool g_failed = false;

}  // namespace

void Install() {
  if (g_initialized) return;

  REXLOG_INFO("bk native GPU renderer: install");
  g_initialized = true;
}

bool RenderFrame(uint8_t* guest_base) {
  if (!g_initialized || g_failed) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_mutex);

  // No-op: native renderer does not yet replace emulated output.
  // Guest rendering is handled by the emulated xenos GPU path.
  (void)guest_base;
  return true;
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_initialized = false;
  g_failed = false;
}

}  // namespace bk::native_gpu
