// beautiful_katamari - Native renderer Vulkan pipeline implementation.
#include <rex/logging.h>

#include "bk_native_pipeline.h"
#include "bk_native_shaders_spirv.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace bk::native {

NativePipeline::~NativePipeline() {
  // Shutdown should be called explicitly; destructor is a safety net.
}

bool NativePipeline::Init(VkDevice device, VkExtent2D extent) {
  device_ = device;

  // Create vertex shader module.
  VkShaderModuleCreateInfo vert_create_info{};
  vert_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  vert_create_info.codeSize = sizeof(kVertexShaderSpirv);
  vert_create_info.pCode = kVertexShaderSpirv;
  if (vkCreateShaderModule(device_, &vert_create_info, nullptr,
                           &vert_module_) != VK_SUCCESS) {
    std::fprintf(stderr, "NATIVE GPU: vertex shader module failed\n"); REXLOG_ERROR("bk native GPU: vertex shader module creation failed");
    return false;
  }

  // Create fragment shader module.
  VkShaderModuleCreateInfo frag_create_info{};
  frag_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  frag_create_info.codeSize = sizeof(kFragmentShaderSpirv);
  frag_create_info.pCode = kFragmentShaderSpirv;
  if (vkCreateShaderModule(device_, &frag_create_info, nullptr,
                           &frag_module_) != VK_SUCCESS) {
    std::fprintf(stderr, "NATIVE GPU: fragment shader module failed\n"); REXLOG_ERROR("bk native GPU: fragment shader module creation failed");
    return false;
  }

  // Shader stage create infos.
  VkPipelineShaderStageCreateInfo vert_stage{};
  vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_stage.module = vert_module_;
  vert_stage.pName = "main";

  VkPipelineShaderStageCreateInfo frag_stage{};
  frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_stage.module = frag_module_;
  frag_stage.pName = "main";

  VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

  // Vertex input: position (vec2, location 0) and color (vec4, location 1).
  // No vertex input: shader outputs constant position and color.


  VkPipelineVertexInputStateCreateInfo vertex_input_info{};
  vertex_input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount = 0;
  vertex_input_info.pVertexBindingDescriptions = nullptr;
  vertex_input_info.vertexAttributeDescriptionCount = 0;
  vertex_input_info.pVertexAttributeDescriptions = nullptr;

  // Input assembly: triangle list.
  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  // Viewport and scissor: dynamic.
  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  // Rasterizer.
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  // Multisampling: disabled.
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Depth/stencil: disabled.
  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_FALSE;
  depth_stencil.depthWriteEnable = VK_FALSE;

  // Color blending: one attachment, write all channels.
  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo color_blending{};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  // Dynamic state: viewport and scissor.
  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount =
      static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
  dynamic_state.pDynamicStates = dynamic_states;

  // Pipeline layout: no push constants, no descriptor sets.
  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 0;
  pipeline_layout_info.pSetLayouts = nullptr;
  pipeline_layout_info.pushConstantRangeCount = 0;
  pipeline_layout_info.pPushConstantRanges = nullptr;

  if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr,
                             &pipeline_layout_) != VK_SUCCESS) {
    std::fprintf(stderr, "NATIVE GPU: pipeline layout failed\n"); REXLOG_ERROR("bk native GPU: pipeline layout creation failed");
    return false;
  }

  // Render pass: single subpass, one color attachment.
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

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  if (vkCreateRenderPass(device_, &render_pass_info, nullptr,
                         &render_pass_) != VK_SUCCESS) {
    std::fprintf(stderr, "NATIVE GPU: render pass failed\n"); REXLOG_ERROR("bk native GPU: render pass creation failed");
    return false;
  }

  // Graphics pipeline create info.
  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info,
                                nullptr, &pipeline_) != VK_SUCCESS) {
    std::fprintf(stderr, "NATIVE GPU: graphics pipeline failed\n"); REXLOG_ERROR("bk native GPU: graphics pipeline creation failed");
    return false;
  }

  valid_ = true;
  return true;
}

void NativePipeline::Shutdown(VkDevice device) {
  if (pipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
  }
  if (pipeline_layout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    pipeline_layout_ = VK_NULL_HANDLE;
  }
  if (render_pass_ != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, render_pass_, nullptr);
    render_pass_ = VK_NULL_HANDLE;
  }
  if (frag_module_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, frag_module_, nullptr);
    frag_module_ = VK_NULL_HANDLE;
  }
  if (vert_module_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, vert_module_, nullptr);
    vert_module_ = VK_NULL_HANDLE;
  }
  device_ = VK_NULL_HANDLE;
  valid_ = false;
}

void NativePipeline::Bind(VkCommandBuffer cmd_buf) const {
  vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  VkViewport viewport{};
  viewport.width = static_cast<float>(1280);
  viewport.height = static_cast<float>(720);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
  VkRect2D scissor{};
  scissor.extent.width = 1280;
  scissor.extent.height = 720;
  vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
}

}  // namespace bk::native
