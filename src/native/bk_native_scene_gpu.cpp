// beautiful_katamari - native Vulkan GPU renderer.
// Replaces emulated xenos GPU output when bk_native_render_scene is active.

#include "bk_native_scene_gpu.h"
#include "bk_native_scene.h"
#include "shaders/shader_data.h"

#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_gpu {
namespace {

struct Gpu {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
  VkDescriptorSet dset = VK_NULL_HANDLE;
  VkBuffer ubuf = VK_NULL_HANDLE;
  VkDeviceMemory umem = VK_NULL_HANDLE;
  VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
  VkDeviceMemory vbmem = VK_NULL_HANDLE, ibmem = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkSemaphore sem = VK_NULL_HANDLE;
  uint32_t qfam = 0;
  bool initialized = false;
  bool failed = false;
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
      *fam = i;
      return true;
    }
  }
  return false;
}

bool init_vulkan(SDL_Window* win) {
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "BKNativeRenderer";
  ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  ai.pEngineName = "BKNative";
  ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  ai.apiVersion = VK_API_VERSION_1_3;

  unsigned ext_count = 0;
  const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
  if (!exts) return false;

  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = ext_count;
  ici.ppEnabledExtensionNames = exts;

  if (vkCreateInstance(&ici, nullptr, &g.instance) != VK_SUCCESS) return false;

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

  VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = g.pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(g.device, &cbai, &g.cmd) != VK_SUCCESS) return false;

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

  VkViewport viewport = {};
  VkRect2D scissor = {};
  VkPipelineViewportStateCreateInfo vp = {};
  vp.viewportCount = 1;
  vp.pViewports = &viewport;
  vp.scissorCount = 1;
  vp.pScissors = &scissor;

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
  gpci.renderPass = VK_NULL_HANDLE; // dynamic rendering

  if (vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                &g.pipeline) != VK_SUCCESS) {
    vkDestroyShaderModule(g.device, vsm, nullptr);
    vkDestroyShaderModule(g.device, fsm, nullptr);
    return false;
  }

  vkDestroyShaderModule(g.device, vsm, nullptr);
  vkDestroyShaderModule(g.device, fsm, nullptr);

  // Uniform buffer
  VkBufferCreateInfo bci = {};
  bci.size = 64;
  bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g.device, &bci, nullptr, &g.ubuf) != VK_SUCCESS) return false;

  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(g.device, g.ubuf, &mr);
  VkMemoryAllocateInfo mai = {};
  mai.allocationSize = mr.size;
  // Find memory type
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(g.phys, &mp);
  uint32_t mt = 0;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((mr.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      mt = i;
      break;
    }
  }
  mai.memoryTypeIndex = mt;
  if (vkAllocateMemory(g.device, &mai, nullptr, &g.umem) != VK_SUCCESS) return false;
  if (vkBindBufferMemory(g.device, g.ubuf, g.umem, 0) != VK_SUCCESS) return false;

  // Vertex/index buffers
  bci.size = 1 << 20; // 1MB vertex buffer
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

  bci.size = 1 << 20; // 1MB index buffer
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

  // Descriptor pool + set
  VkDescriptorPoolSize dps = {};
  dps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  dps.descriptorCount = 1;
  VkDescriptorPoolCreateInfo dpi = {};
  dpi.maxSets = 1;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &dps;
  if (vkCreateDescriptorPool(g.device, &dpi, nullptr, nullptr) != VK_SUCCESS)
    return false;

  VkFenceCreateInfo fci = {};
  if (vkCreateFence(g.device, &fci, nullptr, &g.fence) != VK_SUCCESS) return false;

  g.initialized = true;
  REXLOG_INFO("bk native GPU renderer: initialized");
  return true;
}

void shutdown_vulkan() {
  if (g.device == VK_NULL_HANDLE) return;
  vkDeviceWaitIdle(g.device);
  vkDestroyFence(g.device, g.fence, nullptr);
  vkDestroyPipeline(g.device, g.pipeline, nullptr);
  vkDestroyPipelineLayout(g.device, g.layout, nullptr);
  vkDestroyDescriptorSetLayout(g.device, g.dsl, nullptr);
  vkDestroyBuffer(g.device, g.ib, nullptr);
  vkFreeMemory(g.device, g.ibmem, nullptr);
  vkDestroyBuffer(g.device, g.vb, nullptr);
  vkFreeMemory(g.device, g.vbmem, nullptr);
  vkDestroyBuffer(g.device, g.ubuf, nullptr);
  vkFreeMemory(g.device, g.umem, nullptr);
  vkDestroyCommandPool(g.device, g.pool, nullptr);
  vkDestroyDevice(g.device, nullptr);
  vkDestroyInstance(g.instance, nullptr);
  g = Gpu();
}

}  // namespace

void Install() {
  if (g.initialized || g.failed) return;
  REXLOG_INFO("bk native GPU renderer: Install() called");
  // Vulkan init deferred to first RenderFrame (needs SDL window)
}

bool RenderFrame(uint8_t* guest_base) {
  if (g.failed) return false;
  if (!g.initialized) {
    // Get SDL window from context - for now we can't init without window
    g.failed = true;
    REXLOG_WARN("bk native GPU renderer: no window available");
    return false;
  }

  auto& scene = bk::native::CurrentScene();
  if (scene.items.empty()) return true;

  vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
  vkResetFences(g.device, 1, &g.fence);

  VkCommandBufferBeginInfo cbbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(g.cmd, &cbbi);

  // Upload uniform (view_proj)
  float ub[16];
  memcpy(ub, scene.view_proj, 64);
  void* data;
  vkMapMemory(g.device, g.umem, 0, 64, 0, &data);
  memcpy(data, ub, 64);
  vkUnmapMemory(g.device, g.umem);

  // Bind pipeline
  vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
  vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1,
                          &g.dset, 0, nullptr);

  // Dynamic viewport/scissor
  VkViewport vp = {0, 0, 1280, 720, 0.0f, 1.0f};
  VkRect2D sc = {{0, 0}, {1280, 720}};
  vkCmdSetViewport(g.cmd, 0, 1, &vp);
  vkCmdSetScissor(g.cmd, 0, 1, &sc);

  // Vertex buffer
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.vb, &offset);

  // Draw items
  for (auto& item : scene.items) {
    // Upload vertex data from guest memory
    if (item.vb_data && item.vb_bytes > 0) {
      vkMapMemory(g.device, g.vbmem, 0, item.vb_bytes, 0, &data);
      memcpy(data, item.vb_data, item.vb_bytes);
      vkUnmapMemory(g.device, g.vbmem);
    }

    vkCmdBindIndexBuffer(g.cmd, g.ib, 0, VK_INDEX_TYPE_UINT32);

    for (auto& draw : item.draws) {
      vkCmdDrawIndexed(g.cmd, draw.index_count, 1, draw.start_index,
                       draw.base_vertex, 0);
    }
  }

  vkEndCommandBuffer(g.cmd);
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &g.cmd;
  vkQueueSubmit(g.queue, 1, &si, g.fence);
  vkQueueWaitIdle(g.queue);

  return true;
}

void Shutdown() {
  shutdown_vulkan();
}

}  // namespace bk::native_gpu
