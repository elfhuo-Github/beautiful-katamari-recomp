// beautiful_katamari - Vulkan context implementation.

#include "bk_native_vulkan.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace bk::native {
namespace {

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = false;
#endif

constexpr char kValidationLayerName[] = "VK_LAYER_KHRONOS_validation";
constexpr char kDeviceExtensions[] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

uint32_t FindMemoryType(VkPhysicalDevice physical_device, uint32_t type_filter,
                        VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
    if ((type_filter & (1u << i)) &&
        (mem_properties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool CheckValidationLayerSupport() {
  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  std::vector<VkLayerProperties> available_layers(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
  for (const char* layer_name : {kValidationLayerName}) {
    bool found = false;
    for (const auto& layer : available_layers) {
      if (strcmp(layer.layerName, layer_name) == 0) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

}  // namespace

//------------------------------------------------------------------------------
// VulkanContext
//------------------------------------------------------------------------------

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
  CleanupDevice();
}

bool VulkanContext::Init(SDL_Window* window, uint32_t width, uint32_t height) {
  window_ = window;

  if (!CreateInstance()) return false;
  if (!CreateDevice(window)) return false;
  if (!CreateSwapchain(width, height)) return false;
  if (!CreateCommandPool()) return false;
  if (!CreateCommandBuffers()) return false;
  if (!CreateSyncObjects()) return false;

  valid_ = true;
  return true;
}

bool VulkanContext::CreateInstance() {
  if (kEnableValidationLayers && !CheckValidationLayerSupport()) {
    std::fprintf(stderr,
                 "validation layers requested but not available\n");
    return false;
  }

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Beautiful Katamari Native Renderer";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_3;

  // Get required extensions from SDL.
  Uint32 sdl_ext_count = 0;
  const char* const* sdl_exts =
      SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
  std::vector<const char*> extensions(sdl_exts, sdl_exts + sdl_ext_count);

  if (kEnableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount =
      static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
  if (kEnableValidationLayers) {
    create_info.enabledLayerCount = 1;
    const char* layer_name = kValidationLayerName;
    create_info.ppEnabledLayerNames = &layer_name;

    debug_create_info.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_create_info.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
                                           VkDebugUtilsMessageTypeFlagsEXT,
                                           const VkDebugUtilsMessengerCallbackDataEXT*
                                               p_callback_data,
                                           void*) -> VkBool32 {
      std::fprintf(stderr, "validation layer: %s\n",
                   p_callback_data->pMessage);
      return VK_FALSE;
    };
    create_info.pNext = &debug_create_info;
  } else {
    create_info.enabledLayerCount = 0;
    create_info.pNext = nullptr;
  }

  if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
    std::fprintf(stderr, "failed to create Vulkan instance\n");
    return false;
  }
  return true;
}

bool VulkanContext::CreateDevice(SDL_Window* window) {
  // Create Vulkan surface from SDL window.
  if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
    std::fprintf(stderr, "failed to create Vulkan surface\n");
    return false;
  }

  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
  if (device_count == 0) {
    std::fprintf(stderr,
                 "failed to find GPUs with Vulkan support\n");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

  // Pick the first discrete GPU, fall back to any.
  for (const auto& device : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      physical_device_ = device;
      break;
    }
  }
  if (physical_device_ == VK_NULL_HANDLE) {
    physical_device_ = devices[0];
  }

  // Find queue family with graphics support.
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count,
                                           queue_families.data());

  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queue_family_index_ = i;
      break;
    }
  }

  VkDeviceQueueCreateInfo queue_create_info{};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = queue_family_index_;
  queue_create_info.queueCount = 1;
  float queue_priority = 1.0f;
  queue_create_info.pQueuePriorities = &queue_priority;

  VkPhysicalDeviceFeatures device_features{};
  device_features.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pQueueCreateInfos = &queue_create_info;
  create_info.queueCreateInfoCount = 1;
  create_info.pEnabledFeatures = &device_features;
  create_info.enabledExtensionCount = 1;
  const char* dev_ext = kDeviceExtensions;
  create_info.ppEnabledExtensionNames = &dev_ext;

  if (kEnableValidationLayers) {
    create_info.enabledLayerCount = 1;
    const char* layer_name = kValidationLayerName;
    create_info.ppEnabledLayerNames = &layer_name;
  } else {
    create_info.enabledLayerCount = 0;
  }

  if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) !=
      VK_SUCCESS) {
    std::fprintf(stderr, "failed to create logical device\n");
    return false;
  }

  vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);
  return true;
}

bool VulkanContext::CreateSwapchain(uint32_t width, uint32_t height) {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_,
                                            &capabilities);

  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  VkExtent2D extent = capabilities.currentExtent;
  if (capabilities.currentExtent.width == UINT32_MAX) {
    extent.width = width;
    extent.height = height;
  }

  // Query surface formats.
  uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &format_count,
                                       surface_formats.data());

  VkSurfaceFormatKHR surface_format = {};
  if (format_count == 1 &&
      surface_formats[0].format == VK_FORMAT_UNDEFINED) {
    surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
    surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  } else {
    surface_format = surface_formats[0];
  }

  // Query present modes.
  uint32_t present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                            &present_mode_count, nullptr);
  std::vector<VkPresentModeKHR> present_modes(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                            &present_mode_count,
                                            present_modes.data());

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (const auto& mode : present_modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      present_mode = mode;
      break;
    }
  }

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface_;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_) !=
      VK_SUCCESS) {
    std::fprintf(stderr, "failed to create swapchain\n");
    return false;
  }

  vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
  swapchain_images_.resize(image_count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &image_count,
                          swapchain_images_.data());

  swapchain_image_views_.resize(image_count);
  for (size_t i = 0; i < image_count; i++) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = swapchain_images_[i];
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = surface_format.format;
    view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view_info, nullptr,
                          &swapchain_image_views_[i]) != VK_SUCCESS) {
      std::fprintf(stderr, "failed to create image views\n");
      return false;
    }
  }

  swapchain_extent_ = extent;
  return true;
}

bool VulkanContext::CreateCommandPool() {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.queueFamilyIndex = queue_family_index_;
  pool_info.flags = 0;

  if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) !=
      VK_SUCCESS) {
    std::fprintf(stderr, "failed to create command pool\n");
    return false;
  }
  return true;
}

bool VulkanContext::CreateCommandBuffers() {
  command_buffers_.resize(swapchain_images_.size());

  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool_;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount =
      static_cast<uint32_t>(command_buffers_.size());

  if (vkAllocateCommandBuffers(device_, &alloc_info,
                               command_buffers_.data()) != VK_SUCCESS) {
    std::fprintf(stderr, "failed to allocate command buffers\n");
    return false;
  }
  return true;
}

bool VulkanContext::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateSemaphore(device_, &semaphore_info, nullptr,
                        &image_available_semaphore_) != VK_SUCCESS ||
      vkCreateSemaphore(device_, &semaphore_info, nullptr,
                        &render_finished_semaphore_) != VK_SUCCESS ||
      vkCreateFence(device_, &fence_info, nullptr, &in_flight_fence_) !=
          VK_SUCCESS) {
    std::fprintf(stderr, "failed to create synchronization objects\n");
    return false;
  }
  return true;
}

void VulkanContext::CleanupSwapchain() {
  for (auto image_view : swapchain_image_views_) {
    vkDestroyImageView(device_, image_view, nullptr);
  }
  swapchain_image_views_.clear();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

void VulkanContext::CleanupDevice() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }

  CleanupSwapchain();

  if (command_pool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_, command_pool_, nullptr);
  }

  if (image_available_semaphore_ != VK_NULL_HANDLE) {
    vkDestroySemaphore(device_, image_available_semaphore_, nullptr);
  }
  if (render_finished_semaphore_ != VK_NULL_HANDLE) {
    vkDestroySemaphore(device_, render_finished_semaphore_, nullptr);
  }
  if (in_flight_fence_ != VK_NULL_HANDLE) {
    vkDestroyFence(device_, in_flight_fence_, nullptr);
  }

  if (device_ != VK_NULL_HANDLE) {
    vkDestroyDevice(device_, nullptr);
  }

  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
  }
}

bool VulkanContext::RecreateSwapchain(uint32_t width, uint32_t height) {
  vkDeviceWaitIdle(device_);
  CleanupSwapchain();
  return CreateSwapchain(width, height) && CreateCommandBuffers();
}

bool VulkanContext::BeginFrame() {
  vkWaitForFences(device_, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX);

  VkResult result = vkAcquireNextImageKHR(
      device_, swapchain_, UINT64_MAX, image_available_semaphore_,
      VK_NULL_HANDLE, &current_image_index_);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return false;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    std::fprintf(stderr, "failed to acquire swapchain image\n");
    return false;
  }

  vkResetFences(device_, 1, &in_flight_fence_);

  current_command_buffer_ = command_buffers_[current_image_index_];
  vkResetCommandBuffer(current_command_buffer_, 0);

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(current_command_buffer_, &begin_info) !=
      VK_SUCCESS) {
    std::fprintf(stderr, "failed to begin recording command buffer\n");
    return false;
  }

  frame_begun_ = true;
  return true;
}

bool VulkanContext::EndFrame() {
  if (!frame_begun_) return false;

  VkResult result = vkEndCommandBuffer(current_command_buffer_);
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "failed to record command buffer\n");
    return false;
  }

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  VkSemaphore wait_semaphores[] = {image_available_semaphore_};
  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &current_command_buffer_;
  VkSemaphore signal_semaphores[] = {render_finished_semaphore_};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;

  vkResetFences(device_, 1, &in_flight_fence_);
  if (vkQueueSubmit(queue_, 1, &submit_info, in_flight_fence_) !=
      VK_SUCCESS) {
    std::fprintf(stderr, "failed to submit draw command buffer\n");
    return false;
  }

  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain_;
  present_info.pImageIndices = &current_image_index_;

  result = vkQueuePresentKHR(queue_, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return false;
  }
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "failed to present swapchain image\n");
    return false;
  }

  frame_begun_ = false;
  return true;
}

}  // namespace bk::native
