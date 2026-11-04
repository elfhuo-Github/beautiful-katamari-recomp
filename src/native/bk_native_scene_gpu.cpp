// beautiful_katamari - native Vulkan GPU renderer.
// Replaces emulated xenos GPU output when bk_native_render_scene is active.

#include "bk_native_scene_gpu.h"
#include "bk_native_scene.h"
#include "shaders/shader_data.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_gpu {
namespace {

struct Gpu {
  SDL_Window* window = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
  VkDescriptorPool dpool = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> swapchain_images;
  std::vector<VkImageView> swapchain_views;
  std::vector<VkFramebuffer> swapchain_fbs;
  uint32_t qfam = 0;
  uint32_t width = 1280, height = 720;
  uint32_t current_frame = 0;
  uint32_t swapchain_image_count = 0;
  bool initialized = false;
  bool failed = false;
  bool surface_ready = false;

  // Per-frame resources (triple buffering)
  static constexpr uint32_t kFramesInFlight = 3;
  VkCommandBuffer cmd[kFramesInFlight] = {};
  VkFence fence[kFramesInFlight] = {};
  VkSemaphore sem_image_available[kFramesInFlight] = {};
  VkSemaphore sem_render_finished[kFramesInFlight] = {};
  VkBuffer ubuf[kFramesInFlight] = {};
  VkDeviceMemory umem[kFramesInFlight] = {};
  VkDescriptorSet dset[kFramesInFlight] = {};
  uint32_t frame_index = 0;

  // Device buffers (receive data from staging via command buffer copies)
  VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
  VkDeviceMemory vbmem = VK_NULL_HANDLE, ibmem = VK_NULL_HANDLE;

  // Staging buffers for vertex/index uploads (host-visible, coherent)
  VkBuffer stage_vb = VK_NULL_HANDLE;
  VkDeviceMemory stage_vbmem = VK_NULL_HANDLE;
  VkBuffer stage_ib = VK_NULL_HANDLE;
  VkDeviceMemory stage_ibmem = VK_NULL_HANDLE;
};

Gpu g;

VkResult create_shader_module(VkDevice dev, const uint32_t* code, size_t size,
                              VkShaderModule* out) {
  VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = size;
  ci.pCode = code;
  return vkCreateShaderModule(dev, &ci, nullptr, out);
}

bool pick_queue_family(VkPhysicalDevice p, uint32_t* fam) {
  uint32_t n = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(p, &n, nullptr);
  std::vector<VkQueueFamilyProperties> props(n);
  vkGetPhysicalDeviceQueueFamilyProperties(p, &n, props.data());
  for (uint32_t i = 0; i < n; i++) {
    if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      VkBool32 present = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(p, i, g.surface, &present);
      if (present) {
        *fam = i;
        return true;
      }
    }
  }
  return false;
}

bool create_surface() {
  if (!g.window) return false;
  return true;
}

bool init_instance_and_surface() {
  unsigned ext_count = 0;
  const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
  if (!exts) return false;

  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "BKNativeRenderer";
  ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  ai.pEngineName = "BKNative";
  ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  ai.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = ext_count;
  ici.ppEnabledExtensionNames = exts;

  if (vkCreateInstance(&ici, nullptr, &g.instance) != VK_SUCCESS) return false;

  if (!SDL_Vulkan_CreateSurface(g.window, g.instance, nullptr, &g.surface)) {
    REXLOG_WARN("bk native-render: SDL_Vulkan_CreateSurface failed");
    return false;
  }

  VkPhysicalDevice phys_list[16];
  uint32_t phys_count = 16;
  vkEnumeratePhysicalDevices(g.instance, &phys_count, phys_list);
  if (phys_count == 0) return false;
  g.phys = phys_list[0];

  if (!pick_queue_family(g.phys, &g.qfam)) return false;

  float p = 1.0f;
  VkDeviceQueueCreateInfo dqci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  dqci.queueFamilyIndex = g.qfam;
  dqci.queueCount = 1;
  dqci.pQueuePriorities = &p;

  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &dqci;
  const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = dev_exts;

  if (vkCreateDevice(g.phys, &dci, nullptr, &g.device) != VK_SUCCESS) return false;
  vkGetDeviceQueue(g.device, g.qfam, 0, &g.queue);

  VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = g.qfam;
  if (vkCreateCommandPool(g.device, &pci, nullptr, &g.pool) != VK_SUCCESS) return false;

  // Descriptor set layout
  VkDescriptorSetLayoutBinding binding = {};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo dslci = {};
  dslci.bindingCount = 1;
  dslci.pBindings = &binding;
  if (vkCreateDescriptorSetLayout(g.device, &dslci, nullptr, &g.dsl) != VK_SUCCESS)
    return false;

  VkPipelineLayoutCreateInfo plci = {};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &g.dsl;
  if (vkCreatePipelineLayout(g.device, &plci, nullptr, &g.layout) != VK_SUCCESS)
    return false;

  // Shader modules
  VkShaderModule vsm, fsm;
  if (create_shader_module(g.device, vert_spv, vert_spv_size, &vsm) != VK_SUCCESS)
    return false;
  if (create_shader_module(g.device, frag_spv, frag_spv_size, &fsm) != VK_SUCCESS) {
    vkDestroyShaderModule(g.device, vsm, nullptr);
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vsm;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fsm;
  stages[1].pName = "main";

  VkVertexInputBindingDescription vibd = {};
  vibd.binding = 0;
  vibd.stride = 20;
  vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription viads[2] = {};
  viads[0].binding = 0; viads[0].location = 0;
  viads[0].format = VK_FORMAT_R32G32B32_SFLOAT; viads[0].offset = 0;
  viads[1].binding = 0; viads[1].location = 1;
  viads[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; viads[1].offset = 12;

  VkPipelineVertexInputStateCreateInfo vi = {};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &vibd;
  vi.vertexAttributeDescriptionCount = 2;
  vi.pVertexAttributeDescriptions = viads;

  VkPipelineInputAssemblyStateCreateInfo ia = {};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp = {};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs = {};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms = {};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState ca = {};
  ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb = {};
  cb.attachmentCount = 1;
  cb.pAttachments = &ca;

  // Render pass
  VkAttachmentDescription att = {};
  att.format = VK_FORMAT_B8G8R8A8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference aref = {};
  aref.attachment = 0;
  aref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription spd = {};
  spd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  spd.colorAttachmentCount = 1;
  spd.pColorAttachments = &aref;

  VkRenderPassCreateInfo rpci = {};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &spd;
  if (vkCreateRenderPass(g.device, &rpci, nullptr, &g.render_pass) != VK_SUCCESS) {
    vkDestroyShaderModule(g.device, vsm, nullptr);
    vkDestroyShaderModule(g.device, fsm, nullptr);
    return false;
  }

  VkGraphicsPipelineCreateInfo gpci = {};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vi;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vp;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cb;
  gpci.layout = g.layout;
  gpci.renderPass = g.render_pass;
  gpci.subpass = 0;

  if (vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                &g.pipeline) != VK_SUCCESS) {
    vkDestroyShaderModule(g.device, vsm, nullptr);
    vkDestroyShaderModule(g.device, fsm, nullptr);
    return false;
  }

  vkDestroyShaderModule(g.device, vsm, nullptr);
  vkDestroyShaderModule(g.device, fsm, nullptr);

  // Vertex/index buffers
  VkBufferCreateInfo bci = {};
  VkMemoryRequirements mr;
  VkMemoryAllocateInfo mai = {};
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(g.phys, &mp);
  uint32_t mt = 0;

  bci.size = 1 << 20;
  bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (vkCreateBuffer(g.device, &bci, nullptr, &g.vb) != VK_SUCCESS) return false;
  vkGetBufferMemoryRequirements(g.device, g.vb, &mr);
  mai.allocationSize = mr.size;
  mt = 0;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((mr.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      mt = i;
      break;
    }
  }
  mai.memoryTypeIndex = mt;
  if (vkAllocateMemory(g.device, &mai, nullptr, &g.vbmem) != VK_SUCCESS) return false;
  if (vkBindBufferMemory(g.device, g.vb, g.vbmem, 0) != VK_SUCCESS) return false;

  bci.size = 1 << 20;
  bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if (vkCreateBuffer(g.device, &bci, nullptr, &g.ib) != VK_SUCCESS) return false;
  vkGetBufferMemoryRequirements(g.device, g.ib, &mr);
  mai.allocationSize = mr.size;
  mt = 0;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((mr.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      mt = i;
      break;
    }
  }
  mai.memoryTypeIndex = mt;
  if (vkAllocateMemory(g.device, &mai, nullptr, &g.ibmem) != VK_SUCCESS) return false;
  if (vkBindBufferMemory(g.device, g.ib, g.ibmem, 0) != VK_SUCCESS) return false;

  // Per-frame resources (triple buffering)
  VkCommandBufferAllocateInfo cbai2 = {};
  cbai2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai2.commandPool = g.pool;
  cbai2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai2.commandBufferCount = Gpu::kFramesInFlight;
  if (vkAllocateCommandBuffers(g.device, &cbai2, &g.cmd[0]) != VK_SUCCESS)
    return false;

  VkFenceCreateInfo fci = {};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkSemaphoreCreateInfo sci = {};

  for (uint32_t i = 0; i < Gpu::kFramesInFlight; i++) {
    if (vkCreateFence(g.device, &fci, nullptr, &g.fence[i]) != VK_SUCCESS)
      return false;
    if (vkCreateSemaphore(g.device, &sci, nullptr,
                          &g.sem_image_available[i]) != VK_SUCCESS)
      return false;
    if (vkCreateSemaphore(g.device, &sci, nullptr,
                          &g.sem_render_finished[i]) != VK_SUCCESS)
      return false;

    // Per-frame uniform buffer (coherent, host-visible)
    VkBufferCreateInfo ubci = {};
    ubci.size = 256; // aligned
    ubci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    ubci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g.device, &ubci, nullptr, &g.ubuf[i]) != VK_SUCCESS)
      return false;
    vkGetBufferMemoryRequirements(g.device, g.ubuf[i], &mr);
    mai.allocationSize = mr.size;
    mt = 0;
    for (uint32_t j = 0; j < mp.memoryTypeCount; j++) {
      if ((mr.memoryTypeBits & (1u << j)) &&
          (mp.memoryTypes[j].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
        mt = j;
        break;
      }
    }
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(g.device, &mai, nullptr, &g.umem[i]) != VK_SUCCESS)
      return false;
    if (vkBindBufferMemory(g.device, g.ubuf[i], g.umem[i], 0) != VK_SUCCESS)
      return false;

    VkDescriptorSetAllocateInfo dsai = {};
    dsai.descriptorPool = g.dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g.dsl;
    if (vkAllocateDescriptorSets(g.device, &dsai, &g.dset[i]) != VK_SUCCESS)
      return false;

    VkDescriptorBufferInfo dbi = {};
    dbi.buffer = g.ubuf[i];
    dbi.offset = 0;
    dbi.range = 256;
    VkWriteDescriptorSet wds = {};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = g.dset[i];
    wds.dstBinding = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(g.device, 1, &wds, 0, nullptr);
  }

  // Staging buffers for vertex/index uploads (coherent, host-visible)
  VkBufferCreateInfo sbci = {};
  sbci.size = 1 << 20; // 1MB staging
  sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g.device, &sbci, nullptr, &g.stage_vb) != VK_SUCCESS)
    return false;
  vkGetBufferMemoryRequirements(g.device, g.stage_vb, &mr);
  mai.allocationSize = mr.size;
  mt = 0;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((mr.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      mt = i;
      break;
    }
  }
  mai.memoryTypeIndex = mt;
  if (vkAllocateMemory(g.device, &mai, nullptr, &g.stage_vbmem) != VK_SUCCESS)
    return false;
  if (vkBindBufferMemory(g.device, g.stage_vb, g.stage_vbmem, 0) != VK_SUCCESS)
    return false;

  sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (vkCreateBuffer(g.device, &sbci, nullptr, &g.stage_ib) != VK_SUCCESS)
    return false;
  vkGetBufferMemoryRequirements(g.device, g.stage_ib, &mr);
  mai.allocationSize = mr.size;
  mt = 0;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((mr.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      mt = i;
      break;
    }
  }
  mai.memoryTypeIndex = mt;
  if (vkAllocateMemory(g.device, &mai, nullptr, &g.stage_ibmem) != VK_SUCCESS)
    return false;
  if (vkBindBufferMemory(g.device, g.stage_ib, g.stage_ibmem, 0) != VK_SUCCESS)
    return false;

  g.initialized = true;
  REXLOG_INFO("bk native GPU renderer: Vulkan device initialized (triple buffered)");
  return true;
}

bool create_swapchain() {
  if (!g.device || !g.surface) return false;

  VkSurfaceCapabilitiesKHR caps;
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps) !=
      VK_SUCCESS)
    return false;

  g.width = caps.currentExtent.width != UINT32_MAX
                ? caps.currentExtent.width
                : 1280;
  g.height = caps.currentExtent.height != UINT32_MAX
                 ? caps.currentExtent.height
                 : 720;

  uint32_t fmt_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fmt_count, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fmt_count, fmts.data());
  VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
  VkColorSpaceKHR cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  if (fmt_count == 1 && fmts[0].format == VK_FORMAT_UNDEFINED) {
    fmt = VK_FORMAT_B8G8R8A8_UNORM;
    cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  } else {
    fmt = fmts[0].format;
    cs = fmts[0].colorSpace;
  }

  uint32_t present_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(g.phys, g.surface, &present_count, nullptr);
  std::vector<VkPresentModeKHR> pm(present_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(g.phys, g.surface, &present_count, pm.data());
  VkPresentModeKHR pmode = VK_PRESENT_MODE_FIFO_KHR;
  for (auto& m : pm) {
    if (m == VK_PRESENT_MODE_MAILBOX_KHR) { pmode = m; break; }
    if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) pmode = m;
  }

  uint32_t img_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
    img_count = caps.maxImageCount;

  VkSwapchainCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sci.surface = g.surface;
  sci.minImageCount = img_count;
  sci.imageFormat = fmt;
  sci.imageColorSpace = cs;
  sci.imageExtent = {g.width, g.height};
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = pmode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(g.device, &sci, nullptr, &g.swapchain) != VK_SUCCESS)
    return false;

  uint32_t sc_img_count = 0;
  vkGetSwapchainImagesKHR(g.device, g.swapchain, &sc_img_count, nullptr);
  g.swapchain_image_count = sc_img_count;
  g.swapchain_images.resize(sc_img_count);
  vkGetSwapchainImagesKHR(g.device, g.swapchain, &sc_img_count,
                          g.swapchain_images.data());

  g.swapchain_views.resize(sc_img_count);
  for (uint32_t i = 0; i < sc_img_count; i++) {
    VkImageViewCreateInfo ivci = {};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = g.swapchain_images[i];
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = fmt;
    ivci.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY};
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g.device, &ivci, nullptr, &g.swapchain_views[i]) !=
        VK_SUCCESS)
      return false;
  }

  g.swapchain_fbs.resize(sc_img_count);
  for (uint32_t i = 0; i < sc_img_count; i++) {
    VkFramebufferCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = g.render_pass;
    fci.attachmentCount = 1;
    fci.pAttachments = &g.swapchain_views[i];
    fci.width = g.width;
    fci.height = g.height;
    fci.layers = 1;
    if (vkCreateFramebuffer(g.device, &fci, nullptr, &g.swapchain_fbs[i]) !=
        VK_SUCCESS)
      return false;
  }

  g.surface_ready = true;
  REXLOG_INFO("bk native GPU renderer: swapchain created (%ux%u)", g.width, g.height);
  return true;
}

void destroy_swapchain() {
  for (auto fb : g.swapchain_fbs)
    if (fb) vkDestroyFramebuffer(g.device, fb, nullptr);
  g.swapchain_fbs.clear();
  for (auto iv : g.swapchain_views)
    if (iv) vkDestroyImageView(g.device, iv, nullptr);
  g.swapchain_views.clear();
  if (g.swapchain)
    vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
  g.swapchain = VK_NULL_HANDLE;
  g.swapchain_images.clear();
  g.surface_ready = false;
}

void shutdown_vulkan() {
  if (g.device == VK_NULL_HANDLE) return;
  vkDeviceWaitIdle(g.device);
  destroy_swapchain();
  if (g.render_pass) vkDestroyRenderPass(g.device, g.render_pass, nullptr);

  for (uint32_t i = 0; i < Gpu::kFramesInFlight; i++) {
    if (g.fence[i]) vkDestroyFence(g.device, g.fence[i], nullptr);
    if (g.sem_image_available[i])
      vkDestroySemaphore(g.device, g.sem_image_available[i], nullptr);
    if (g.sem_render_finished[i])
      vkDestroySemaphore(g.device, g.sem_render_finished[i], nullptr);
    if (g.ubuf[i]) vkDestroyBuffer(g.device, g.ubuf[i], nullptr);
    if (g.umem[i]) vkFreeMemory(g.device, g.umem[i], nullptr);
  }

  vkDestroyPipeline(g.device, g.pipeline, nullptr);
  vkDestroyPipelineLayout(g.device, g.layout, nullptr);
  vkDestroyDescriptorSetLayout(g.device, g.dsl, nullptr);
  vkDestroyDescriptorPool(g.device, g.dpool, nullptr);

  if (g.stage_vb) vkDestroyBuffer(g.device, g.stage_vb, nullptr);
  if (g.stage_vbmem) vkFreeMemory(g.device, g.stage_vbmem, nullptr);
  if (g.stage_ib) vkDestroyBuffer(g.device, g.stage_ib, nullptr);
  if (g.stage_ibmem) vkFreeMemory(g.device, g.stage_ibmem, nullptr);

  vkDestroyBuffer(g.device, g.vb, nullptr);
  vkFreeMemory(g.device, g.vbmem, nullptr);
  if (g.ib) vkDestroyBuffer(g.device, g.ib, nullptr);
  if (g.ibmem) vkFreeMemory(g.device, g.ibmem, nullptr);
  vkDestroyCommandPool(g.device, g.pool, nullptr);
  vkDestroyDevice(g.device, nullptr);
  if (g.surface) vkDestroySurfaceKHR(g.instance, g.surface, nullptr);
  vkDestroyInstance(g.instance, nullptr);
  g = Gpu();
}

}  // namespace

void Install() {
  if (g.initialized || g.failed) return;
  REXLOG_INFO("bk native GPU renderer: Install() called");
}

void SetWindow(SDL_Window* window) {
  g.window = window;
}

bool RenderFrame(uint8_t* guest_base) {
  if (g.failed) return false;

  REXLOG_INFO("bk native GPU renderer: RenderFrame called (window=%p, initialized=%d)",
              static_cast<void*>(g.window), g.initialized);

  // Lazy init on first call
  if (!g.initialized) {
    if (!g.window) {
      g.failed = true;
      REXLOG_WARN("bk native GPU renderer: no window set");
      return false;
    }
    if (!init_instance_and_surface()) {
      g.failed = true;
      REXLOG_WARN("bk native GPU renderer: instance/surface init failed");
      return false;
    }
    if (!create_swapchain()) {
      g.failed = true;
      REXLOG_WARN("bk native GPU renderer: swapchain init failed");
      return false;
    }
  }

  auto& scene = bk::native::CurrentScene();
  if (scene.items.empty()) return true;

  // Recreate swapchain if needed (surface changed)
  if (!g.surface_ready) {
    destroy_swapchain();
    if (!create_swapchain()) {
      g.failed = true;
      return false;
    }
  }

  uint32_t fi = g.frame_index;
  g.frame_index = (g.frame_index + 1) % Gpu::kFramesInFlight;

  // Wait for this frame's fence to be free (triple buffering)
  vkWaitForFences(g.device, 1, &g.fence[fi], VK_TRUE, UINT64_MAX);
  vkResetFences(g.device, 1, &g.fence[fi]);

  // Acquire next swapchain image
  uint32_t image_index = 0;
  vkAcquireNextImageKHR(g.device, g.swapchain, UINT64_MAX,
                        g.sem_image_available[fi], VK_NULL_HANDLE,
                        &image_index);

  VkCommandBufferBeginInfo cbbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(g.cmd[fi], &cbbi);

  // Upload uniform (view_proj) - coherent memory, direct memcpy
  void* ub_data;
  vkMapMemory(g.device, g.umem[fi], 0, 256, 0, &ub_data);
  memcpy(ub_data, scene.view_proj, 64);
  vkUnmapMemory(g.device, g.umem[fi]);

  // Begin render pass
  VkRenderPassBeginInfo rpbi = {};
  rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpbi.renderPass = g.render_pass;
  rpbi.framebuffer = g.swapchain_fbs[image_index];
  rpbi.renderArea = {{0, 0}, {g.width, g.height}};
  VkClearValue clear = {{{0.1f, 0.1f, 0.15f, 1.0f}}};
  rpbi.clearValueCount = 1;
  rpbi.pClearValues = &clear;
  vkCmdBeginRenderPass(g.cmd[fi], &rpbi, VK_SUBPASS_CONTENTS_INLINE);

  // Bind pipeline
  vkCmdBindPipeline(g.cmd[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
  vkCmdBindDescriptorSets(g.cmd[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1,
                          &g.dset[fi], 0, nullptr);

  // Dynamic viewport/scissor
  VkViewport vp = {0.0f, 0.0f, (float)g.width, (float)g.height, 0.0f, 1.0f};
  VkRect2D sc = {{0, 0}, {g.width, g.height}};
  vkCmdSetViewport(g.cmd[fi], 0, 1, &vp);
  vkCmdSetScissor(g.cmd[fi], 0, 1, &sc);

  // Vertex buffer
  VkDeviceSize vb_offset = 0;
  vkCmdBindVertexBuffers(g.cmd[fi], 0, 1, &g.vb, &vb_offset);

  // Draw items - upload vertex data to staging, then to device buffer
  size_t total_vb = 0;
  size_t total_ib = 0;
  for (auto& item : scene.items) {
    total_vb += item.vb_bytes;
    total_ib += item.ib_count * 4;
  }

  // Single upload for all vertex data this frame
  if (total_vb > 0) {
    void* stage_data;
    vkMapMemory(g.device, g.stage_vbmem, 0, total_vb, 0, &stage_data);
    uint8_t* dst = static_cast<uint8_t*>(stage_data);
    for (auto& item : scene.items) {
      if (item.vb_data && item.vb_bytes > 0) {
        memcpy(dst, item.vb_data, item.vb_bytes);
        dst += item.vb_bytes;
      }
    }
    vkUnmapMemory(g.device, g.stage_vbmem);
  }

  // Single upload for all index data this frame
  if (total_ib > 0) {
    void* stage_data;
    vkMapMemory(g.device, g.stage_ibmem, 0, total_ib, 0, &stage_data);
    uint8_t* dst = static_cast<uint8_t*>(stage_data);
    for (auto& item : scene.items) {
      size_t ib_bytes = item.ib_count * 4;
      if (item.ib_data && ib_bytes > 0) {
        memcpy(dst, item.ib_data, ib_bytes);
        dst += ib_bytes;
      }
    }
    vkUnmapMemory(g.device, g.stage_ibmem);
  }

  // Draw items
  size_t vb_offset_accum = 0;
  size_t ib_offset_accum = 0;
  for (auto& item : scene.items) {
    // Copy staged vertex data to device buffer via command buffer
    if (item.vb_data && item.vb_bytes > 0) {
      VkBufferCopy copy_region = {vb_offset_accum, vb_offset_accum, item.vb_bytes};
      vkCmdCopyBuffer(g.cmd[fi], g.stage_vb, g.vb, 1, &copy_region);
    }

    vkCmdBindVertexBuffers(g.cmd[fi], 0, 1, &g.vb, &vb_offset_accum);

    VkDeviceSize ib_offset = ib_offset_accum;
    vkCmdBindIndexBuffer(g.cmd[fi], g.ib, ib_offset, VK_INDEX_TYPE_UINT32);

    for (auto& draw : item.draws) {
      vkCmdDrawIndexed(g.cmd[fi], draw.index_count, 1, draw.start_index,
                       draw.base_vertex, 0);
    }

    vb_offset_accum += item.vb_bytes;
    ib_offset_accum += item.ib_count * 4;
  }

  vkCmdEndRenderPass(g.cmd[fi]);
  vkEndCommandBuffer(g.cmd[fi]);

  // Submit with semaphores
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &g.cmd[fi];
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &g.sem_image_available[fi];
  si.pWaitDstStageMask = wait_stages;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &g.sem_render_finished[fi];
  vkQueueSubmit(g.queue, 1, &si, g.fence[fi]);

  // Present with wait semaphore
  VkPresentInfoKHR pi = {};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &g.sem_render_finished[fi];
  pi.swapchainCount = 1;
  pi.pSwapchains = &g.swapchain;
  pi.pImageIndices = &image_index;
  pi.pResults = nullptr;
  vkQueuePresentKHR(g.queue, &pi);

  return true;
}

void Shutdown() {
  shutdown_vulkan();
}

}  // namespace bk::native_gpu
