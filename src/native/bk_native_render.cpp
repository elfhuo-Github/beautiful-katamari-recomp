// beautiful_katamari - native renderer hook layer (implementation).
//
// Hooks Beautiful Katamari's guest render entry points to capture per-draw
// state for debugging/analysis. Does NOT replace the emulated GPU output -
// the xenos GPU plugin renders normally. The hooks capture data for
// analysis/debugging without interfering with the emulated output.
//
// NOTE: These hooks are disabled by default (BK_NATIVE_GUEST_HOOKS = 0)
// because dlsym(RTLD_NEXT) cannot resolve the original guest functions when
// they are defined in the same executable. When enabled, the hooks would
// intercept __imp__VdSwap etc. but never call the originals, causing the
// guest to spin forever (black screen, 100% CPU).

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
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <vector>
#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_render {
REXCVAR_DEFINE_BOOL(bk_native_render, true, "Beautiful Katamari", "Enable the Beautiful Katamari native renderer hook layer.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(bk_native_render_scene, false, "Beautiful Katamari", "Render the game scene natively from the hooked draw stream.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(bk_native_render_scene_debug, 0, "Beautiful Katamari", "Native scene debug: 0=normal, 1=clear only, 2=solid color per item, 3=limit to 32 items, 4=disable depth test.")
    .range(0, 4).lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(bk_guest_fps_cap, 0.0, "Beautiful Katamari", "Pace the guest render loop to this frame rate (0 = uncapped).")
    .range(0.0, 1000.0).lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {
std::mutex g_mutex;
std::vector<bk::native::DrawItem> g_current_frame;
uint64_t g_frame_index = 0;
std::atomic<bool> g_announced{false};
using VdSwapFn = void (*)(PPCContext&, uint8_t*);
static VdSwapFn g_real_vd_swap = nullptr;
static bool g_vd_swap_resolved = false;
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

#define BK_NATIVE_GUEST_HOOKS 0

#if BK_NATIVE_GUEST_HOOKS
//------------------------------------------------------------------------------
// Guest render function hooks (BK-specific addresses)
//=============================================================================
//
// Hooked functions:
//   __imp__VdSwap         - Present/swap, frame boundary
//   __imp__VdInitializeRingBuffer  - Ring buffer setup
//   __imp__VdEnableRingBufferRPtrWriteBack - Rptr writeback
//   sub_824E04B0                   - Pre-swap ring buffer processing
//   sub_824E5568                   - Writes command packets to ring buffer
//   sub_824DF5F8                   - Draw submission
//
// These hooks call the original guest functions so the emulated xenos GPU
// output renders normally. The hooks capture data for debugging/analysis
// without interfering with the emulated output.

extern "C" void __imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  static int swap_call_count = 0;
  swap_call_count++;
  if (swap_call_count <= 10) fprintf(stderr, "=== bk native-render: __imp__VdSwap hook called #%d (frame=%llu) ===
", swap_call_count, (unsigned long long)g_frame_index);

  if (!g_real_vd_swap) {
    g_real_vd_swap = reinterpret_cast<VdSwapFn>(dlsym(RTLD_NEXT, "__imp__VdSwap"));
    if (!g_real_vd_swap) {
      REXLOG_ERROR("bk native-render: __imp__VdSwap original NOT resolved via dlsym!");
    }
  }

  const bool enabled = rex::cvar::GetFlagByName("bk_native_render") == "true";
  if (enabled) PaceGuestFrame();

  // Draw hooks capture data into g_current_frame for debugging/analysis.
  // The native renderer is incomplete - do NOT call OnFrameEnd/RenderFrame
  // here, as that would replace the emulated xenos GPU output and cause
  // a black screen. The emulated GPU path handles all rendering normally.
  g_current_frame.clear();

  if (g_real_vd_swap) g_real_vd_swap(ctx, base);
  else REXLOG_WARN("bk native-render: __imp__VdSwap original not resolved - guest may spin");
  g_in_hook = false;
}

extern "C" void __imp__VdInitializeRingBuffer(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  if (rex::cvar::GetFlagByName("bk_native_render") == "true") REXLOG_INFO("bk native-render: VdInitializeRingBuffer called");
  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr; static bool resolved = false;
  if (!resolved) { resolved = true; real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "__imp__VdInitializeRingBuffer")); }
  if (real) real(ctx, base);
  g_in_hook = false;
}

extern "C" void __imp__VdEnableRingBufferRPtrWriteBack(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  if (rex::cvar::GetFlagByName("bk_native_render") == "true") REXLOG_INFO("bk native-render: VdEnableRingBufferRPtrWriteBack called");
  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr; static bool resolved = false;
  if (!resolved) { resolved = true; real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "__imp__VdEnableRingBufferRPtrWriteBack")); }
  if (real) real(ctx, base);
  g_in_hook = false;
}

extern "C" void sub_824E04B0(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  static int e04b0_call_count = 0; e04b0_call_count++;
  if (e04b0_call_count <= 10) fprintf(stderr, "=== bk native-render: sub_824E04B0 hook called #%d (r3=0x%08X r4=0x%08X) ===
", e04b0_call_count, ctx.r3.u32, ctx.r4.u32);
  if (rex::cvar::GetFlagByName("bk_native_render") == "true" && rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({0, ctx.r3.u32, ctx.r4.u32, 0});
  }
  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr; static bool resolved = false;
  if (!resolved) { resolved = true; real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "__imp__sub_824E04B0")); }
  if (real) real(ctx, base);
  g_in_hook = false;
}

extern "C" void sub_824E5568(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  static int e5568_call_count = 0; e5568_call_count++;
  if (e5568_call_count <= 10) fprintf(stderr, "=== bk native-render: sub_824E5568 hook called #%d (r3=0x%08X r4=0x%08X) ===
", e5568_call_count, ctx.r3.u32, ctx.r4.u32);
  if (rex::cvar::GetFlagByName("bk_native_render") == "true" && rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({1, ctx.r3.u32, ctx.r4.u32, 0});
  }
  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr; static bool resolved = false;
  if (!resolved) { resolved = true; real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "__imp__sub_824E5568")); }
  if (real) real(ctx, base);
  g_in_hook = false;
}

extern "C" void sub_824DF5F8(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;
  static int df5f8_call_count = 0; df5f8_call_count++;
  if (df5f8_call_count <= 10) fprintf(stderr, "=== bk native-render: sub_824DF5F8 hook called #%d (r3=0x%08X r4=0x%08X) ===
", df5f8_call_count, ctx.r3.u32, ctx.r4.u32);
  if (rex::cvar::GetFlagByName("bk_native_render") == "true" && rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({2, ctx.r3.u32, ctx.r4.u32, 0});
  }
  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr; static bool resolved = false;
  if (!resolved) { resolved = true; real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "__imp__sub_824DF5F8")); }
  if (real) real(ctx, base);
  g_in_hook = false;
}

#endif  // BK_NATIVE_GUEST_HOOKS

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
#if BK_NATIVE_GUEST_HOOKS
    REXLOG_INFO("bk native-render: hooked __imp__VdSwap (frame boundary)");
    REXLOG_INFO("bk native-render: hooked sub_824E04B0 (pre-swap capture)");
    REXLOG_INFO("bk native-render: hooked sub_824E5568 (mesh submission)");
    REXLOG_INFO("bk native-render: hooked sub_824DF5F8 (draw submission)");
#else
    REXLOG_INFO("bk native-render: guest hooks disabled (BK_NATIVE_GUEST_HOOKS=0)");
#endif
  }
  if (native_scene) bk::native_gpu::Install();
}

}  // namespace bk::native_render
