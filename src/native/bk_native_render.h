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

// Precise guest frame pacing (guest render thread, swap boundary).
// Caps the guest frame rate to `bk_guest_fps_cap` fps using absolute-schedule
// pacing so sleep jitter never accumulates. No-op when cap < 1.
void PaceGuestFrame();

// Cvar accessors (defined in bk_native_render.cpp; avoids cross-TU
// REXCVAR_DECLARE linkage issues).
bool IsNativeRenderEnabled();
bool IsNativeRenderSceneEnabled();

// Install() at startup - called from ReXApp::OnPreSetup()
void Install();

// Set the SDL window handle for Vulkan surface creation. Called from
// ReXApp::OnPostSetup() after the window exists.
// Finds the SDL window internally since GetNativeWindowHandle may return null.
void SetWindow();

// Called at the guest swap boundary: builds the native frame scene and
// renders it natively when bk_native_render_scene is active.
// (Implementation in bk::native::OnFrameEnd, declared in bk_native_scene.h)

}  // namespace bk::native_render
