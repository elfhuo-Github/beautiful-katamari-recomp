// beautiful_katamari - native renderer hook layer (implementation).

#include "bk_native_render.h"
#include "bk_native_scene.h"
#include "bk_native_scene_gpu.h"

#include "bk_native_guest_read.h"
#include "bk_native_palette.h"

#include "generated/default/beautiful_katamari_init.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_render {
REXCVAR_DEFINE_BOOL(
    bk_native_render, true, "Beautiful Katamari",
    "Enable the Beautiful Katamari native renderer hook layer. The guest "
    "render hooks are installed at boot but capture/no-op until "
    "bk_native_render_scene is also enabled.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(
    bk_native_render_scene, true, "Beautiful Katamari",
    "Render the game scene natively from the hooked draw stream, replacing "
    "the emulated xenos GPU output. Requires bk_native_render. Hot-toggles "
    "live between the native and emulated renderers.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(
    bk_native_render_scene_debug, 0, "Beautiful Katamari",
    "Native scene debug: 0=normal, 1=clear only, 2=solid color per item, "
    "3=limit to 32 items, 4=disable depth test.")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(
    bk_guest_fps_cap, 0.0, "Beautiful Katamari",
    "Pace the guest render loop to this frame rate (0 = uncapped). BK "
    "produces frames at irregular intervals; capping a few fps below the "
    "display refresh smooths VRR judder.")
    .range(0.0, 1000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

std::mutex g_mutex;
std::vector<bk::native::SubmitRecord> g_current_frame;
uint64_t g_frame_index = 0;
std::atomic<bool> g_announced{false};

using VdSwapFn = void (*)(PPCContext&, uint8_t*);
static VdSwapFn g_real_vd_swap = nullptr;
static bool g_vd_swap_resolved = false;

using RingBufferFn = void (*)(PPCContext&, uint8_t*);
static RingBufferFn g_real_vd_init_ring = nullptr;

using SubFunc = void (*)(PPCContext&, uint8_t*);
static SubFunc g_real_sub_824e04b0 = nullptr;
static SubFunc g_real_sub_824e5568 = nullptr;
static SubFunc g_real_sub_824df5f8 = nullptr;

static thread_local bool g_in_hook = false;

bool Enabled() { return rex::cvar::GetFlagByName("bk_native_render") == "true"; }

}  // namespace

void PaceGuestFrame() {
  double cap = rex::cvar::Query<double>("bk_guest_fps_cap");
  if (cap < 1.0) return;
  static std::chrono::steady_clock::time_point s_next{};
  const auto interval = std::chrono::duration_cast<
      std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / cap));
  const auto now = std::chrono::steady_clock::now();
  if (s_next.time_since_epoch().count() == 0 ||
      now > s_next + interval) {
    s_next = now + interval;
    return;
  }
  while (true) {
    const auto remaining = s_next - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) break;
    if (remaining > std::chrono::milliseconds(2)) {
      std::this_thread::sleep_for(remaining - std::chrono::milliseconds(2));
    } else if (remaining > std::chrono::microseconds(50)) {
      std::this_thread::yield();
    }
  }
  s_next += interval;
}

bool IsNativeRenderEnabled() { return rex::cvar::GetFlagByName("bk_native_render") == "true"; }

bool IsNativeRenderSceneEnabled() { return rex::cvar::GetFlagByName("bk_native_render_scene") == "true"; }

}  // namespace bk::native_render

// Install() at global scope - called from ReXApp::OnPreSetup()
extern "C" void Install() {
  if (rex::cvar::GetFlagByName("bk_native_render") != "true") {
    return;
  }
  REXLOG_INFO("bk native-render hook layer enabled");
  REXLOG_INFO("bk native-render: hooked __imp__VdSwap (frame boundary)");
  REXLOG_INFO("bk native-render: hooked sub_824E04B0 (pre-swap capture)");
  REXLOG_INFO("bk native-render: hooked sub_824E5568 (mesh submission)");
  REXLOG_INFO("bk native-render: hooked sub_824DF5F8 (draw submission)");
  if (rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    bk::native_gpu::Install();
  }
}
