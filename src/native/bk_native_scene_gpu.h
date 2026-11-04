// beautiful_katamari - native renderer GPU half (public interface).

// Renders the NativeFrameScene (built by bk_native_scene.cpp on the guest
// thread) natively with Vulkan, replacing the emulated xenos GPU output when
// bk_native_render_scene is active. Runs on the GPU render thread.
//
// Inspired by skate3recomp's skate3_native_scene_gpu.cpp, rewritten for BK.

#pragma once

#include <cstdint>

namespace bk::native_gpu {

// Initialize the native GPU renderer (Vulkan instance, device, queues).
// Called once at startup after the window/SDL is available.
void Install();

// Set the SDL window handle for Vulkan surface/swapchain creation.
// Finds the SDL window internally since GetNativeWindowHandle may return null.
void SetWindow();

// Render one frame: read the current NativeFrameScene, upload vertex/index
// data and constants, draw all items, and present through the Vulkan swapchain.
// Called once per guest frame at the swap boundary.
// Returns false if the native path failed (caller falls back to emulated).
bool RenderFrame(uint8_t* guest_base);

// Shutdown the native GPU renderer.
void Shutdown();

}  // namespace bk::native_gpu
