module;
#include "RHI.Vulkan.hpp"

export module Runtime.RHI.Pipeline;

import Runtime.RHI.Device;
import Runtime.RHI.Shader;
import Runtime.RHI.Swapchain; // Needed to know color formats

export namespace Runtime::RHI {

    struct PipelineConfig {
        ShaderModule* VertexShader = nullptr;
        ShaderModule* FragmentShader = nullptr;
    };

    class GraphicsPipeline {
    public:
        GraphicsPipeline(VulkanDevice& device, const VulkanSwapchain& swapchain, const PipelineConfig& config, VkDescriptorSetLayout descriptorSetLayout);
        ~GraphicsPipeline();

        [[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
        [[nodiscard]] VkPipelineLayout GetLayout() const { return m_Layout; }

    private:
        VulkanDevice& m_Device;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_Layout = VK_NULL_HANDLE;

        void CreateLayout(VkDescriptorSetLayout descriptorLayout);
        void CreatePipeline(const VulkanSwapchain& swapchain, const PipelineConfig& config);
    };
}