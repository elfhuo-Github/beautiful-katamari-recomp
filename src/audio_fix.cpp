// beautiful_katamari - app-level audio workarounds.
//
// Two runtime behaviours break XAudio2 streaming in this game:
//
// 1. The guest programs XMA contexts by writing the 64-byte XMA_CONTEXT_DATA
//    directly into guest memory.  It sets loop_count to 255 ("loop forever")
//    but leaves loop_start/loop_end at 0.  The runtime clamps a zero loop_end
//    to 32 bits and therefore treats *every* decode as a loop-end frame: it
//    resets input_buffer_read_offset to 32 after each decode and limits the
//    frame output to `loop_subframe_end + 1` subframes.  The result is that
//    one 128-sample subframe of frame 0 is decoded over and over, which is the
//    constant buzzy tone that obscures the music.  Real hardware ignores a
//    zero-length loop region, so clear loop_count when loop_end is 0.
//
// 2. While the guest streams, it refills input buffer 0 in place.  Nothing
//    here changes that; the loop-count fix alone lets the decoder advance
//    through the buffer instead of restarting at bit 32 forever.
//
// The context patcher runs on a 1 ms timer so newly created/started contexts
// are corrected before they can produce more than a subframe of wrong output.
// Each fix is a single aligned 32-bit store, so it cannot tear against the
// runtime reading the context.
//
// Set BK_AUDIO_CAPTURE=<path> to additionally dump every render frame the
// guest submits (6 channels x 256 big-endian float samples per frame) for
// offline analysis.

#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include <rex/ppc/context.h>
#include <rex/types.h>

namespace {

using PPCFuncPtr = void (*)(PPCContext&, uint8_t*);

constexpr uint32_t kContextCount = 32;
constexpr uint32_t kContextMask = 0x000FF000u;  // loop_count bits 12..19
constexpr uint32_t kLoopEndMask = 0x03FFFFFFu;  // loop_end bits 0..25

std::atomic<uint8_t*> g_base{nullptr};
std::atomic<uint32_t> g_contexts[kContextCount];
std::atomic<int> g_context_count{0};
std::atomic<bool> g_patcher_started{false};

PPCFuncPtr Resolve(const char* name) {
  auto fn = reinterpret_cast<PPCFuncPtr>(dlsym(RTLD_NEXT, name));
  if (!fn) {
    fn = reinterpret_cast<PPCFuncPtr>(dlsym(RTLD_DEFAULT, name));
  }
  return fn;
}

inline uint32_t LoadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void StoreBe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

void PatchContext(uint8_t* base, uint32_t guest_va) {
  if (!base || !guest_va) {
    return;
  }
  uint8_t* p = base + guest_va;
  const uint32_t data0 = LoadBe32(p);
  if (!data0) {
    return;  // Not initialized yet.
  }
  const uint32_t loop_count = (data0 >> 12) & 0xFF;
  if (!loop_count) {
    return;
  }
  // Only override the degenerate "loop forever over an empty region" setup.
  const uint32_t loop_end = LoadBe32(p + 16) & kLoopEndMask;
  if (loop_end != 0) {
    return;
  }
  StoreBe32(p, data0 & ~kContextMask);
}

void PatchAll(uint8_t* base) {
  const int count = g_context_count.load(std::memory_order_acquire);
  for (int i = 0; i < count; i++) {
    PatchContext(base, g_contexts[i].load(std::memory_order_relaxed));
  }
}

void RegisterContext(uint32_t guest_va) {
  if (!guest_va || (guest_va & 0x3F)) {
    return;
  }
  const int count = g_context_count.load(std::memory_order_acquire);
  for (int i = 0; i < count; i++) {
    if (g_contexts[i].load(std::memory_order_relaxed) == guest_va) {
      return;
    }
  }
  if (count >= static_cast<int>(kContextCount)) {
    return;
  }
  g_contexts[count].store(guest_va, std::memory_order_relaxed);
  g_context_count.store(count + 1, std::memory_order_release);

  bool expected = false;
  if (g_patcher_started.compare_exchange_strong(expected, true)) {
    std::thread([] {
      while (true) {
        PatchAll(g_base.load(std::memory_order_acquire));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }).detach();
  }
}

bool LoggingEnabled() {
  static const bool enabled = std::getenv("BK_AUDIO_LOG") != nullptr;
  return enabled;
}

// ── Optional capture of the guest's submitted render frames ─────────────────

std::FILE* g_capture = nullptr;
std::mutex g_capture_mutex;
std::atomic<uint32_t> g_capture_frames{0};

void CaptureInit() {
  static std::once_flag once;
  std::call_once(once, [] {
    const char* path = std::getenv("BK_AUDIO_CAPTURE");
    if (path && *path) {
      g_capture = std::fopen(path, "wb");
      if (g_capture) {
        std::fprintf(stderr, "[audio_fix] capturing render frames to %s\n", path);
      }
    }
  });
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void __imp__XMACreateContext(PPCContext& ctx,
                                                                               uint8_t* base) {
  static PPCFuncPtr real = Resolve("__imp__XMACreateContext");
  const uint32_t out_va = ctx.r3.u32;
  if (real) {
    real(ctx, base);
  }

  g_base.store(base, std::memory_order_release);

  uint32_t context_va = 0;
  if (out_va) {
    context_va = LoadBe32(base + out_va);
  }
  if (context_va) {
    RegisterContext(context_va);
    if (LoggingEnabled()) {
      std::fprintf(stderr, "[audio_fix] XMACreateContext -> 0x%08X\n", context_va);
    }
  }
}

extern "C" __attribute__((visibility("default"))) void __imp__XAudioSubmitRenderDriverFrame(
    PPCContext& ctx, uint8_t* base) {
  static PPCFuncPtr real = Resolve("__imp__XAudioSubmitRenderDriverFrame");

  g_base.store(base, std::memory_order_release);
  PatchAll(base);

  CaptureInit();
  if (g_capture) {
    const uint32_t samples_va = ctx.r4.u32;
    const uint32_t frame = g_capture_frames.fetch_add(1, std::memory_order_relaxed);
    if (samples_va) {
      constexpr size_t kFrameBytes = 6 * 256 * sizeof(float);
      std::lock_guard<std::mutex> guard(g_capture_mutex);
      std::fwrite(base + samples_va, 1, kFrameBytes, g_capture);
    }
    if (frame == 0) {
      std::fprintf(stderr, "[audio_fix] first render frame: samples=0x%08X\n", samples_va);
    }
  }

  if (real) {
    real(ctx, base);
  }
}
