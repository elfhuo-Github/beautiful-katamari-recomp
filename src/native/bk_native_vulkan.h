// beautiful_katamari - Vulkan context for native renderer.
//
// Creates and manages a standalone Vulkan device, swapchain, and command
// buffers for the native renderer. Runs in a separate X11 window showing
// the natively-rendered scene alongside the emulated xenos output.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>

namespace bk::native {

// Vulkan context: instance, device, swapchain, command pool, etc.
class VulkanContext {
 public:
  VulkanContext();
  ~VulkanContext();

  // Initialize Vulkan: instance, device, swapchain, command pool.
  bool Init(SDL_Window* window, uint32_t width, uint32_t height);

  // Recreate swapchain (e.g. after window resize).
  bool RecreateSwapchain(uint32_t width, uint32_t height);

  // Begin a frame: acquire next swapchain image, begin command buffer.
  bool BeginFrame();

  // End a frame: end command buffer, submit, present.
  bool EndFrame();

  // Get the current swapchain image index.
  uint32_t CurrentImageIndex() const { return current_image_index_; }

  // Get the current command buffer.
  VkCommandBuffer CurrentCommandBuffer() const { return current_command_buffer_; }

  // Get the current swapchain image view.
  VkImageView CurrentImageView() const {
    return swapchain_image_views_[current_image_index_];
  }

  // Get swapchain extent.
  VkExtent2D SwapchainExtent() const { return swapchain_extent_; }

  // Get Vulkan device.
  VkDevice Device() const { return device_; }

  // Get Vulkan physical device.
  VkPhysicalDevice PhysicalDevice() const { return physical_device_; }

  // Get Vulkan queue.
  VkQueue Queue() const { return queue_; }

  // Get queue family index.
  uint32_t QueueFamilyIndex() const { return queue_family_index_; }

  bool IsValid() const { return valid_; }

 private:
  bool CreateInstance();
  bool CreateDevice(SDL_Window* window);
  bool CreateSwapchain(uint32_t width, uint32_t height);
  bool CreateCommandPool();
  bool CreateCommandBuffers();
  bool CreateSyncObjects();
  void CleanupSwapchain();
  void CleanupDevice();

  SDL_Window* window_ = nullptr;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queue_family_index_ = 0;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D swapchain_extent_ = {};
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;

  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> command_buffers_;
  uint32_t current_image_index_ = 0;
  VkCommandBuffer current_command_buffer_ = VK_NULL_HANDLE;

  VkSemaphore image_available_semaphore_ = VK_NULL_HANDLE;
  VkSemaphore render_finished_semaphore_ = VK_NULL_HANDLE;
  VkFence in_flight_fence_ = VK_NULL_HANDLE;

  bool valid_ = false;
  bool frame_begun_ = false;
};

}  // namespace bk::native
