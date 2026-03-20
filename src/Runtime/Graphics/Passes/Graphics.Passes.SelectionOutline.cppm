module;

#include <array>
#include <memory>
#include <span>
#include <vector>
#include <entt/entity/registry.hpp>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.SelectionOutline;

import Graphics.Passes.SelectionOutlineSettings;
import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Core.Hash;
import ECS;
import RHI;

export namespace Graphics::Passes
{

    [[nodiscard]] uint32_t AppendOutlineRenderablePickIds(const entt::registry& registry,
                                                          entt::entity root,
                                                          std::span<uint32_t> outIds,
                                                          uint32_t count = 0u);

    [[nodiscard]] uint32_t ResolveOutlineRenderablePickId(const entt::registry& registry,
                                                          entt::entity root);

    class SelectionOutlinePass final : public IRenderFeature
    {
    public:
        static constexpr uint32_t kMaxSelectedIds = kSelectionOutlineDebugMaxSelectedIds;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout&) override;

        void SetShaderRegistry(const ShaderRegistry& shaderRegistry)
        {
            m_ShaderRegistry = &shaderRegistry;
            m_DebugState.ShaderRegistryConfigured = true;
        }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;
        void OnResize(uint32_t width, uint32_t height) override;

        // Must be called after RenderGraph::Compile() to update per-frame descriptor
        // bindings to point at the actual PickID image view for this frame.
        void PostCompile(uint32_t frameIndex, std::span<const RenderGraphDebugImage> debugImages);

        // Configuration accessors
        SelectionOutlineSettings& GetSettings() { return m_Settings; }
        [[nodiscard]] const SelectionOutlineSettings& GetSettings() const { return m_Settings; }
        [[nodiscard]] const SelectionOutlineDebugState& GetDebugState() const { return m_DebugState; }

    private:
        struct OutlinePassData
        {
            RGResourceHandle EntityId;
            RGResourceHandle Target;
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

        // User-configurable settings
        SelectionOutlineSettings m_Settings;
        SelectionOutlineDebugState m_DebugState;
    };
}
