module;

#include <memory>
#include <span>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.PostProcess.Histogram;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Graphics.Passes.PostProcessSettings;
import RHI.Buffer;
import RHI.ComputePipeline;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // HistogramSubPass — compute shader luminance histogram.
    //
    // Reads SceneColorHDR, writes a 256-bin SSBO for CPU readback.
    // -----------------------------------------------------------------
    class HistogramSubPass
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        VkSampler linearSampler,
                        VkImageView dummyView);

        void AddPass(RenderPassContext& ctx,
                     RGResourceHandle sceneColor,
                     const PostProcessSettings& settings);

        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages,
                         VkSampler linearSampler,
                         RGResourceHandle sceneColorHandle);

        void Shutdown();
        void OnResize();

        void SetShaderRegistry(const ShaderRegistry* reg) { m_ShaderRegistry = reg; }
        [[nodiscard]] bool IsPipelineBuilt() const { return static_cast<bool>(m_Pipeline); }
        [[nodiscard]] const HistogramReadback& GetHistogram() const { return m_Readback; }

    private:
        RHI::VulkanDevice*    m_Device         = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_Sets[3]   = {};
        std::unique_ptr<RHI::ComputePipeline> m_Pipeline;
        std::unique_ptr<RHI::VulkanBuffer>    m_Buffers[3]; // per-frame SSBO
        HistogramReadback m_Readback;

        std::unique_ptr<RHI::ComputePipeline> BuildPipeline();
    };
}
