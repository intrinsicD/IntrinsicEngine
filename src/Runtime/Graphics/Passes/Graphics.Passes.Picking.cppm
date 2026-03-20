module;

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include "RHI.Vulkan.hpp"

export module Graphics.Passes.Picking;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Components;
import ECS;
import RHI.Buffer;
import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;

export namespace Graphics::Passes
{
    class PickingPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator&, RHI::DescriptorLayout&) override
        {
            m_Device = &device;
        }

        void SetPipeline(RHI::GraphicsPipeline* p) { m_Pipeline = p; }
        void SetMeshPickPipeline(RHI::GraphicsPipeline* p) { m_MeshPickPipeline = p; }
        void SetLinePickPipeline(RHI::GraphicsPipeline* p) { m_LinePickPipeline = p; }
        void SetPointPickPipeline(RHI::GraphicsPipeline* p) { m_PointPickPipeline = p; }

        void AddPasses(RenderPassContext& ctx) override;

    private:
        struct FaceIdBufferEntry
        {
            std::unique_ptr<RHI::VulkanBuffer> Buffer;
            uint32_t Count = 0;
        };

        struct PickPassData
        {
            RGResourceHandle IdBuffer;
            RGResourceHandle PrimIdBuffer;
            RGResourceHandle Depth;
        };

        struct PickCopyPassData
        {
            RGResourceHandle IdBuffer;
            RGResourceHandle PrimIdBuffer;
        };

        uint64_t EnsureFaceIdBuffer(uint32_t geoIndex,
                                    const uint32_t* faceIds,
                                    uint32_t triangleCount);

        RHI::VulkanDevice* m_Device = nullptr; // non-owning
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // legacy single-output (fallback)
        RHI::GraphicsPipeline* m_MeshPickPipeline = nullptr;
        RHI::GraphicsPipeline* m_LinePickPipeline = nullptr;
        RHI::GraphicsPipeline* m_PointPickPipeline = nullptr;
        std::unordered_map<uint32_t, FaceIdBufferEntry> m_FaceIdBuffers;
    };
}
