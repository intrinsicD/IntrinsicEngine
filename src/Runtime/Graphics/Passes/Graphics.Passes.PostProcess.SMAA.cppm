module;

#include <memory>
#include <span>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.PostProcess.SMAA;

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
    // SMAASubPass — 3-pass subpixel morphological anti-aliasing.
    //
    // Pass 1: Edge detection    (PostLdrTemp -> SMAAEdges)
    // Pass 2: Blend weight      (SMAAEdges + AreaTex + SearchTex -> SMAAWeights)
    // Pass 3: Resolve           (PostLdrTemp + SMAAWeights -> SceneColorLDR)
    // -----------------------------------------------------------------
    class SMAASubPass
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        VkSampler linearSampler,
                        VkImageView dummyView);

        void AddPasses(RenderPassContext& ctx,
                       RGResourceHandle postLdr,
                       RGResourceHandle sceneColorLdr,
                       const PostProcessSettings& settings);

        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages,
                         VkSampler linearSampler,
                         RGResourceHandle postLdrHandle);

        void Shutdown();
        void OnResize();

        void SetShaderRegistry(const ShaderRegistry* reg) { m_ShaderRegistry = reg; }
        [[nodiscard]] bool IsEdgePipelineBuilt() const { return static_cast<bool>(m_EdgePipeline); }
        [[nodiscard]] bool IsBlendPipelineBuilt() const { return static_cast<bool>(m_BlendPipeline); }
        [[nodiscard]] bool IsResolvePipelineBuilt() const { return static_cast<bool>(m_ResolvePipeline); }
        [[nodiscard]] RGResourceHandle GetLastEdgesHandle() const { return m_LastEdgesHandle; }
        [[nodiscard]] RGResourceHandle GetLastWeightsHandle() const { return m_LastWeightsHandle; }

    private:
        RHI::VulkanDevice*    m_Device         = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        // Edge detection
        VkDescriptorSetLayout m_EdgeSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_EdgeSets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_EdgePipeline;

        // Blend weight
        VkDescriptorSetLayout m_BlendSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_BlendSets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_BlendPipeline;

        // Neighborhood blending
        VkDescriptorSetLayout m_ResolveSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_ResolveSets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_ResolvePipeline;

        // Lookup textures (persistent)
        std::unique_ptr<RHI::VulkanImage> m_AreaTex;
        std::unique_ptr<RHI::VulkanImage> m_SearchTex;

        RGResourceHandle m_LastEdgesHandle{};
        RGResourceHandle m_LastWeightsHandle{};

        void InitializeLookupTextures(VkSampler linearSampler);
        std::unique_ptr<RHI::GraphicsPipeline> BuildEdgePipeline(VkFormat edgeFormat);
        std::unique_ptr<RHI::GraphicsPipeline> BuildBlendPipeline(VkFormat weightFormat);
        std::unique_ptr<RHI::GraphicsPipeline> BuildResolvePipeline(VkFormat outputFormat);
    };
}
