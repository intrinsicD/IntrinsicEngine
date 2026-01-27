module;

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Forward;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import Core;
import ECS;
import RHI;

export namespace Graphics::Passes
{
    class ForwardPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool, RHI::DescriptorLayout&) override
        {
            m_Device = &device;
            m_DescriptorPool = &descriptorPool;
        }

        void SetPipeline(RHI::GraphicsPipeline* p) { m_Pipeline = p; }
        void SetCullPipeline(RHI::ComputePipeline* p) { m_CullPipeline = p; }

        void AddPasses(RenderPassContext& ctx) override;

        // Stage 1: provided by PipelineLibrary (must match pipeline layout set=2).
        void SetInstanceSetLayout(VkDescriptorSetLayout layout) { m_InstanceSetLayout = layout; }
        void SetCullSetLayout(VkDescriptorSetLayout layout) { m_CullSetLayout = layout; }

        void SetEnableGpuCulling(bool enable) { m_EnableGpuCulling = enable; }

    private:
        struct PassData
        {
            RGResourceHandle Color;
            RGResourceHandle Depth;
        };

        struct RenderPacket
        {
            Geometry::GeometryHandle GeoHandle;
            uint32_t TextureID = 0;
            glm::mat4 Transform{1.0f};
            bool IsSelected = false;

            bool operator<(const RenderPacket& other) const
            {
                if (GeoHandle != other.GeoHandle) return GeoHandle < other.GeoHandle;
                if (TextureID != other.TextureID) return TextureID < other.TextureID;
                return IsSelected < other.IsSelected;
            }
        };

        RHI::VulkanDevice* m_Device = nullptr; // non-owning
        RHI::DescriptorAllocator* m_DescriptorPool = nullptr; // non-owning
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // owned by RenderSystem for now
        RHI::ComputePipeline* m_CullPipeline = nullptr; // owned by PipelineLibrary

        // Stage 1: SSBO pull-model.
        static constexpr uint32_t FRAMES = 2;
        std::unique_ptr<RHI::VulkanBuffer> m_InstanceBuffer[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_VisibilityBuffer[FRAMES];

        // Stage 3: compute culling.
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::PersistentDescriptorPool> m_CullSetPool;

        // Per-frame buffers for compute culling.
        std::unique_ptr<RHI::VulkanBuffer> m_BoundsBuffer[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_DrawCountBuffer[FRAMES];

        // Indirect buffer becomes GPU-written (compute) and consumed by draw indirect.
        std::unique_ptr<RHI::VulkanBuffer> m_IndirectIndexedBuffer[FRAMES];

        // Stage 1: allocate per-frame descriptor sets freshly (do not cache across frames).
        VkDescriptorSetLayout m_InstanceSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::PersistentDescriptorPool> m_InstanceSetPool;

        bool m_EnableGpuCulling = true;
    };
}
