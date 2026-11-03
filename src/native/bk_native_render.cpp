// beautiful_katamari - native renderer hook layer (implementation).
//
// Hooks Beautiful Katamari's guest render entry points, captures per-draw
// state from the guest constant bank / buffers, builds a native frame scene,
// and optionally replaces the emulated GPU output with our own Vulkan
// renderer.
//
// Architecture (inspired by skate3recomp's skate3_native_render.cpp, rewritten
// for BK's guest render pipeline):
//
//   1. Guest render functions are overridden via REX_FUNC (weak aliases of the
//      recompiled __imp__ entry points). Our override runs BEFORE and AFTER
//      the original, capturing state the guest flushed to its constant bank.
//   2. Captured records are accumulated into a per-frame ring buffer.
//   3. At frame end (swap boundary hook), BuildFrameScene() converts the
//      records into a NativeFrameScene (geometry + transforms + textures).
//   4. The GPU render thread renders NativeFrameScene natively and presents
//      it through the SDK presenter, skipping the emulated xenos draw path.
//   5. Guest memory reads are fault-guarded (see bk_native_guest_read.cpp)
//      because the guest can revoke pages between capture and dereference.
//
// BK-specific function addresses (discovered from BKRecomp/ppc/ppc_recomp.116.cpp):
//
//   __imp__VdSwap                  - Present/swap, frame boundary (line 28182)
//   __imp__VdInitializeRingBuffer  - Ring buffer setup (line 6840)
//   __imp__VdEnableRingBufferRPtrWriteBack - Rptr writeback (line 6871)
//   __imp__VdSetGraphicsInterruptCallback - Interrupt callback (line 9330)
//   sub_824E04B0                   - Pre-swap ring buffer processing
//   sub_824DFF98                   - Ring buffer management
//   sub_824E5568                   - Writes command packets to ring buffer
//   sub_824E1740                   - Display dimension setup
//
// These Vd* symbols are undefined in the executable and resolved from
// librexruntime.so at runtime. Our hooks override them; the original is
// reached via dlsym(RTLD_NEXT, ...).

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

namespace bk::native_render {

namespace {

std::mutex g_mutex;
std::vector<bk::native::SubmitRecord> g_current_frame;
uint64_t g_frame_index = 0;
std::atomic<bool> g_announced{false};

// Original function pointers resolved via dlsym(RTLD_NEXT, ...).
// If dlsym fails we fall back to calling the __imp__ symbol directly
// (which will recurse into our own hook - guarded by g_in_hook below).
using VdSwapFn = void (*)(PPCContext&, uint8_t*);
static VdSwapFn g_real_vd_swap = nullptr;
static bool g_vd_swap_resolved = false;

using RingBufferFn = void (*)(PPCContext&, uint8_t*);
static RingBufferFn g_real_vd_init_ring = nullptr;
static RingBufferFn g_real_vd_rptr_wb = nullptr;

using SubFunc = void (*)(PPCContext&, uint8_t*);
static SubFunc g_real_sub_824e04b0 = nullptr;

// Re-entrancy guard: if we're inside our own hook and the real function
// pointer couldn't be resolved, avoid infinite recursion.
static thread_local bool g_in_hook = false;

bool Enabled() { return REXCVAR_GET(bk_native_render); }

}  // namespace

// Precise guest frame pacing (see bk_guest_fps_cap): called on the guest
// render thread at the swap boundary. Absolute-schedule pacing (target +=
// interval) so sleep jitter never accumulates; resyncs when the guest falls
// more than one interval behind (loads, hitches).
void PaceGuestFrame() {
  double cap = REXCVAR_GET(bk_guest_fps_cap);
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

//=============================================================================
// Guest render function hooks (BK-specific addresses)
//=============================================================================
//
// Hooked functions:
//   __imp__VdSwap         - Present/swap (frame boundary)
//   __imp__VdInitializeRingBuffer - GPU ring buffer init
//   __imp__VdEnableRingBufferRPtrWriteBack - Rptr writeback
//   sub_824E04B0          - Pre-swap ring buffer processing
//
// Real addresses from ppc_recomp.116.cpp:
//   VdSwap is called inside sub_824E8E90 at line 28182
//   VdInitializeRingBuffer at line 6840
//   VdEnableRingBufferRPtrWriteBack at line 6871
//   VdSetGraphicsInterruptCallback at line 9330

//------------------------------------------------------------------------------
// VdSwap - Present/swap, frame boundary.
//
// Called once per guest frame. We use this to:
//   1. Pace the guest frame rate
//   2. Build the native frame scene from captured records
//   3. Optionally render natively instead of via xenos
//
// The original VdSwap is always called so the emulated path still works.
// When bk_native_render_scene is enabled, our native renderer overlays
// or replaces the emulated output.
//
// Original resolved via dlsym(RTLD_NEXT, "__imp__VdSwap") from
// librexruntime.so. Falls back to __imp__VdSwap symbol if dlsym fails.

extern "C" void __imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  if (g_in_hook) {
    // Already inside our hook - don't recurse. Just return.
    return;
  }
  g_in_hook = true;

  // Resolve the original on first call.
  if (!g_vd_swap_resolved) {
    g_vd_swap_resolved = true;
    g_real_vd_swap = reinterpret_cast<VdSwapFn>(
        dlsym(RTLD_NEXT, "__imp__VdSwap"));
    if (!g_real_vd_swap) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        REXLOG_WARN(
            "bk native-render: dlsym(RTLD_NEXT, __imp__VdSwap) returned null; "
            "emulated swap will be skipped");
      }
    }
  }

  const bool enabled = Enabled();
  const bool scene_enabled =
      enabled && REXCVAR_GET(bk_native_render_scene);

  static std::atomic<uint64_t> g_vdswap_calls{0};
  uint64_t my_call = g_vdswap_calls.fetch_add(1);

  // Pre-swap: pace the guest frame rate.
  if (enabled) {
    PaceGuestFrame();
  }

  // Call the original VdSwap so the emulated path still runs.
  if (g_real_vd_swap) {
    g_real_vd_swap(ctx, base);
  } else if (my_call < 3) {
    REXLOG_WARN(
        "bk native-render: __imp__VdSwap original not resolved (dlsym null); "
        "emulated swap skipped on call #{}", my_call);
  }

  // Post-swap: build native scene and render if enabled.
  if (scene_enabled) {
    bk::native::OnFrameEnd(base, g_frame_index, g_current_frame);
    g_current_frame.clear();
    ++g_frame_index;
  }

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// VdInitializeRingBuffer - GPU ring buffer initialization.
//
// Called during GPU setup. We hook it to capture the ring buffer
// configuration for later use by the native renderer.

extern "C" void __imp__VdInitializeRingBuffer(PPCContext& ctx, uint8_t* base) {
  if (g_in_hook) {
    return;
  }
  g_in_hook = true;

  if (Enabled()) {
    REXLOG_INFO("bk native-render: VdInitializeRingBuffer called");
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__VdInitializeRingBuffer"));
    g_real_vd_init_ring = real;
  }
  if (real) {
    real(ctx, base);
  }

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// VdEnableRingBufferRPtrWriteBack - Ring buffer read pointer writeback.

extern "C" void __imp__VdEnableRingBufferRPtrWriteBack(PPCContext& ctx,
                                                        uint8_t* base) {
  if (g_in_hook) {
    return;
  }
  g_in_hook = true;

  if (Enabled()) {
    REXLOG_INFO("bk native-render: VdEnableRingBufferRPtrWriteBack called");
  }

  using Fn = void (*)(PPCContext&, uint8_t*);
  static Fn real = nullptr;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    real = reinterpret_cast<Fn>(
        dlsym(RTLD_NEXT, "__imp__VdEnableRingBufferRPtrWriteBack"));
    g_real_vd_rptr_wb = real;
  }
  if (real) {
    real(ctx, base);
  }

  g_in_hook = false;
}

//------------------------------------------------------------------------------
// sub_824E04B0 - Pre-swap ring buffer processing.
//
// Called just before VdSwap each frame. Captures any draw commands
// submitted to the ring buffer this frame.

extern "C" void sub_824E04B0(PPCContext& ctx, uint8_t* base) {
  if (g_in_hook) {
    return;
  }
  g_in_hook = true;

  if (Enabled() && REXCVAR_GET(bk_native_render_scene)) {
    // Capture a submit record for this frame's draw submission.
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_frame.push_back({0, ctx.r3.u32, ctx.r4.u32, 0});
  }

  __imp__sub_824E04B0(ctx, base);

  g_in_hook = false;
}

//=============================================================================
// Hook registration (called once at startup)
//=============================================================================

void Install() {
  if (!Enabled()) {
    return;
  }
  if (!g_announced.exchange(true)) {
    REXLOG_INFO("bk native-render hook layer enabled");
    REXLOG_INFO("bk native-render: hooked __imp__VdSwap (frame boundary)");
    REXLOG_INFO("bk native-render: hooked sub_824E04B0 (pre-swap capture)");
  }
  // Per-function registration happens automatically via the REX_FUNC weak
  // alias mechanism: defining REX_FUNC(name) overrides __imp__name in the
  // function table. No explicit registry call is needed here.
}

}  // namespace bk::native_render
