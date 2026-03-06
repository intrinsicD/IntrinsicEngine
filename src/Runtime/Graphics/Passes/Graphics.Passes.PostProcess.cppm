module;

#include <memory>
#include <span>
#include "RHI.Vulkan.hpp"

export module Graphics:Passes.PostProcess;

import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import RHI;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // Tone-mapping operator selection (push constant enum).
    // -----------------------------------------------------------------
    enum class ToneMapOperator : int
    {
        ACES     = 0,
        Reinhard = 1,
    };

    // -----------------------------------------------------------------
    // Post-processing settings exposed to the editor UI.
    // -----------------------------------------------------------------
    struct PostProcessSettings
    {
        // Tone mapping
        float            Exposure     = 1.0f;
        ToneMapOperator  ToneOperator = ToneMapOperator::ACES;

        // FXAA
        bool  FXAAEnabled          = true;
        float FXAAContrastThreshold  = 0.0312f;
        float FXAARelativeThreshold  = 0.063f;
        float FXAASubpixelBlending   = 0.75f;
    };

    // -----------------------------------------------------------------
    // PostProcessPass — HDR tone mapping + optional FXAA.
    //
    // Reads canonical `SceneColorHDR` from the blackboard.
    // Writes canonical `SceneColorLDR` for later overlays and final presentation.
    //
    // When FXAA is enabled:
    //   ToneMap: SceneColorHDR -> PostLdrTemp (transient, swapchain format)
    //   FXAA:    PostLdrTemp   -> SceneColorLDR
    //
    // When FXAA is disabled:
    //   ToneMap: SceneColorHDR -> SceneColorLDR
    // -----------------------------------------------------------------
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

    private:
        RHI::VulkanDevice*   m_Device          = nullptr;
        const ShaderRegistry* m_ShaderRegistry  = nullptr;

        PostProcessSettings m_Settings;

        // Tone map pipeline + descriptors
        VkDescriptorSetLayout m_ToneMapSetLayout = VK_NULL_HANDLE;
        VkSampler             m_LinearSampler    = VK_NULL_HANDLE;
        VkDescriptorSet       m_ToneMapSets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_ToneMapPipeline;

        // FXAA pipeline + descriptors
        VkDescriptorSetLayout m_FXAASetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_FXAASets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_FXAAPipeline;

        // Safe default binding used until PostCompile patches in the frame's actual image views.
        std::unique_ptr<RHI::VulkanImage> m_DummySampled;

        // Cached resource handles for PostCompile descriptor update.
        RGResourceHandle m_LastSceneColorHandle{};
        RGResourceHandle m_LastPostLdrHandle{};

        std::unique_ptr<RHI::GraphicsPipeline> BuildToneMapPipeline(VkFormat outputFormat);
        std::unique_ptr<RHI::GraphicsPipeline> BuildFXAAPipeline(VkFormat outputFormat);
    };
}
