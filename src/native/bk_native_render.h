// beautiful_katamari - native renderer hook layer (public interface).

// The guest-function hooks in bk_native_render.cpp are always present;
// everything they do is gated behind the bk_native_render cvar. Install()
// only announces state and validates configuration. Scene capture/build and
// rendering live in bk_native_scene.cpp / bk_native_scene_gpu.cpp.
//
// Inspired by skate3recomp's skate3_native_render.h, rewritten for
// Beautiful Katamari's guest render pipeline.

#pragma once

#include <cstdint>

namespace bk::native_render {

void Install();

// Precise guest frame pacing (guest render thread, swap boundary).
// Caps the guest frame rate to `bk_guest_fps_cap` fps using absolute-schedule
// pacing so sleep jitter never accumulates. No-op when cap < 1.
void PaceGuestFrame();

}  // namespace bk::native_render
