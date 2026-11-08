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
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include <rex/cvar.h>
#include <rex/logging.h>

VkDebugUtilsMessengerEXT g_debug_messenger = VK_NULL_HANDLE;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void* p_user_data) {
    if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "[Vulkan Debug] %s: %s\n",
                p_callback_data->pMessageIdName ? p_callback_data->pMessageIdName : "?",
                p_callback_data->pMessage ? p_callback_data->pMessage : "(no message)");
    }
    return VK_FALSE;
}

PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;

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

  // Depth image
  VkImage depth_image = VK_NULL_HANDLE;
  VkDeviceMemory depth_mem = VK_NULL_HANDLE;
  std::vector<VkImageView> depth_views;

  // Staging buffers for vertex/index uploads (host-visible, coherent)
  VkBuffer stage_vb = VK_NULL_HANDLE;
  VkDeviceMemory stage_vbmem = VK_NULL_HANDLE;
  VkBuffer stage_ib = VK_NULL_HANDLE;
  VkDeviceMemory stage_ibmem = VK_NULL_HANDLE;

  // Independent render thread (bypasses guest hook interposition issues)
  std::thread render_thread;
  std::atomic<bool> render_thread_running{false};
  std::atomic<bool> render_thread_should_stop{false};
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

  // Add debug utils extension
  std::vector<const char*> instance_exts(exts, exts + ext_count);
  instance_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "BKNativeRenderer";
  ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  ai.pEngineName = "BKNative";
  ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  ai.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = (uint32_t)instance_exts.size();
  ici.ppEnabledExtensionNames = instance_exts.data();
  ici.enabledLayerCount = 0;

  VkResult vr = vkCreateInstance(&ici, nullptr, &g.instance);
  if (vr != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateInstance failed: %d ===\n", vr);
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateInstance succeeded ===\n");

  // Load debug utils function pointers
  pfnCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g.instance, "vkCreateDebugUtilsMessengerEXT");
  pfnDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g.instance, "vkDestroyDebugUtilsMessengerEXT");

  // Create debug messenger
  if (pfnCreateDebugUtilsMessengerEXT) {
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debug_callback;
    debugCreateInfo.pUserData = nullptr;

    VkResult dr = pfnCreateDebugUtilsMessengerEXT(g.instance, &debugCreateInfo, nullptr, &g_debug_messenger);
    if (dr != VK_SUCCESS) {
      fprintf(stderr, "=== bk native GPU renderer: vkCreateDebugUtilsMessengerEXT failed: %d ===\n", dr);
    } else {
      fprintf(stderr, "=== bk native GPU renderer: debug messenger created ===\n");
    }
  } else {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateDebugUtilsMessengerEXT not available ===\n");
  }

  if (!SDL_Vulkan_CreateSurface(g.window, g.instance, nullptr, &g.surface)) {
    fprintf(stderr, "=== bk native GPU renderer: SDL_Vulkan_CreateSurface failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: SDL_Vulkan_CreateSurface succeeded ===\n");

  VkPhysicalDevice phys_list[16];
  uint32_t phys_count = 16;
  vkEnumeratePhysicalDevices(g.instance, &phys_count, phys_list);
  fprintf(stderr, "=== bk native GPU renderer: phys_count=%u ===\n", phys_count);
  if (phys_count == 0) return false;
  g.phys = phys_list[0];

  if (!pick_queue_family(g.phys, &g.qfam)) {
    fprintf(stderr, "=== bk native GPU renderer: pick_queue_family failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: pick_queue_family succeeded, qfam=%u ===\n", g.qfam);

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

  VkResult vr2 = vkCreateDevice(g.phys, &dci, nullptr, &g.device);
  if (vr2 != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateDevice failed: %d ===\n", vr2);
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateDevice succeeded ===\n");
  vkGetDeviceQueue(g.device, g.qfam, 0, &g.queue);

  VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = g.qfam;
  if (vkCreateCommandPool(g.device, &pci, nullptr, &g.pool) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateCommandPool failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateCommandPool succeeded ===\n");

  // Descriptor set layout
  VkDescriptorSetLayoutBinding binding = {};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo dslci = {};
  dslci.bindingCount = 1;
  dslci.pBindings = &binding;
  if (vkCreateDescriptorSetLayout(g.device, &dslci, nullptr, &g.dsl) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateDescriptorSetLayout failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateDescriptorSetLayout succeeded ===\n");

  VkPipelineLayoutCreateInfo plci = {};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &g.dsl;
  if (vkCreatePipelineLayout(g.device, &plci, nullptr, &g.layout) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreatePipelineLayout failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreatePipelineLayout succeeded ===\n");

  // Shader modules
  VkShaderModule vsm, fsm;
  if (create_shader_module(g.device, vert_spv, vert_spv_size, &vsm) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: create_shader_module(vert) failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: create_shader_module(vert) succeeded ===\n");
  if (create_shader_module(g.device, frag_spv, frag_spv_size, &fsm) != VK_SUCCESS) {
    vkDestroyShaderModule(g.device, vsm, nullptr);
    fprintf(stderr, "=== bk native GPU renderer: create_shader_module(frag) failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: create_shader_module(frag) succeeded ===\n");

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
  VkViewport vp_viewport = {};
  VkRect2D vp_scissor = {};
  vp.pViewports = &vp_viewport;
  vp.pScissors = &vp_scissor;

  VkPipelineRasterizationStateCreateInfo rs = {};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  rs.depthClampEnable = VK_FALSE;
  rs.rasterizerDiscardEnable = VK_FALSE;
  rs.depthBiasEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo ds = {};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_FALSE;
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  ds.depthBoundsTestEnable = VK_FALSE;
  ds.stencilTestEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo ms = {};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState ca = {};
  ca.blendEnable = VK_TRUE;
  ca.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  ca.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  ca.colorBlendOp = VK_BLEND_OP_ADD;
  ca.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  ca.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  ca.alphaBlendOp = VK_BLEND_OP_ADD;
  ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb = {};
  cb.attachmentCount = 1;
  cb.pAttachments = &ca;

  // Render pass with depth attachment
  VkAttachmentDescription atts[2] = {};
  atts[0].format = VK_FORMAT_B8G8R8A8_UNORM;
  atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
  atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  atts[1].format = VK_FORMAT_D32_SFLOAT;
  atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
  atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference aref = {};
  aref.attachment = 0;
  aref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference dref = {};
  dref.attachment = 1;
  dref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription spd = {};
  spd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  spd.colorAttachmentCount = 1;
  spd.pColorAttachments = &aref;
  spd.pDepthStencilAttachment = &dref;

  VkRenderPassCreateInfo rpci = {};
  rpci.attachmentCount = 2;
  rpci.pAttachments = atts;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &spd;
  if (vkCreateRenderPass(g.device, &rpci, nullptr, &g.render_pass) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateRenderPass failed ===\n");
    vkDestroyShaderModule(g.device, vsm, nullptr);
    vkDestroyShaderModule(g.device, fsm, nullptr);
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateRenderPass succeeded ===\n");

  VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkGraphicsPipelineCreateInfo gpci = {};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vi;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &vp;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pDepthStencilState = &ds;
  gpci.pColorBlendState = &cb;
  gpci.pDynamicState = &dynamicState;
  gpci.layout = g.layout;
  gpci.renderPass = g.render_pass;
  gpci.subpass = 0;

  VkResult vr3 = vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                &g.pipeline);
  if (vr3 != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateGraphicsPipelines failed: %d ===\n", vr3);
    vkDestroyShaderModule(g.device, vsm, nullptr);
    vkDestroyShaderModule(g.device, fsm, nullptr);
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateGraphicsPipelines succeeded ===\n");

  vkDestroyShaderModule(g.device, vsm, nullptr);
  vkDestroyShaderModule(g.device, fsm, nullptr);

  // Vertex/index buffers
  VkBufferCreateInfo bci = {};
  VkMemoryRequirements mr;
  VkMemoryAllocateInfo mai = {};
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(g.phys, &mp);
  uint32_t mt = 0;

  fprintf(stderr, "=== bk native GPU renderer: about to create vertex buffer ===\n");
  bci.size = 1 << 20;
  bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (vkCreateBuffer(g.device, &bci, nullptr, &g.vb) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkCreateBuffer(vb) failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkCreateBuffer(vb) succeeded ===\n");
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
  if (vkAllocateMemory(g.device, &mai, nullptr, &g.vbmem) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkAllocateMemory(vbmem) failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkAllocateMemory(vbmem) succeeded ===\n");
  if (vkBindBufferMemory(g.device, g.vb, g.vbmem, 0) != VK_SUCCESS) {
    fprintf(stderr, "=== bk native GPU renderer: vkBindBufferMemory(vb) failed ===\n");
    return false;
  }
  fprintf(stderr, "=== bk native GPU renderer: vkBindBufferMemory(vb) succeeded ===\n");

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

  // Create depth image and views
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(g.phys, &mp);
  VkImageCreateInfo dic = {};
  dic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  dic.imageType = VK_IMAGE_TYPE_2D;
  dic.format = VK_FORMAT_D32_SFLOAT;
  dic.extent = {g.width, g.height, 1};
  dic.mipLevels = 1;
  dic.arrayLayers = 1;
  dic.samples = VK_SAMPLE_COUNT_1_BIT;
  dic.tiling = VK_IMAGE_TILING_OPTIMAL;
  dic.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  dic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  dic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(g.device, &dic, nullptr, &g.depth_image) != VK_SUCCESS)
    return false;
  VkMemoryRequirements dm_req;
  vkGetImageMemoryRequirements(g.device, g.depth_image, &dm_req);
  VkMemoryAllocateInfo dm_ai = {};
  dm_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  dm_ai.allocationSize = dm_req.size;
  uint32_t dm_ti = 0;
  for (uint32_t j = 0; j < mp.memoryTypeCount; j++) {
    if ((dm_req.memoryTypeBits & (1u << j)) &&
        (mp.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      dm_ti = j;
      break;
    }
  }
  dm_ai.memoryTypeIndex = dm_ti;
  if (vkAllocateMemory(g.device, &dm_ai, nullptr, &g.depth_mem) != VK_SUCCESS)
    return false;
  if (vkBindImageMemory(g.device, g.depth_image, g.depth_mem, 0) != VK_SUCCESS)
    return false;

  g.depth_views.resize(sc_img_count);
  for (uint32_t i = 0; i < sc_img_count; i++) {
    VkImageViewCreateInfo divci = {};
    divci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    divci.image = g.depth_image;
    divci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    divci.format = VK_FORMAT_D32_SFLOAT;
    divci.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY};
    divci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g.device, &divci, nullptr, &g.depth_views[i]) !=
        VK_SUCCESS)
      return false;
  }

  g.swapchain_fbs.resize(sc_img_count);
  for (uint32_t i = 0; i < sc_img_count; i++) {
    VkImageView attachments[2] = {g.swapchain_views[i], g.depth_views[i]};
    VkFramebufferCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = g.render_pass;
    fci.attachmentCount = 2;
    fci.pAttachments = attachments;
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
  for (auto dv : g.depth_views)
    if (dv) vkDestroyImageView(g.device, dv, nullptr);
  g.depth_views.clear();
  if (g.depth_image) vkDestroyImage(g.device, g.depth_image, nullptr);
  g.depth_image = VK_NULL_HANDLE;
  if (g.depth_mem) vkFreeMemory(g.device, g.depth_mem, nullptr);
  g.depth_mem = VK_NULL_HANDLE;
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

  if (g.depth_image) vkDestroyImage(g.device, g.depth_image, nullptr);
  if (g.depth_mem) vkFreeMemory(g.device, g.depth_mem, nullptr);
  vkDestroyBuffer(g.device, g.vb, nullptr);
  vkFreeMemory(g.device, g.vbmem, nullptr);
  if (g.ib) vkDestroyBuffer(g.device, g.ib, nullptr);
  if (g.ibmem) vkFreeMemory(g.device, g.ibmem, nullptr);
  vkDestroyCommandPool(g.device, g.pool, nullptr);
  vkDestroyDevice(g.device, nullptr);
  if (g.surface) vkDestroySurfaceKHR(g.instance, g.surface, nullptr);
  vkDestroyInstance(g.instance, nullptr);
  // Reset fields manually (can't use g = Gpu() because std::thread is not copyable)
  g.instance = VK_NULL_HANDLE;
  g.phys = VK_NULL_HANDLE;
  g.device = VK_NULL_HANDLE;
  g.queue = VK_NULL_HANDLE;
  g.pool = VK_NULL_HANDLE;
  g.layout = VK_NULL_HANDLE;
  g.pipeline = VK_NULL_HANDLE;
  g.dsl = VK_NULL_HANDLE;
  g.dpool = VK_NULL_HANDLE;
  g.render_pass = VK_NULL_HANDLE;
  g.surface = VK_NULL_HANDLE;
  g.swapchain = VK_NULL_HANDLE;
  g.swapchain_images.clear();
  g.swapchain_views.clear();
  g.swapchain_fbs.clear();
  g.qfam = 0;
  g.width = 1280;
  g.height = 720;
  g.current_frame = 0;
  g.swapchain_image_count = 0;
  g.initialized = false;
  g.failed = false;
  g.surface_ready = false;
  g.frame_index = 0;
}

}  // namespace

void SetWindow() {
  // Find the SDL window directly (GetNativeWindowHandle may return null)
  int window_count = 0;
  SDL_Window** sdl_windows = SDL_GetWindows(&window_count);
  if (sdl_windows && window_count > 0) {
    g.window = sdl_windows[0];
    fprintf(stderr, "=== bk native GPU renderer: SetWindow found SDL_Window=%p ===\n", g.window);
  } else {
    fprintf(stderr, "=== bk native GPU renderer: SetWindow - no SDL windows found ===\n");
  }
}

void render_thread_func() {
  REXLOG_INFO("bk native GPU renderer: render thread started");

  // Wait for window to be set
  while (!g.render_thread_should_stop && !g.window) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  fprintf(stderr, "=== bk native GPU renderer: render thread detected window=%p ===\n", g.window);

  if (g.render_thread_should_stop) {
    g.render_thread_running = false;
    return;
  }

  // Initialize Vulkan
  fprintf(stderr, "=== bk native GPU renderer: render thread calling init_instance_and_surface ===\n");
  if (!init_instance_and_surface()) {
    g.failed = true;
    REXLOG_WARN("bk native GPU renderer: instance/surface init failed");
    g.render_thread_running = false;
    return;
  }
  fprintf(stderr, "=== bk native GPU renderer: render thread calling create_swapchain ===\n");
  if (!create_swapchain()) {
    g.failed = true;
    REXLOG_WARN("bk native GPU renderer: swapchain init failed");
    g.render_thread_running = false;
    return;
  }
  g.initialized = true;
  fprintf(stderr, "=== bk native GPU renderer: Vulkan initialized in render thread ===\n");
  REXLOG_INFO("bk native GPU renderer: Vulkan initialized in render thread");

  // Render loop (uncapped - runs as fast as Vulkan allows)
  while (!g.render_thread_should_stop) {
    auto start = std::chrono::steady_clock::now();

    // Render one frame (calls the existing RenderFrame logic inline)
    auto& scene = bk::native::CurrentScene();
    if (!scene.items.empty()) {
      if (!g.surface_ready) {
        destroy_swapchain();
        if (!create_swapchain()) {
          g.failed = true;
          break;
        }
      }

      uint32_t fi = g.frame_index;
      g.frame_index = (g.frame_index + 1) % Gpu::kFramesInFlight;

      vkWaitForFences(g.device, 1, &g.fence[fi], VK_TRUE, UINT64_MAX);
      vkResetFences(g.device, 1, &g.fence[fi]);

      uint32_t image_index = 0;
      vkAcquireNextImageKHR(g.device, g.swapchain, UINT64_MAX,
                            g.sem_image_available[fi], VK_NULL_HANDLE,
                            &image_index);

      VkCommandBufferBeginInfo cbbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(g.cmd[fi], &cbbi);

      void* ub_data;
      vkMapMemory(g.device, g.umem[fi], 0, 256, 0, &ub_data);
      memcpy(ub_data, scene.view_proj, 64);
      vkUnmapMemory(g.device, g.umem[fi]);

      VkRenderPassBeginInfo rpbi = {};
      rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpbi.renderPass = g.render_pass;
      rpbi.framebuffer = g.swapchain_fbs[image_index];
      rpbi.renderArea = {{0, 0}, {g.width, g.height}};
      VkClearValue clear[2] = {};
      clear[0].color = {{0.1f, 0.1f, 0.15f, 1.0f}};
      clear[1].depthStencil = {1.0f, 0};
      rpbi.clearValueCount = 2;
      rpbi.pClearValues = clear;
      vkCmdBeginRenderPass(g.cmd[fi], &rpbi, VK_SUBPASS_CONTENTS_INLINE);

      if (g.pipeline != VK_NULL_HANDLE && !scene.items.empty()) {
        vkCmdBindPipeline(g.cmd[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
        vkCmdBindDescriptorSets(g.cmd[fi], VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1,
                                &g.dset[fi], 0, nullptr);

        VkViewport vp = {0.0f, 0.0f, (float)g.width, (float)g.height, 0.0f, 1.0f};
        VkRect2D sc = {{0, 0}, {g.width, g.height}};
        vkCmdSetViewport(g.cmd[fi], 0, 1, &vp);
        vkCmdSetScissor(g.cmd[fi], 0, 1, &sc);

        VkDeviceSize vb_offset = 0;
        vkCmdBindVertexBuffers(g.cmd[fi], 0, 1, &g.vb, &vb_offset);

        // Upload vertex/index data from guest memory to staging buffers
        size_t total_vb = 0;
        size_t total_ib = 0;
        for (auto& item : scene.items) {
          total_vb += item.vb_bytes;
          total_ib += item.ib_count * 4;
        }

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
      }

      vkCmdEndRenderPass(g.cmd[fi]);
      vkEndCommandBuffer(g.cmd[fi]);

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

      VkPresentInfoKHR pi = {};
      pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      pi.waitSemaphoreCount = 1;
      pi.pWaitSemaphores = &g.sem_render_finished[fi];
      pi.swapchainCount = 1;
      pi.pSwapchains = &g.swapchain;
      pi.pImageIndices = &image_index;
      pi.pResults = nullptr;
      vkQueuePresentKHR(g.queue, &pi);
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = end - start;
    // Cap render thread at 60 FPS (~16ms per frame) to prevent CPU spinning
    constexpr auto kFrameTime = std::chrono::milliseconds(16);
    if (elapsed < kFrameTime) {
      std::this_thread::sleep_for(kFrameTime - elapsed);
    }
  }

  g.render_thread_running = false;
  REXLOG_INFO("bk native GPU renderer: render thread stopped");
}

void Install() {
  if (g.initialized || g.failed || g.render_thread_running) return;
  REXLOG_INFO("bk native GPU renderer: Install() called");

  g.render_thread_should_stop = false;
  g.render_thread = std::thread(render_thread_func);
  g.render_thread_running = true;
}



bool RenderFrame(uint8_t* guest_base) {
  (void)guest_base;
  // RenderFrame is now driven by the render thread.
  // This stub is kept for API compatibility.
  return true;
}
}  // namespace bk::native_gpu
