module;

#include <memory>
#include <span>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.PostProcess.ToneMap;

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
    // ToneMapSubPass — HDR → LDR tone mapping with optional color grading.
    //
    // Reads SceneColorHDR + optional bloom result.
    // Writes either SceneColorLDR (no AA) or PostLdrTemp (AA enabled).
    // -----------------------------------------------------------------
    class ToneMapSubPass
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        VkSampler linearSampler,
                        VkImageView dummyView);

        /// Add the tone map pass writing to \p outputTarget.
        /// Returns the RG handle of the tone-mapped output (for downstream AA).
        RGResourceHandle AddPass(RenderPassContext& ctx,
                                 RGResourceHandle sceneColor,
                                 RGResourceHandle bloomResult,
                                 RGResourceHandle outputTarget,
                                 const PostProcessSettings& settings);

        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages,
                         VkSampler linearSampler,
                         VkImageView dummyView);

        void Shutdown();
        void OnResize();

        void SetShaderRegistry(const ShaderRegistry* reg) { m_ShaderRegistry = reg; }

        [[nodiscard]] bool IsPipelineBuilt() const { return static_cast<bool>(m_Pipeline); }
        [[nodiscard]] RGResourceHandle GetLastSceneColorHandle() const { return m_LastSceneColorHandle; }
        [[nodiscard]] RGResourceHandle GetLastOutputHandle() const { return m_LastOutputHandle; }

    private:
        RHI::VulkanDevice*    m_Device         = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_Sets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        RGResourceHandle m_LastSceneColorHandle{};
        RGResourceHandle m_LastBloomHandle{};
        RGResourceHandle m_LastOutputHandle{};

        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(VkFormat outputFormat);
    };
}
