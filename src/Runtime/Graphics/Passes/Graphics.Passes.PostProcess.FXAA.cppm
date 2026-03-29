module;

#include <memory>
#include <span>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.PostProcess.FXAA;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Graphics.Passes.PostProcessSettings;
import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // FXAASubPass — single-pass fast approximate anti-aliasing.
    //
    // Reads PostLdrTemp (tone-mapped LDR), writes SceneColorLDR.
    // -----------------------------------------------------------------
    class FXAASubPass
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        VkSampler linearSampler,
                        VkImageView dummyView);

        void AddPass(RenderPassContext& ctx,
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
        [[nodiscard]] bool IsPipelineBuilt() const { return static_cast<bool>(m_Pipeline); }

    private:
        RHI::VulkanDevice*    m_Device         = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_Sets[3]   = {};
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(VkFormat outputFormat);
    };
}
