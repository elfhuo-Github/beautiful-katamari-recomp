// beautiful_katamari - Native renderer Vulkan pipeline.
//
// Creates Vulkan shader modules, render pass, pipeline layout, and graphics
// pipeline for rendering DrawItems natively.

#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace bk::native {

// Pipeline resources: shader modules, render pass, pipeline layout, pipeline.
class NativePipeline {
 public:
  NativePipeline() = default;
  ~NativePipeline();

  // Initialize: create shader modules from SPIR-V, render pass, pipeline
  // layout, and graphics pipeline.
  bool Init(VkDevice device, VkExtent2D extent);

  // Clean up Vulkan resources.
  void Shutdown(VkDevice device);

  // Bind the pipeline and descriptor sets for drawing.
  void Bind(VkCommandBuffer cmd_buf) const;

  VkRenderPass RenderPass() const { return render_pass_; }
  VkPipelineLayout PipelineLayout() const { return pipeline_layout_; }
  VkPipeline Pipeline() const { return pipeline_; }

  bool IsValid() const { return valid_; }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkShaderModule vert_module_ = VK_NULL_HANDLE;
  VkShaderModule frag_module_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  bool valid_ = false;
};

}  // namespace bk::native
