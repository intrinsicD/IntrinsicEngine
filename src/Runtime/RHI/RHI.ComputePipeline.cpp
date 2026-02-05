module;
#include <memory>
#include <expected>
#include "RHI.Vulkan.hpp"

module RHI:ComputePipeline.Impl;

import :ComputePipeline;

namespace RHI
{
    ComputePipeline::~ComputePipeline()
    {
        if (!m_Device) return;

        VkDevice logicalDevice = m_Device->GetLogicalDevice();
        if (m_Pipeline)
        {
            VkPipeline pipeline = m_Pipeline;
            m_Device->SafeDestroy([logicalDevice, pipeline]()
            {
                vkDestroyPipeline(logicalDevice, pipeline, nullptr);
            });
            m_Pipeline = VK_NULL_HANDLE;
        }

        if (m_Layout)
        {
            VkPipelineLayout layout = m_Layout;
            m_Device->SafeDestroy([logicalDevice, layout]()
            {
                vkDestroyPipelineLayout(logicalDevice, layout, nullptr);
            });
            m_Layout = VK_NULL_HANDLE;
        }
    }

    ComputePipelineBuilder::ComputePipelineBuilder(std::shared_ptr<VulkanDevice> device)
        : m_Device(std::move(device))
    {
        m_ShaderStage = {};
        m_ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        m_ShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    ComputePipelineBuilder& ComputePipelineBuilder::SetShader(ShaderModule* comp)
    {
        if (comp)
            m_ShaderStage = comp->GetStageInfo();
        return *this;
    }

    ComputePipelineBuilder& ComputePipelineBuilder::AddDescriptorSetLayout(VkDescriptorSetLayout layout)
    {
        m_DescriptorSetLayouts.push_back(layout);
        return *this;
    }

    ComputePipelineBuilder& ComputePipelineBuilder::AddPushConstantRange(VkPushConstantRange range)
    {
        m_PushConstants.push_back(range);
        return *this;
    }

    std::expected<std::unique_ptr<ComputePipeline>, VkResult> ComputePipelineBuilder::Build()
    {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = (uint32_t)m_DescriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = m_DescriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = (uint32_t)m_PushConstants.size();
        pipelineLayoutInfo.pPushConstantRanges = m_PushConstants.data();

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkResult res = vkCreatePipelineLayout(m_Device->GetLogicalDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout);
        if (res != VK_SUCCESS) return std::unexpected(res);

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = m_ShaderStage;
        pipelineInfo.layout = pipelineLayout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        res = vkCreateComputePipelines(m_Device->GetLogicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (res != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(m_Device->GetLogicalDevice(), pipelineLayout, nullptr);
            return std::unexpected(res);
        }

        return std::make_unique<ComputePipeline>(m_Device, pipeline, pipelineLayout);
    }
}
