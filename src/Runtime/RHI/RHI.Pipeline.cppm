module;
#include "RHI.Vulkan.hpp"
#include <memory>
#include <vector>

export module Runtime.RHI.Pipeline;

import Runtime.RHI.Device;
import Runtime.RHI.Shader;
import Runtime.RHI.Swapchain; // Needed to know color formats

export namespace Runtime::RHI
{
    struct PipelineConfig
    {
        ShaderModule* VertexShader = nullptr;
        ShaderModule* FragmentShader = nullptr;

        std::vector<VkVertexInputBindingDescription> BindingDescriptions;
        std::vector<VkVertexInputAttributeDescription> AttributeDescriptions;

        VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool DepthTest = true;
        bool DepthWrite = true;
    };

    class GraphicsPipeline
    {
    public:
        GraphicsPipeline(std::shared_ptr<VulkanDevice> device,
                         const VulkanSwapchain& swapchain,
                         const PipelineConfig& config,
                         const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
        ~GraphicsPipeline();

        [[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
        [[nodiscard]] VkPipelineLayout GetLayout() const { return m_Layout; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_Layout = VK_NULL_HANDLE;

        void CreateLayout(const std::vector<VkDescriptorSetLayout> &descriptorLayouts);
        void CreatePipeline(const VulkanSwapchain& swapchain, const PipelineConfig& config);
    };
}
