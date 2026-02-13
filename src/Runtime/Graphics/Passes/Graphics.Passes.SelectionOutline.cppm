module;

#include <memory>
#include <span>
#include <vector>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.SelectionOutline;

import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core.Hash;
import ECS;
import RHI;

export namespace Graphics::Passes
{
    class SelectionOutlinePass final : public IRenderFeature
    {
    public:
        static constexpr uint32_t kMaxSelectedIds = 16;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout&) override;

        void SetShaderRegistry(const ShaderRegistry& shaderRegistry) { m_ShaderRegistry = &shaderRegistry; }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;

        // Must be called after RenderGraph::Compile() to update per-frame descriptor
        // bindings to point at the actual PickID image view for this frame.
        void PostCompile(uint32_t frameIndex, std::span<const RenderGraphDebugImage> debugImages);

    private:
        struct OutlinePassData
        {
            RGResourceHandle PickID;
            RGResourceHandle Backbuffer;
        };

        RHI::VulkanDevice* m_Device = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;
        std::unique_ptr<RHI::VulkanImage> m_DummyPickId;

        // Cached handle from AddPasses for PostCompile descriptor update.
        RGResourceHandle m_LastPickIdHandle{};
    };
}
