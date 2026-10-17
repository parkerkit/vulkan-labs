#include "framework/pipeline.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
namespace {
VkPipelineShaderStageCreateInfo GetStage(VkShaderStageFlagBits stage, VkShaderModule module) {
  VkPipelineShaderStageCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .stage = stage,
    .module = module,
    .pName = "main",
    .pSpecializationInfo = nullptr,
  };
  return info;
}
}  // namespace

void Pipeline::CreateLayout(const PipelineLayoutDesc& desc) {
  VkPipelineLayoutCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .setLayoutCount = static_cast<uint32_t>(desc.set_layouts.size()),
    .pSetLayouts = desc.set_layouts.data(),
    .pushConstantRangeCount = static_cast<uint32_t>(desc.push_constants.size()),
    .pPushConstantRanges = desc.push_constants.data(),
  };
  detail::Check(vkCreatePipelineLayout(m_device, &info, nullptr, &m_layout), "Create pipeline layout");
}

Pipeline Pipeline::CreateGraphics(Device& device, const GraphicsPipelineDesc& desc) {
  detail::Require(desc.vertex_shader != VK_NULL_HANDLE && desc.fragment_shader != VK_NULL_HANDLE,
                  "Graphics pipeline requires vertex and fragment shaders");
  Pipeline pipeline{};
  pipeline.m_device = device.GetHandle();
  pipeline.CreateLayout(desc.layout);
  const std::array stages{GetStage(VK_SHADER_STAGE_VERTEX_BIT, desc.vertex_shader),
                          GetStage(VK_SHADER_STAGE_FRAGMENT_BIT, desc.fragment_shader)};
  VkPipelineVertexInputStateCreateInfo vertex{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .vertexBindingDescriptionCount = static_cast<uint32_t>(desc.vertex_bindings.size()),
    .pVertexBindingDescriptions = desc.vertex_bindings.data(),
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.vertex_attributes.size()),
    .pVertexAttributeDescriptions = desc.vertex_attributes.data(),
  };
  VkPipelineInputAssemblyStateCreateInfo assembly{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .topology = desc.topology,
    .primitiveRestartEnable = {},
  };
  VkPipelineViewportStateCreateInfo viewport{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
  };
  VkPipelineRasterizationStateCreateInfo raster{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .depthClampEnable = {},
    .rasterizerDiscardEnable = {},
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = desc.cull_mode,
    .frontFace = desc.front_face,
    .depthBiasEnable = {},
    .depthBiasConstantFactor = {},
    .depthBiasClamp = {},
    .depthBiasSlopeFactor = {},
    .lineWidth = 1.0F,
  };
  VkPipelineMultisampleStateCreateInfo multisample{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .rasterizationSamples = desc.samples,
    .sampleShadingEnable = {},
    .minSampleShading = {},
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = {},
    .alphaToOneEnable = {},
  };
  VkPipelineDepthStencilStateCreateInfo depth{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .depthTestEnable = desc.depth_format != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE,
    .depthWriteEnable = {},
    .depthCompareOp = {},
    .depthBoundsTestEnable = {},
    .stencilTestEnable = {},
    .front = {},
    .back = {},
    .minDepthBounds = {},
    .maxDepthBounds = {},
  };
  depth.depthWriteEnable = depth.depthTestEnable == VK_TRUE && desc.enable_depth_write ? VK_TRUE : VK_FALSE;
  depth.depthCompareOp = desc.depth_compare;
  std::vector<VkPipelineColorBlendAttachmentState> attachments(desc.color_formats.size());
  for (auto& attachment : attachments) {
    attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    attachment.blendEnable = desc.enable_alpha_blending ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo blend{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .logicOpEnable = {},
    .logicOp = {},
    .attachmentCount = static_cast<uint32_t>(attachments.size()),
    .pAttachments = attachments.data(),
    .blendConstants = {},
  };
  // 尺寸由录制时的动态状态决定，窗口尺寸变化不要求重新创建管线。
  const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
    .pDynamicStates = dynamic_states.data(),
  };
  // 动态渲染通过附件格式声明兼容性，无需绑定 RenderPass 对象。
  VkPipelineRenderingCreateInfo rendering{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .pNext = nullptr,
    .viewMask = {},
    .colorAttachmentCount = static_cast<uint32_t>(desc.color_formats.size()),
    .pColorAttachmentFormats = desc.color_formats.data(),
    .depthAttachmentFormat = desc.depth_format,
    .stencilAttachmentFormat = {},
  };
  VkGraphicsPipelineCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = &rendering,
    .flags = {},
    .stageCount = static_cast<uint32_t>(stages.size()),
    .pStages = stages.data(),
    .pVertexInputState = &vertex,
    .pInputAssemblyState = &assembly,
    .pTessellationState = nullptr,
    .pViewportState = &viewport,
    .pRasterizationState = &raster,
    .pMultisampleState = &multisample,
    .pDepthStencilState = &depth,
    .pColorBlendState = &blend,
    .pDynamicState = &dynamic,
    .layout = pipeline.m_layout,
    .renderPass = {},
    .subpass = {},
    .basePipelineHandle = {},
    .basePipelineIndex = {},
  };
  detail::Check(vkCreateGraphicsPipelines(pipeline.m_device, desc.cache, 1, &info, nullptr, &pipeline.m_pipeline),
                "Create graphics pipeline");
  return pipeline;
}

Pipeline Pipeline::CreateCompute(Device& device, const ComputePipelineDesc& desc) {
  detail::Require(desc.shader != VK_NULL_HANDLE, "Compute pipeline requires a shader");
  Pipeline pipeline{};
  pipeline.m_device = device.GetHandle();
  pipeline.CreateLayout(desc.layout);
  VkComputePipelineCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .stage = GetStage(VK_SHADER_STAGE_COMPUTE_BIT, desc.shader),
    .layout = pipeline.m_layout,
    .basePipelineHandle = {},
    .basePipelineIndex = {},
  };
  detail::Check(vkCreateComputePipelines(pipeline.m_device, desc.cache, 1, &info, nullptr, &pipeline.m_pipeline),
                "Create compute pipeline");
  return pipeline;
}
Pipeline::~Pipeline() noexcept { Release(); }
Pipeline::Pipeline(Pipeline&& other) noexcept { *this = std::move(other); }
Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
    m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
  }
  return *this;
}
void Pipeline::Release() noexcept {
  if (m_pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
  }
  if (m_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(m_device, m_layout, nullptr);
  }
  m_pipeline = VK_NULL_HANDLE;
  m_layout = VK_NULL_HANDLE;
}
VkPipeline Pipeline::GetHandle() const noexcept { return m_pipeline; }
VkPipelineLayout Pipeline::GetLayout() const noexcept { return m_layout; }
}  // namespace fw
