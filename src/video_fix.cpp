// beautiful_katamari - app-level video/display workarounds.
//
// The runtime's VdGetCurrentDisplayInformation reports an enormous overscan:
// it fills display_window_overscan_left/top/right/bottom with a quarter of the
// display size (xboxkrnl_video.cpp: `overscan_x = mode.display_width / 4`).
// Real hardware reports a few percent, and many titles use these fields to
// decide how far to pull their UI inside the frame.  Beautiful Katamari draws
// the save-file screens as if the left quarter of the frame were outside the
// visible area: it blackens 25% of the left of the frame and clips its UI
// there (the "black band down one side" artifact).
//
// BK_VIDEO_LOG=1          dump every X_DISPLAY_INFO the guest asks for.
// BK_VIDEO_NO_OVERSCAN=1  zero the overscan fields after the runtime filled
//                         them in.

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <rex/ppc/context.h>
#include <rex/types.h>

namespace {

using PPCFuncPtr = void (*)(PPCContext&, uint8_t*);

PPCFuncPtr Resolve(const char* name) {
  auto fn = reinterpret_cast<PPCFuncPtr>(dlsym(RTLD_NEXT, name));
  if (!fn) {
    fn = reinterpret_cast<PPCFuncPtr>(dlsym(RTLD_DEFAULT, name));
  }
  return fn;
}

bool EnvFlag(const char* name) {
  const char* value = std::getenv(name);
  return value && *value && *value != '0';
}

uint16_t LoadBe16(const uint8_t* p) {
  return uint16_t((uint32_t(p[0]) << 8) | uint32_t(p[1]));
}

// X_DISPLAY_INFO (see rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp).
constexpr uint32_t kOffFrontBufferWidth = 0x00;    // be16
constexpr uint32_t kOffFrontBufferHeight = 0x02;   // be16
constexpr uint32_t kOffOverscanLeft = 0x40;        // be16
constexpr uint32_t kOffOverscanTop = 0x42;         // be16
constexpr uint32_t kOffOverscanRight = 0x44;       // be16
constexpr uint32_t kOffOverscanBottom = 0x46;      // be16
constexpr uint32_t kOffDisplayWidth = 0x48;        // be16
constexpr uint32_t kOffDisplayHeight = 0x4A;       // be16
constexpr uint32_t kOffActualDisplayWidth = 0x56;  // be16
constexpr uint32_t kDisplayInfoSize = 0x58;

}  // namespace

extern "C" __attribute__((visibility("default"))) void __imp__VdGetCurrentDisplayInformation(
    PPCContext& ctx, uint8_t* base) {
  static PPCFuncPtr real = Resolve("__imp__VdGetCurrentDisplayInformation");
  if (real) {
    real(ctx, base);
  }

  const uint32_t info_va = ctx.r3.u32;
  if (!info_va) {
    return;
  }
  uint8_t* info = base + info_va;

  if (EnvFlag("BK_VIDEO_LOG")) {
    static int calls = 0;
    if (calls < 12) {
      ++calls;
      std::fprintf(stderr,
                   "[video_fix] VdGetCurrentDisplayInformation(0x%08X) fb=%ux%u "
                   "overscan L/T/R/B=%u/%u/%u/%u display=%ux%u actual_w=%u\n",
                   info_va, LoadBe16(info + kOffFrontBufferWidth),
                   LoadBe16(info + kOffFrontBufferHeight), LoadBe16(info + kOffOverscanLeft),
                   LoadBe16(info + kOffOverscanTop), LoadBe16(info + kOffOverscanRight),
                   LoadBe16(info + kOffOverscanBottom), LoadBe16(info + kOffDisplayWidth),
                   LoadBe16(info + kOffDisplayHeight), LoadBe16(info + kOffActualDisplayWidth));
      if (calls == 1) {
        std::fprintf(stderr, "[video_fix] raw:");
        for (uint32_t i = 0; i < kDisplayInfoSize; ++i) {
          std::fprintf(stderr, " %02X", info[i]);
        }
        std::fprintf(stderr, "\n");
      }
    }
  }

  if (EnvFlag("BK_VIDEO_NO_OVERSCAN")) {
    // display_window_overscan_left..bottom, four big-endian uint16 fields.
    std::memset(info + kOffOverscanLeft, 0, 8);
  }
}
