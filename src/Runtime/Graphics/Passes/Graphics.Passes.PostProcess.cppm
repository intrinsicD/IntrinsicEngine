module;

#include <memory>
#include <span>
#include <array>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.PostProcess;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Graphics.Passes.PostProcessSettings;
import Graphics.Passes.PostProcess.Bloom;
import Graphics.Passes.PostProcess.ToneMap;
import Graphics.Passes.PostProcess.FXAA;
import Graphics.Passes.PostProcess.SMAA;
import Graphics.Passes.PostProcess.Histogram;
import RHI.Buffer;
import RHI.ComputePipeline;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.Pipeline;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // PostProcessPass — orchestrator for post-processing sub-passes.
    //
    // Delegates to BloomSubPass, ToneMapSubPass, FXAASubPass,
    // SMAASubPass, and HistogramSubPass. Each sub-pass owns its own
    // pipelines, descriptor sets, and PostCompile logic.
    //
    // Reads canonical `SceneColorHDR` from the blackboard.
    // Writes canonical `SceneColorLDR` for later overlays and final presentation.
    // -----------------------------------------------------------------
    class PostProcessPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout&) override;

        void SetShaderRegistry(const ShaderRegistry& shaderRegistry);

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;
        void OnResize(uint32_t width, uint32_t height) override;

        // Called after RenderGraph::Compile() to update descriptor bindings.
        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages);

        [[nodiscard]] PostProcessSettings& GetSettings() { return m_Settings; }
        [[nodiscard]] const PostProcessSettings& GetSettings() const { return m_Settings; }
        [[nodiscard]] const PostProcessDebugState& GetDebugState() const { return m_DebugState; }

        // Access CPU-side histogram readback for UI display.
        [[nodiscard]] const HistogramReadback& GetHistogram() const { return m_Histogram.GetHistogram(); }

    private:
        RHI::VulkanDevice*   m_Device          = nullptr;
        const ShaderRegistry* m_ShaderRegistry  = nullptr;

        PostProcessSettings m_Settings;
        PostProcessDebugState m_DebugState;

        // Shared resources.
        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        std::unique_ptr<RHI::VulkanImage> m_DummySampled;

        // Sub-passes.
        BloomSubPass     m_Bloom;
        ToneMapSubPass   m_ToneMap;
        FXAASubPass      m_FXAA;
        SMAASubPass      m_SMAA;
        HistogramSubPass m_Histogram;

        // Cached handles for PostCompile coordination.
        RGResourceHandle m_LastSceneColorHandle{};
        RGResourceHandle m_LastPostLdrHandle{};
    };
}
