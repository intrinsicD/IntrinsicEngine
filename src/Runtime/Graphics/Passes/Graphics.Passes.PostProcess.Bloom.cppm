module;

#include <memory>
#include <span>
#include <array>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.PostProcess.Bloom;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Graphics.Passes.PostProcessSettings;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.Pipeline;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // BloomSubPass — HDR bloom extraction via progressive mip chain.
    //
    // Downsample: SceneColorHDR -> BloomMip0 -> ... -> BloomMipN
    // Upsample:   BloomMipN -> ... -> BloomMip0 (additive accumulation)
    // -----------------------------------------------------------------
    class BloomSubPass
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        VkSampler linearSampler,
                        VkImageView dummyView);

        void AddPasses(RenderPassContext& ctx,
                       RGResourceHandle sceneColor,
                       const PostProcessSettings& settings);

        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages,
                         VkSampler linearSampler);

        void Shutdown();
        void OnResize();

        void SetShaderRegistry(const ShaderRegistry* reg) { m_ShaderRegistry = reg; }

        [[nodiscard]] RGResourceHandle GetBloomResult() const { return m_LastBloomMip0Handle; }

        // Debug state accessors.
        [[nodiscard]] bool IsDownPipelineBuilt() const { return static_cast<bool>(m_DownPipeline); }
        [[nodiscard]] bool IsUpPipelineBuilt() const { return static_cast<bool>(m_UpPipeline); }
        [[nodiscard]] const std::array<RGResourceHandle, kBloomMipCount>& GetLastDownHandles() const { return m_LastDownHandles; }
        [[nodiscard]] const std::array<RGResourceHandle, kBloomMipCount>& GetLastUpSrcHandles() const { return m_LastUpSrcHandles; }

    private:
        RHI::VulkanDevice*    m_Device         = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        VkDescriptorSetLayout m_DownSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_DownSets[3][kBloomMipCount] = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_DownPipeline;

        VkDescriptorSetLayout m_UpSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_UpSets[3][kBloomMipCount] = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_UpPipeline;

        RGResourceHandle m_LastBloomMip0Handle{};
        std::array<RGResourceHandle, kBloomMipCount> m_LastDownHandles{};
        std::array<RGResourceHandle, kBloomMipCount> m_LastUpSrcHandles{};
        std::array<RGResourceHandle, kBloomMipCount> m_LastMipWriteHandles{};

        std::unique_ptr<RHI::GraphicsPipeline> BuildDownsamplePipeline();
        std::unique_ptr<RHI::GraphicsPipeline> BuildUpsamplePipeline();
    };
}
