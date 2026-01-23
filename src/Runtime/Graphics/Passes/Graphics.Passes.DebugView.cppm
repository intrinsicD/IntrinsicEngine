module;

#include <memory>
#include <vector>
#include <span>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.DebugView;

import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core;
import Interface;
import RHI;

export namespace Graphics::Passes
{
    class DebugViewPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout&) override;

        void SetShaderRegistry(const ShaderRegistry& shaderRegistry) { m_ShaderRegistry = &shaderRegistry; }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;

        void OnResize(uint32_t width, uint32_t height) override;

        // Must be called after RenderGraph::Compile(). Updates per-frame descriptor set
        // bindings to point at the selected image view for this frame.
        void PostCompile(uint32_t frameIndex, std::span<const RenderGraphDebugImage> debugImages);

        // Expose the per-frame descriptor set used by the debug-view full-screen pass.
        [[nodiscard]] VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const;

        [[nodiscard]] VkSampler GetSampler() const { return m_Sampler; }

        // ImGui preview texture id (owned by this feature).
        [[nodiscard]] void* GetImGuiTextureId(uint32_t frameIndex) const;

    private:
        struct ResolveData
        {
            RGResourceHandle Src;
            RGResourceHandle Dst;
            VkFormat SrcFormat = VK_FORMAT_UNDEFINED;
            bool IsDepth = false;
        };

        RHI::VulkanDevice* m_Device = nullptr; // non-owning
        const ShaderRegistry* m_ShaderRegistry = nullptr; // non-owning
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        std::unique_ptr<RHI::VulkanImage> m_DummyFloat;
        std::unique_ptr<RHI::VulkanImage> m_DummyUint;
        std::unique_ptr<RHI::VulkanImage> m_DummyDepth;

        // Per-frame preview RGBA image (owned here rather than RenderSystem)
        std::vector<std::unique_ptr<RHI::VulkanImage>> m_PreviewImages;
        std::vector<void*> m_ImGuiTextureIds;

        // Cached handle from AddPasses used for PostCompile descriptor update.
        RGResourceHandle m_LastSrcHandle{};
    };
}
