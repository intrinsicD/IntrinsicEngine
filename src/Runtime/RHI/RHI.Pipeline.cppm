module;
#include "RHI.Vulkan.hpp"
#include <memory>
#include <vector>
#include <expected>

export module RHI:Pipeline;

import :Device;
import :Shader;
import :Types; // For VertexInputDescription

export namespace RHI
{
    // The resulting resource wrapper
    class GraphicsPipeline
    {
    public:
        // Takes ownership of the pipeline handle
        GraphicsPipeline(std::shared_ptr<VulkanDevice> device, VkPipeline pipeline, VkPipelineLayout layout)
            : m_Device(device), m_Pipeline(pipeline), m_Layout(layout)
        {
        }

        ~GraphicsPipeline(); // Destroys pipeline and layout

        [[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
        [[nodiscard]] VkPipelineLayout GetLayout() const { return m_Layout; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    };

    // The Builder
    class PipelineBuilder
    {
    public:
        PipelineBuilder(std::shared_ptr<VulkanDevice> device);

        PipelineBuilder& SetShaders(ShaderModule* vert, ShaderModule* frag);
        PipelineBuilder& SetInputLayout(const VertexInputDescription& layout);
        PipelineBuilder& SetTopology(VkPrimitiveTopology topology);
        PipelineBuilder& SetPolygonMode(VkPolygonMode mode);
        PipelineBuilder& SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
        PipelineBuilder& SetMultisampling(VkSampleCountFlagBits samples);
        PipelineBuilder& EnableDepthTest(bool depthWrite, VkCompareOp op = VK_COMPARE_OP_LESS);
        PipelineBuilder& DisableDepthTest();

        // Critical: Dynamic Rendering Formats
        PipelineBuilder& SetColorFormats(const std::vector<VkFormat>& formats);
        PipelineBuilder& SetDepthFormat(VkFormat format);

        // Sets (Layouts)
        PipelineBuilder& AddDescriptorSetLayout(VkDescriptorSetLayout layout);
        PipelineBuilder& AddPushConstantRange(VkPushConstantRange range);

        [[nodiscard]] std::expected<std::unique_ptr<GraphicsPipeline>, VkResult> Build();

    private:
        std::shared_ptr<VulkanDevice> m_Device;

        std::vector<VkPipelineShaderStageCreateInfo> m_ShaderStages;
        VertexInputDescription m_VertexInput;
        VkPipelineInputAssemblyStateCreateInfo m_InputAssembly;
        VkPipelineViewportStateCreateInfo m_ViewportState;
        VkPipelineRasterizationStateCreateInfo m_Rasterizer;
        VkPipelineMultisampleStateCreateInfo m_Multisampling;
        VkPipelineColorBlendAttachmentState m_ColorBlendAttachment;
        VkPipelineDepthStencilStateCreateInfo m_DepthStencil;
        VkPipelineRenderingCreateInfo m_RenderingInfo;

        std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;
        std::vector<VkPushConstantRange> m_PushConstants;

        std::vector<VkFormat> m_ColorFormats; // Temp storage to keep pointer valid
    };
}
