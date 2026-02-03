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
        ForwardPass() = default;

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
            RGResourceHandle Color{};
            RGResourceHandle Depth{};
        };

        struct RenderPacket
        {
            Geometry::GeometryHandle GeoHandle{};
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
        std::unique_ptr<RHI::VulkanBuffer> m_Stage1VisibilityBuffer[FRAMES];

        // Stage 3: visibility/remap (GPU-written) lives in m_VisibilityBuffer[].
        std::unique_ptr<RHI::VulkanBuffer> m_VisibilityBuffer[FRAMES];

        // Cache the instance descriptor set per frame (updated each frame).
        VkDescriptorSet m_InstanceSet[FRAMES] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

        // Stage 3: compute culling.
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::PersistentDescriptorPool> m_CullSetPool;

        // Per-frame buffers for compute culling.
        std::unique_ptr<RHI::VulkanBuffer> m_BoundsBuffer[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_DrawCountBuffer[FRAMES];

        // Indirect buffer becomes GPU-written (compute) and consumed by draw indirect.
        // NOTE: Stage 2 builds indirect on the CPU (host-visible), Stage 3 builds indirect on the GPU (device-local).
        // Keep them split to avoid aliasing a CPU Write() into GPU-only memory.
        std::unique_ptr<RHI::VulkanBuffer> m_Stage2IndirectIndexedBuffer[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3IndirectIndexedBuffer[FRAMES];

        // Option A (multi-geometry Stage 3): per-frame geometry table (dense) and packed output streams.
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3GeometryIndexCount[FRAMES];

        // Packed outputs (device-local): [GeometryCount * MaxDrawsPerGeometry]
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3VisibilityPacked[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3IndirectPacked[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3DrawCountsPacked[FRAMES];

        uint32_t m_Stage3LastGeometryCount = 0;
        uint32_t m_Stage3LastMaxDrawsPerGeometry = 0;

        // Stage 1: allocate per-frame descriptor sets freshly (do not cache across frames).
        VkDescriptorSetLayout m_InstanceSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::PersistentDescriptorPool> m_InstanceSetPool;

        bool m_EnableGpuCulling = true;

        // ---------------------------------------------------------------------
        // Unified draw-stream contract (long-term error-resistant design)
        // ---------------------------------------------------------------------
        struct DrawBatch
        {
            Geometry::GeometryHandle GeoHandle{};
            VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkBuffer IndexBuffer = VK_NULL_HANDLE;
            uint32_t IndexCount = 0;

            // Vertex buffer device addresses (SoA).
            uint64_t PtrPositions = 0;
            uint64_t PtrNormals = 0;
            uint64_t PtrAux = 0;

            // Packed-slice offsets (bytes) into Indirect/Visibility buffers.
            VkDeviceSize IndirectOffsetBytes = 0;
            VkDeviceSize VisibilityOffsetBytes = 0;
            VkDeviceSize CountOffsetBytes = 0;

            // Per-batch indirect buffers. If CountBuffer is VK_NULL_HANDLE, fall back to fixed drawCount.
            RHI::VulkanBuffer* IndirectBuffer = nullptr; // non-owning
            RHI::VulkanBuffer* CountBuffer = nullptr;    // optional (non-owning)
            uint32_t MaxDraws = 0;

            // Visibility/remap buffer: maps gl_InstanceIndex -> instance slot.
            RHI::VulkanBuffer* VisibilityBuffer = nullptr; // non-owning

            // Instance buffer for the vertex shader (set=2,binding=0).
            RHI::VulkanBuffer* InstanceBuffer = nullptr; // non-owning
        };

        struct DrawStream
        {
            // Invariant: if Batches is empty, raster pass is skipped.
            std::vector<DrawBatch> Batches{};
        };

        // Build either a CPU (Stage1/2) or GPU-driven (Stage3) draw stream.
        [[nodiscard]] DrawStream BuildDrawStream(RenderPassContext& ctx);

        // Record a single raster pass that consumes the draw stream exactly once.
        void AddRasterPass(RenderPassContext& ctx, RGResourceHandle backbuffer, RGResourceHandle depth, DrawStream&& stream);

        // Legacy helpers (will be folded into BuildDrawStream/AddRasterPass).
        void AddStage1And2Passes(RenderPassContext& ctx, RGResourceHandle backbuffer, RGResourceHandle depth);
        void AddStage3Passes(RenderPassContext& ctx, RGResourceHandle backbuffer, RGResourceHandle depth,
                             Geometry::GeometryHandle singleGeometry);
    };
}
