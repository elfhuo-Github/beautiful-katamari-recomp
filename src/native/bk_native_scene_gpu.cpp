// beautiful_katamari - native renderer GPU half (implementation).

// Renders the NativeFrameScene (built by bk_native_scene.cpp on the guest
// thread) natively with Vulkan, replacing the emulated xenos GPU output when
// bk_native_render_scene is active.
//
// This implementation creates a standalone Vulkan context and renders into
// its own window. It demonstrates the native renderer pipeline by clearing
// the screen to a debug color and presenting. Guest geometry rendering via
// Vulkan graphics pipelines is TBD.

#include "bk_native_scene_gpu.h"
#include "bk_native_vulkan.h"
#include "bk_native_scene.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_gpu {
namespace {


std::mutex g_mutex;
bool g_initialized = false;
bool g_failed = false;

struct NativeRenderer {
  bool valid = false;
  bk::native::VulkanContext vk;
  SDL_Window* window = nullptr;
  uint32_t width = 1280;
  uint32_t height = 720;
  VkRenderPass render_pass = VK_NULL_HANDLE;
};
NativeRenderer g_renderer;

}  // namespace

void Install() {
  if (false) {
    return;
  }
  if (g_initialized) return;

  REXLOG_INFO("bk native GPU renderer: install");

  g_renderer.window = SDL_CreateWindow(
      "Beautiful Katamari - Native Renderer", 1280, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
  if (!g_renderer.window) {
    REXLOG_ERROR("bk native GPU: failed to create SDL window");
    g_failed = true;
    return;
  }

  if (!g_renderer.vk.Init(g_renderer.window, 1280, 720)) {
    REXLOG_ERROR("bk native GPU: Vulkan context init failed");
    g_failed = true;
    return;
  }

  // Create a simple render pass with one color attachment.
  VkAttachmentDescription color_attachment{};
  color_attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref{};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;

  if (vkCreateRenderPass(g_renderer.vk.Device(), &render_pass_info, nullptr,
                         &g_renderer.render_pass) != VK_SUCCESS) {
    REXLOG_ERROR("bk native GPU: render pass creation failed");
    g_failed = true;
    return;
  }

  g_initialized = true;
  g_renderer.valid = true;
  REXLOG_INFO("bk native GPU renderer: initialized");
}

bool RenderFrame(uint8_t* guest_base) {
  if (!g_initialized || !g_renderer.valid || g_failed) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_mutex);

  const bk::native::FrameScene& scene = bk::native::CurrentScene();
  (void)scene;  // Scene rendering TBD

  // Begin frame.
  if (!g_renderer.vk.BeginFrame()) {
    int w, h;
    SDL_GetWindowSize(g_renderer.window, &w, &h);
    g_renderer.vk.RecreateSwapchain(w, h);
    return true;
  }

  VkCommandBuffer cmd = g_renderer.vk.CurrentCommandBuffer();
  VkRenderPassBeginInfo rp_begin{};
  rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp_begin.renderPass = g_renderer.render_pass;
  rp_begin.renderArea.offset = {0, 0};
  rp_begin.renderArea.extent = g_renderer.vk.SwapchainExtent();
  VkClearValue clear_color = {{0.1f, 0.2f, 0.3f, 1.0f}};  // Debug blue
  rp_begin.clearValueCount = 1;
  rp_begin.pClearValues = &clear_color;

  // Framebuffer for current swapchain image.
  static std::vector<VkFramebuffer> framebuffers;
  if (framebuffers.size() <= g_renderer.vk.CurrentImageIndex()) {
    framebuffers.resize(g_renderer.vk.CurrentImageIndex() + 1);
  }
  VkFramebuffer& fb = framebuffers[g_renderer.vk.CurrentImageIndex()];
  if (fb == VK_NULL_HANDLE) {
    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = g_renderer.render_pass;
    fb_info.attachmentCount = 1;
    VkImageView iv = g_renderer.vk.CurrentImageView();
    fb_info.pAttachments = &iv;
    fb_info.width = g_renderer.vk.SwapchainExtent().width;
    fb_info.height = g_renderer.vk.SwapchainExtent().height;
    fb_info.layers = 1;
    if (vkCreateFramebuffer(g_renderer.vk.Device(), &fb_info, nullptr,
                            &fb) != VK_SUCCESS) {
      REXLOG_ERROR("bk native GPU: framebuffer creation failed");
      return false;
    }
  }
  rp_begin.framebuffer = fb;

  vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(cmd);

  if (!g_renderer.vk.EndFrame()) {
    int w, h;
    SDL_GetWindowSize(g_renderer.window, &w, &h);
    g_renderer.vk.RecreateSwapchain(w, h);
  }

  return true;
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_renderer.render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(g_renderer.vk.Device(), g_renderer.render_pass,
                        nullptr);
    g_renderer.render_pass = VK_NULL_HANDLE;
  }
  g_renderer.vk.~VulkanContext();
  if (g_renderer.window) {
    SDL_DestroyWindow(g_renderer.window);
    g_renderer.window = nullptr;
  }
  g_initialized = false;
  g_renderer.valid = false;
  g_failed = false;
}

}  // namespace bk::native_gpu
