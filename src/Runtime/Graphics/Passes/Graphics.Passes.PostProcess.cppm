module;

#include <memory>
#include <span>
#include <array>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics:Passes.PostProcess;

import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import :Passes.PostProcessSettings;
import RHI;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // PostProcessPass — Bloom + HDR tone mapping + optional FXAA.
    //
    // Reads canonical `SceneColorHDR` from the blackboard.
    // Writes canonical `SceneColorLDR` for later overlays and final presentation.
    //
    // Bloom chain (when enabled):
    //   Downsample: SceneColorHDR -> BloomMip0 -> ... -> BloomMipN
    //   Upsample:   BloomMipN -> ... -> BloomMip0 (additive accumulation)
    //
    // When FXAA is enabled:
    //   ToneMap: SceneColorHDR + BloomMip0 -> PostLdrTemp
    //   FXAA:    PostLdrTemp -> SceneColorLDR
    //
    // When FXAA is disabled:
    //   ToneMap: SceneColorHDR + BloomMip0 -> SceneColorLDR
    // -----------------------------------------------------------------

    static constexpr uint32_t kBloomMipCount = 5;

    class PostProcessPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout&) override;

        void SetShaderRegistry(const ShaderRegistry& shaderRegistry)
        {
            m_ShaderRegistry = &shaderRegistry;
        }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;
        void OnResize(uint32_t width, uint32_t height) override;

        // Called after RenderGraph::Compile() to update descriptor bindings.
        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages);

        PostProcessSettings& GetSettings() { return m_Settings; }
        const PostProcessSettings& GetSettings() const { return m_Settings; }

        // Access CPU-side histogram readback for UI display.
        [[nodiscard]] const HistogramReadback& GetHistogram() const { return m_HistogramReadback; }

    private:
        RHI::VulkanDevice*   m_Device          = nullptr;
        const ShaderRegistry* m_ShaderRegistry  = nullptr;

        PostProcessSettings m_Settings;

        // Tone map pipeline + descriptors (2 bindings: scene color + bloom)
        VkDescriptorSetLayout m_ToneMapSetLayout = VK_NULL_HANDLE;
        VkSampler             m_LinearSampler    = VK_NULL_HANDLE;
        VkDescriptorSet       m_ToneMapSets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_ToneMapPipeline;

        // FXAA pipeline + descriptors
        VkDescriptorSetLayout m_FXAASetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_FXAASets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_FXAAPipeline;

        // Bloom downsample pipeline + descriptors
        VkDescriptorSetLayout m_BloomDownSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_BloomDownSets[3][kBloomMipCount] = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_BloomDownPipeline;

        // Bloom upsample pipeline + descriptors
        VkDescriptorSetLayout m_BloomUpSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_BloomUpSets[3][kBloomMipCount] = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_BloomUpPipeline;

        // Safe default binding used until PostCompile patches in the frame's actual image views.
        std::unique_ptr<RHI::VulkanImage> m_DummySampled;

        // Cached resource handles for PostCompile descriptor update.
        RGResourceHandle m_LastSceneColorHandle{};
        RGResourceHandle m_LastPostLdrHandle{};
        RGResourceHandle m_LastBloomMip0Handle{};
        std::array<RGResourceHandle, kBloomMipCount> m_LastBloomDownHandles{};
        std::array<RGResourceHandle, kBloomMipCount> m_LastBloomUpSrcHandles{};

        // Histogram compute pipeline + storage
        VkDescriptorSetLayout m_HistogramSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_HistogramSets[3]   = {};
        std::unique_ptr<RHI::ComputePipeline> m_HistogramPipeline;
        std::unique_ptr<RHI::VulkanBuffer>    m_HistogramBuffers[3]; // per-frame SSBO
        HistogramReadback m_HistogramReadback;

        std::unique_ptr<RHI::GraphicsPipeline> BuildToneMapPipeline(VkFormat outputFormat);
        std::unique_ptr<RHI::GraphicsPipeline> BuildFXAAPipeline(VkFormat outputFormat);
        std::unique_ptr<RHI::GraphicsPipeline> BuildBloomDownsamplePipeline();
        std::unique_ptr<RHI::GraphicsPipeline> BuildBloomUpsamplePipeline();
        std::unique_ptr<RHI::ComputePipeline>  BuildHistogramPipeline();

        void AddBloomPasses(RenderPassContext& ctx, RGResourceHandle sceneColor);
        void AddHistogramPass(RenderPassContext& ctx, RGResourceHandle sceneColor);
    };
}
