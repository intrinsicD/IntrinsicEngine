module;
#include "RHI.Vulkan.hpp"
#include <memory>
#include <vector>
#include <expected>

export module RHI:ComputePipeline;

import :Device;
import :Shader;

export namespace RHI
{
    class ComputePipeline
    {
    public:
        ComputePipeline(std::shared_ptr<VulkanDevice> device, VkPipeline pipeline, VkPipelineLayout layout)
            : m_Device(std::move(device)), m_Pipeline(pipeline), m_Layout(layout)
        {
        }

        ~ComputePipeline();

        [[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
        [[nodiscard]] VkPipelineLayout GetLayout() const { return m_Layout; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    };

    class ComputePipelineBuilder
    {
    public:
        explicit ComputePipelineBuilder(std::shared_ptr<VulkanDevice> device);

        ComputePipelineBuilder& SetShader(ShaderModule* comp);
        ComputePipelineBuilder& AddDescriptorSetLayout(VkDescriptorSetLayout layout);
        ComputePipelineBuilder& AddPushConstantRange(VkPushConstantRange range);

        [[nodiscard]] std::expected<std::unique_ptr<ComputePipeline>, VkResult> Build();

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkPipelineShaderStageCreateInfo m_ShaderStage{};
        std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;
        std::vector<VkPushConstantRange> m_PushConstants;
    };
}
