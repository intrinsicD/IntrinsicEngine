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
                        RHI::DescriptorAllocator&, RHI::DescriptorLayout&) override
        {
            m_Device = &device;
        }

        void SetPipeline(RHI::GraphicsPipeline* p) { m_Pipeline = p; }

        void AddPasses(RenderPassContext& ctx) override;

    private:
        struct PassData
        {
            RGResourceHandle Color;
            RGResourceHandle Depth;
        };

        struct RenderPacket
        {
            GeometryHandle GeoHandle;
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
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // owned by RenderSystem for now
    };
}
