// beautiful_katamari - native renderer hook layer (implementation).
//
// Hooks Beautiful Katamari's guest render entry points to capture per-draw
// state for debugging/analysis. Does NOT replace the emulated GPU output -
// the xenos GPU plugin renders normally. The hooks capture data for
// analysis/debugging without interfering with the emulated output.

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
    bk_native_render_scene, false, "Beautiful Katamari",
    "Render the game scene natively from the hooked draw stream, replacing "
    "the emulated xenos GPU output. Requires bk_native_render. Hot-toggles "
    "live between the native and emulated renderers. Defaults to false "
    "because the native renderer is incomplete and would cause black screens.")
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

//------------------------------------------------------------------------------
// Guest render function hooks (BK-specific addresses)
//=============================================================================
//
// DISABLED (0).  These definitions have external linkage and the exact names of
// the guest import thunks, so they replace the generated ones at link time no
// matter what the cvars say: the cvar only gates the hook body, not the
// interposition itself.  The hooks then try to reach the original through
// dlsym(RTLD_NEXT), which cannot see a symbol defined inside the executable, so
// the original guest functions never run - the game boots to a black window and
// loops on "Unhandled guest access violation: write of guest 0x00000284".
//
// They are kept for reference until the renderer moves into the SDK (the code
// that can actually decode draws); nothing in the shipped build uses them.
#define BK_NATIVE_GUEST_HOOKS 0
#if BK_NATIVE_GUEST_HOOKS
//
// Hooked functions:
//   __imp__VdSwap         - Present/swap, frame boundary (line 28182)
//   __imp__VdInitializeRingBuffer  - Ring buffer setup (line 6840)
//   __imp__VdEnableRingBufferRPtrWriteBack - Rptr writeback (line 6871)
//   __imp__VdSetGraphicsInterruptCallback - Interrupt callback (line 9330)
//   sub_824E04B0                   - Pre-swap ring buffer processing
//   sub_824E5568                   - Writes command packets to ring buffer
//   sub_824DF5F8                   - Draw submission
//
// Real addresses from ppc_recomp.116.cpp.
//
// These hooks call the original guest functions so the emulated xenos GPU
// output renders normally. The hooks capture data for debugging/analysis
// without interfering with the emulated output.

//------------------------------------------------------------------------------
// __imp__VdSwap - Present/swap, frame boundary.
//
// Called once per guest frame. Calls the original VdSwap so the emulated
// xenos GPU output renders normally. Does NOT call OnFrameEnd/RenderFrame
// because the native renderer is incomplete and would replace the emulated
// output with debug quads, causing black screens.

extern "C" void __imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  static bool resolved = false;
  static VdSwapFn g_real_vd_swap = nullptr;
  if (!resolved) {
    resolved = true;
    g_real_vd_swap = reinterpret_cast<VdSwapFn>(
        dlsym(RTLD_NEXT, "__imp__VdSwap"));
  }

  const bool enabled = rex::cvar::GetFlagByName("bk_native_render") == "true";

  // Pace guest frame rate if enabled
  if (enabled) {
    PaceGuestFrame();
  }

  // Call original VdSwap so emulated GPU output renders normally
  if (g_real_vd_swap) {
    g_real_vd_swap(ctx, base);
  } else {
    REXLOG_WARN("bk native-render: __imp__VdSwap original not resolved");
  }

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// __imp__VdInitializeRingBuffer - GPU ring buffer initialization

extern "C" void __imp__VdInitializeRingBuffer(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true") {
    REXLOG_INFO("bk native-render: VdInitializeRingBuffer called");
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__VdInitializeRingBuffer"));
  }
  if (real) real(ctx, base);

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// __imp__VdEnableRingBufferRPtrWriteBack - Ring buffer read pointer writeback

extern "C" void __imp__VdEnableRingBufferRPtrWriteBack(PPCContext& ctx,
                                                        uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true") {
    REXLOG_INFO("bk native-render: VdEnableRingBufferRPtrWriteBack called");
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__VdEnableRingBufferRPtrWriteBack"));
  }
  if (real) real(ctx, base);

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// sub_824E04B0 - Pre-swap ring buffer processing

extern "C" void sub_824E04B0(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({0, ctx.r3.u32, ctx.r4.u32, 0});
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__sub_824E04B0"));
  }
  if (real) real(ctx, base);

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// sub_824E5568 - Main mesh submission (draw call)

extern "C" void sub_824E5568(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({1, ctx.r3.u32, ctx.r4.u32, 0});
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__sub_824E5568"));
  }
  if (real) real(ctx, base);

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// sub_824DF5F8 - Draw submission (lower level)

extern "C" void sub_824DF5F8(PPCContext& ctx, uint8_t* base) {
  static thread_local bool g_in_hook = false;
  if (g_in_hook) return;
  g_in_hook = true;

  if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
      rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({2, ctx.r3.u32, ctx.r4.u32, 0});
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__sub_824DF5F8"));
  }
  if (real) real(ctx, base);

  g_in_hook = false;
}

#endif  // BK_NATIVE_GUEST_HOOKS

//=============================================================================
// Hook registration (called once at startup)
//============================================================================

void Install() {
  if (rex::cvar::GetFlagByName("bk_native_render") != "true") {
    return;
  }
  if (!g_announced.exchange(true)) {
    REXLOG_INFO("bk native-render: capture layer enabled (guest hooks disabled)");
  }
  if (rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
    bk::native_gpu::Install();
  }
}

}  // namespace bk::native_render
