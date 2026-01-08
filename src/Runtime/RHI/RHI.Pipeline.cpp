module;
#include "RHI.Vulkan.hpp"
#include <vector>
#include <memory>
#include <expected>

module RHI:Pipeline.Impl;
import :Pipeline;
import Core;

namespace RHI
{
    GraphicsPipeline::~GraphicsPipeline()
    {
        if (m_Pipeline) vkDestroyPipeline(m_Device->GetLogicalDevice(), m_Pipeline, nullptr);
        if (m_Layout) vkDestroyPipelineLayout(m_Device->GetLogicalDevice(), m_Layout, nullptr);
    }

    PipelineBuilder::PipelineBuilder(std::shared_ptr<VulkanDevice> device) : m_Device(device)
    {
        // Initialize defaults (Standard Opaque)
        m_InputAssembly = {};
        m_InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        m_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        m_Rasterizer = {};
        m_Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        m_Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        m_Rasterizer.lineWidth = 1.0f;
        m_Rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        m_Rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        m_Multisampling = {};
        m_Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        m_Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        m_ColorBlendAttachment = {};
        m_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        m_ColorBlendAttachment.blendEnable = VK_FALSE;

        m_DepthStencil = {};
        m_DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        EnableDepthTest(true);

        m_RenderingInfo = {};
        m_RenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        // Dummy Viewport (Dynamic State will override)
        m_ViewportState = {};
        m_ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        m_ViewportState.viewportCount = 1;
        m_ViewportState.scissorCount = 1;
    }

    PipelineBuilder& PipelineBuilder::SetShaders(ShaderModule* vert, ShaderModule* frag)
    {
        m_ShaderStages.clear();
        if (vert) m_ShaderStages.push_back(vert->GetStageInfo());
        if (frag) m_ShaderStages.push_back(frag->GetStageInfo());
        return *this;
    }

    PipelineBuilder& PipelineBuilder::SetInputLayout(const VertexInputDescription& layout)
    {
        m_VertexInput = layout;
        return *this;
    }

    // ... Implement other setters simply updating the struct members ...

    PipelineBuilder& PipelineBuilder::EnableDepthTest(bool depthWrite, VkCompareOp op)
    {
        m_DepthStencil.depthTestEnable = VK_TRUE;
        m_DepthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        m_DepthStencil.depthCompareOp = op;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::SetColorFormats(const std::vector<VkFormat>& formats)
    {
        m_ColorFormats = formats; // Copy to member to keep valid pointer
        m_RenderingInfo.colorAttachmentCount = (uint32_t)m_ColorFormats.size();
        m_RenderingInfo.pColorAttachmentFormats = m_ColorFormats.data();
        return *this;
    }

    PipelineBuilder& PipelineBuilder::SetDepthFormat(VkFormat format)
    {
        m_RenderingInfo.depthAttachmentFormat = format;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::AddDescriptorSetLayout(VkDescriptorSetLayout layout)
    {
        m_DescriptorSetLayouts.push_back(layout);
        return *this;
    }

    PipelineBuilder& PipelineBuilder::AddPushConstantRange(VkPushConstantRange range)
    {
        m_PushConstants.push_back(range);
        return *this;
    }

    std::expected<std::unique_ptr<GraphicsPipeline>, VkResult> PipelineBuilder::Build()
    {
        // 1. Pipeline Layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = (uint32_t)m_DescriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = m_DescriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = (uint32_t)m_PushConstants.size();
        pipelineLayoutInfo.pPushConstantRanges = m_PushConstants.data();

        VkPipelineLayout pipelineLayout;
        VkResult res = vkCreatePipelineLayout(m_Device->GetLogicalDevice(), &pipelineLayoutInfo, nullptr,
                                              &pipelineLayout);
        if (res != VK_SUCCESS) return std::unexpected(res);

        // 2. Vertex Input State
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = (uint32_t)m_VertexInput.Bindings.size();
        vertexInputInfo.pVertexBindingDescriptions = m_VertexInput.Bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)m_VertexInput.Attributes.size();
        vertexInputInfo.pVertexAttributeDescriptions = m_VertexInput.Attributes.data();

        // 3. Dynamic State
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY
        };
        VkPipelineDynamicStateCreateInfo dynamicInfo{};
        dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicInfo.dynamicStateCount = (uint32_t)dynamicStates.size();
        dynamicInfo.pDynamicStates = dynamicStates.data();

        // 4. Color Blending (Assumes 1 attachment for simplicity, expand if needed)
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &m_ColorBlendAttachment;

        // 5. Create Info
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &m_RenderingInfo;
        pipelineInfo.stageCount = (uint32_t)m_ShaderStages.size();
        pipelineInfo.pStages = m_ShaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &m_InputAssembly;
        pipelineInfo.pViewportState = &m_ViewportState;
        pipelineInfo.pRasterizationState = &m_Rasterizer;
        pipelineInfo.pMultisampleState = &m_Multisampling;
        pipelineInfo.pDepthStencilState = &m_DepthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicInfo;
        pipelineInfo.layout = pipelineLayout;

        VkPipeline pipeline;
        res = vkCreateGraphicsPipelines(m_Device->GetLogicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                        &pipeline);

        if (res != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(m_Device->GetLogicalDevice(), pipelineLayout, nullptr);
            return std::unexpected(res);
        }

        return std::make_unique<GraphicsPipeline>(m_Device, pipeline, pipelineLayout);
    }
}
