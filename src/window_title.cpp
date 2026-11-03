// window_title.cpp - live status in the game window title.
//
// Shows the renderer, guest output resolution, live guest frame rate, process
// CPU usage and the SDK version, refreshed twice per second, e.g.:
//
//   Beautiful Katamari | Vulkan FSI 1280x720 | 59.9 fps (16.7 ms) | CPU 312% | rexglue 0.10.0
//
// The guest frame counter comes from interposing Presenter::RefreshGuestOutput,
// which the GPU plugin calls once per guest front buffer swap.  That is the
// only per-frame hook the runtime exposes (rex::perf's kFps/kFrameTimeUs
// counters are declared but never written by the prebuilt libraries), and it is
// immune to the presenter repainting the same guest frame (which is what made
// MangoHud report five-digit frame rates).
//
// Interposition works because the executable is linked with --export-dynamic
// (see CMakeLists.txt): its definition is found before the one in
// librexruntime.so for references from other shared objects, and the real
// implementation is still reachable through dlsym(RTLD_NEXT).
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/version.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "window_title.h"

namespace {

std::atomic<uint64_t> g_guest_frames{0};

constexpr const char* kRefreshGuestOutputSymbol =
    "_ZN3rex2ui9Presenter18RefreshGuestOutputEjjjjSt8functionIFbRNS1_25GuestOutputRefreshContextEEE";

// utime + stime of the whole process, in clock ticks.
uint64_t ProcessCpuTicks() {
  std::FILE* f = std::fopen("/proc/self/stat", "r");
  if (!f) {
    return 0;
  }
  char buffer[4096];
  size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, f);
  std::fclose(f);
  buffer[read] = '\0';
  // The command name is parenthesised and may contain spaces; fields after it
  // start after the last ')'.
  char* close = std::strrchr(buffer, ')');
  if (!close) {
    return 0;
  }
  // Fields from 3 (state) onwards; utime is field 14, stime is 15.
  char* cursor = close + 1;
  for (int field = 3; field < 14; ++field) {
    while (*cursor == ' ') ++cursor;
    while (*cursor && *cursor != ' ') ++cursor;
    if (!*cursor) return 0;
  }
  unsigned long long utime = 0, stime = 0;
  if (std::sscanf(cursor, " %llu %llu", &utime, &stime) != 2) {
    return 0;
  }
  return uint64_t(utime) + uint64_t(stime);
}

struct TitleState {
  rex::ui::Window* window = nullptr;
  rex::ui::WindowedAppContext* app_context = nullptr;
  std::thread thread;
  std::atomic<bool> stop{false};
  std::string static_prefix;
};

TitleState g_title;

void RunTitleThread() {
  long ticks_per_second = ::sysconf(_SC_CLK_TCK);
  if (ticks_per_second <= 0) {
    ticks_per_second = 100;
  }
  uint64_t last_frames = g_guest_frames.load(std::memory_order_relaxed);
  uint64_t last_cpu = ProcessCpuTicks();
  auto last_time = std::chrono::steady_clock::now();
  double smoothed_fps = 0.0;
  char title[512];

  while (!g_title.stop.load(std::memory_order_relaxed)) {
    // Sleep in small slices so shutdown does not wait for a full period.
    for (int i = 0; i < 10 && !g_title.stop.load(std::memory_order_relaxed); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (g_title.stop.load(std::memory_order_relaxed)) {
      break;
    }

    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - last_time).count();
    uint64_t frames = g_guest_frames.load(std::memory_order_relaxed);
    uint64_t cpu = ProcessCpuTicks();
    if (seconds <= 0.0) {
      continue;
    }

    double instant_fps = double(frames - last_frames) / seconds;
    double cpu_percent = double(cpu - last_cpu) / double(ticks_per_second) / seconds * 100.0;
    last_frames = frames;
    last_cpu = cpu;
    last_time = now;

    // Light exponential smoothing so the number is readable without lagging
    // behind big scene changes.
    smoothed_fps = smoothed_fps > 0.0 ? smoothed_fps * 0.5 + instant_fps * 0.5 : instant_fps;

    double frame_ms = smoothed_fps > 0.01 ? 1000.0 / smoothed_fps : 0.0;
    std::snprintf(title, sizeof(title), "%s | %.1f fps (%.1f ms) | CPU %.0f%% | rexglue %s",
                  g_title.static_prefix.c_str(), smoothed_fps, frame_ms, cpu_percent,
                  REXGLUE_VERSION_STRING);

    rex::ui::Window* window = g_title.window;
    rex::ui::WindowedAppContext* context = g_title.app_context;
    std::string text(title);
    context->CallInUIThread([window, text]() {
      if (window) {
        window->SetTitle(text);
      }
    });
  }
}

}  // namespace

namespace rex::ui {

// Interposes the SDK implementation to count guest frames.  See the file
// comment for why this is safe and why --export-dynamic is required.
bool Presenter::RefreshGuestOutput(
    uint32_t frontbuffer_width, uint32_t frontbuffer_height, uint32_t display_aspect_ratio_x,
    uint32_t display_aspect_ratio_y,
    std::function<bool(GuestOutputRefreshContext& context)> refresher) {
  g_guest_frames.fetch_add(1, std::memory_order_relaxed);

  using RealFunction = bool (*)(Presenter*, uint32_t, uint32_t, uint32_t, uint32_t,
                                std::function<bool(GuestOutputRefreshContext&)>);
  static RealFunction real = reinterpret_cast<RealFunction>(
      dlsym(RTLD_NEXT, kRefreshGuestOutputSymbol));
  if (!real) {
    static bool reported = false;
    if (!reported) {
      reported = true;
      std::fprintf(stderr, "[bk] ERROR: could not resolve the real %s via dlsym\n",
                   kRefreshGuestOutputSymbol);
    }
    return false;
  }
  return real(this, frontbuffer_width, frontbuffer_height, display_aspect_ratio_x,
              display_aspect_ratio_y, std::move(refresher));
}

}  // namespace rex::ui

namespace bk {

void StartTitleUpdater(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                       std::string_view static_prefix) {
  StopTitleUpdater();
  if (!window) {
    return;
  }
  g_title.window = window;
  g_title.app_context = &app_context;
  g_title.static_prefix.assign(static_prefix);
  g_title.stop.store(false, std::memory_order_relaxed);
  g_title.thread = std::thread(RunTitleThread);
}

void StopTitleUpdater() {
  if (!g_title.thread.joinable()) {
    return;
  }
  g_title.stop.store(true, std::memory_order_relaxed);
  g_title.thread.join();
  // Flush any queued SetTitle calls while the window is still alive.
  if (g_title.app_context) {
    g_title.app_context->ExecutePendingFunctionsFromUIThread();
  }
  g_title.window = nullptr;
  g_title.app_context = nullptr;
}

}  // namespace bk
