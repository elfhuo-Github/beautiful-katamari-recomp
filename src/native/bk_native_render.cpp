// beautiful_katamari - native renderer hook layer (implementation).
//
// Hooks Beautiful Katamari's guest render entry points to capture per-draw
// state and replace the emulated Xenos GPU output with native Vulkan rendering.
//
// The hooks use linker --wrap to interpose on guest __imp__ functions and
// call the originals via __real_* symbols. This is necessary because
// dlsym(RTLD_NEXT) cannot resolve symbols defined in the same executable.

#include "bk_native_render.h"
#include "bk_native_scene.h"
#include "bk_native_scene_gpu.h"
#include "bk_native_guest_read.h"
#include "bk_native_palette.h"
#include "generated/default/beautiful_katamari_init.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <rex/cvar.h>
#include <rex/logging.h>

// Declare __real_* symbols provided by the linker --wrap mechanism.
// These point to the original guest implementations of the wrapped functions.
extern "C" void __real___imp__VdSwap(PPCContext&, uint8_t*);
extern "C" void __real___imp__VdInitializeRingBuffer(PPCContext&, uint8_t*);
extern "C" void __real___imp__VdEnableRingBufferRPtrWriteBack(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824E04B0(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824E5568(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824DF5F8(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824DF8C8(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824DFB70(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824E0110(PPCContext&, uint8_t*);
extern "C" void __real___imp__sub_824DFC48(PPCContext&, uint8_t*);

namespace bk::native_render {
REXCVAR_DEFINE_BOOL(bk_native_render, true, "Beautiful Katamari", "Enable the Beautiful Katamari native renderer hook layer.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(bk_native_render_scene, true, "Beautiful Katamari", "Render the game scene natively from the hooked draw stream with Vulkan.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(bk_native_render_scene_debug, 0, "Beautiful Katamari", "Native scene debug: 0=normal, 1=clear only, 2=solid color per item, 3=limit to 32 items, 4=disable depth test.")
    .range(0, 4).lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(bk_guest_fps_cap, 60.0, "Beautiful Katamari", "Pace the guest render loop to this frame rate (0 = uncapped).")
    .range(0.0, 1000.0).lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {
std::mutex g_mutex;
std::vector<bk::native::DrawItem> g_current_frame;
uint64_t g_frame_index = 0;
std::atomic<bool> g_announced{false};
static thread_local bool g_in_hook = false;
bool Enabled() { return rex::cvar::GetFlagByName("bk_native_render") == "true"; }
}  // namespace

void PaceGuestFrame() {
  double cap = rex::cvar::Query<double>("bk_guest_fps_cap");
  if (cap < 1.0) return;
  static std::chrono::steady_clock::time_point s_next{};
  const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / cap));
  const auto now = std::chrono::steady_clock::now();
  if (s_next.time_since_epoch().count() == 0 || now > s_next + interval) { s_next = now + interval; return; }
  while (true) {
    const auto remaining = s_next - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) break;
    if (remaining > std::chrono::milliseconds(2)) std::this_thread::sleep_for(remaining - std::chrono::milliseconds(2));
    else if (remaining > std::chrono::milliseconds(50)) std::this_thread::yield();
  }
  s_next += interval;
}

bool IsNativeRenderEnabled() { return rex::cvar::GetFlagByName("bk_native_render") == "true"; }
bool IsNativeRenderSceneEnabled() { return rex::cvar::GetFlagByName("bk_native_render_scene") == "true"; }

//------------------------------------------------------------------------------
// Wrapped guest render functions. The linker --wrap flag redirects all guest
// calls to __imp__XXX to __wrap___imp__XXX. We call __real___imp__XXX for the
// original guest implementation, then add our native rendering on top.

extern "C" void __wrap___imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  static int g_frame = 0;
  static auto g_last_fps_time = std::chrono::steady_clock::now();
  static int g_frames_since_fps = 0;

  g_frame++;
  g_frames_since_fps++;

  // Guest FPS report every second
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_fps_time);
  if (elapsed >= std::chrono::milliseconds(1000)) {
    double fps = g_frames_since_fps * 1000.0 / elapsed.count();
    fprintf(stderr, "=== bk native-render: guest FPS=%.1f frame=%d ===\n",
            fps, g_frame);
    g_last_fps_time = now;
    g_frames_since_fps = 0;
  }

  const bool enabled = rex::cvar::GetFlagByName("bk_native_render") == "true";
  if (enabled) PaceGuestFrame();

  // Call the original guest VdSwap so ring buffer / GPU state stays in sync.
  __real___imp__VdSwap(ctx, base);

  // Native rendering: submit captured draw items via Vulkan.
  if (rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    if (!g_current_frame.empty()) {
      bk::native::OnFrameEnd(base, g_frame_index++, g_current_frame);
      bk::native_gpu::RenderFrame(base);
    }
    g_current_frame.clear();
  }

  g_in_hook = false;
}

extern "C" void __wrap___imp__VdInitializeRingBuffer(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  if (rex::cvar::GetFlagByName("bk_native_render") == "true") {
    REXLOG_INFO("bk native-render: VdInitializeRingBuffer called");
  }
  __real___imp__VdInitializeRingBuffer(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__VdEnableRingBufferRPtrWriteBack(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  if (rex::cvar::GetFlagByName("bk_native_render") == "true") {
    REXLOG_INFO("bk native-render: VdEnableRingBufferRPtrWriteBack called");
  }
  __real___imp__VdEnableRingBufferRPtrWriteBack(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__sub_824E04B0(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 0, ctx.r4.u32, 0});
  }

  __real___imp__sub_824E04B0(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__sub_824E5568(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 1, ctx.r4.u32, 0});
  }

  __real___imp__sub_824E5568(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__sub_824DF5F8(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 2, ctx.r4.u32, 0});
  }

  __real___imp__sub_824DF5F8(ctx, base);
  g_in_hook = false;
}

// Additional draw function hooks - Katamari uses DrawVerticesUP for most draws
// (83-84% per katamari-nx analysis). These might be additional draw entry points.
extern "C" void __wrap___imp__sub_824DF8C8(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 3, ctx.r4.u32, 0});
  }

  __real___imp__sub_824DF8C8(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__sub_824DFB70(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 4, ctx.r4.u32, 0});
  }

  __real___imp__sub_824DFB70(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__sub_824E0110(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 5, ctx.r4.u32, 0});
  }

  __real___imp__sub_824E0110(ctx, base);
  g_in_hook = false;
}

extern "C" void __wrap___imp__sub_824DFC48(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({ctx.r3.u32, 6, ctx.r4.u32, 0});
  }

  __real___imp__sub_824DFC48(ctx, base);
  g_in_hook = false;
}

// Wrappers for bare sub_* symbol names (weak aliases of __imp__sub_*).
// --wrap works per-symbol-name; guest code may reference either name.
extern "C" void __wrap_sub_824E04B0(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824E04B0(ctx, base);
}
extern "C" void __wrap_sub_824E5568(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824E5568(ctx, base);
}
extern "C" void __wrap_sub_824DF5F8(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824DF5F8(ctx, base);
}
extern "C" void __wrap_sub_824DF8C8(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824DF8C8(ctx, base);
}
extern "C" void __wrap_sub_824DFB70(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824DFB70(ctx, base);
}
extern "C" void __wrap_sub_824E0110(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824E0110(ctx, base);
}
extern "C" void __wrap_sub_824DFC48(PPCContext& ctx, uint8_t* base) {
  __wrap___imp__sub_824DFC48(ctx, base);
}

//=============================================================================
// Hook registration (called once at startup)
//============================================================================

void Install() {
  bool native_render = rex::cvar::GetFlagByName("bk_native_render") == "true";
  bool native_scene = rex::cvar::GetFlagByName("bk_native_render_scene") == "true";
  REXLOG_INFO("bk native-render: Install() called (native_render=%d, native_scene=%d)", native_render, native_scene);
  if (!native_render) return;
  if (!g_announced.exchange(true)) {
    REXLOG_INFO("bk native-render hook layer enabled");
    REXLOG_INFO("bk native-render: hooked __imp__VdSwap (frame boundary + native render)");
    REXLOG_INFO("bk native-render: hooked sub_824E04B0 (pre-swap capture)");
    REXLOG_INFO("bk native-render: hooked sub_824E5568 (mesh submission)");
    REXLOG_INFO("bk native-render: hooked sub_824DF5F8 (draw submission)");
    REXLOG_INFO("bk native-render: hooked sub_824DF8C8 (additional draw)");
    REXLOG_INFO("bk native-render: hooked sub_824DFB70 (additional draw)");
    REXLOG_INFO("bk native-render: hooked sub_824E0110 (additional draw)");
    REXLOG_INFO("bk native-render: hooked sub_824DFC48 (additional draw)");
  }
  if (native_scene) bk::native_gpu::Install();
}

}  // namespace bk::native_render
