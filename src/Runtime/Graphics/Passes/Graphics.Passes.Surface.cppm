module;

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics.Passes.Surface;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Components;
import Geometry.Handle;
import ECS;
import RHI;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // Surface Pass Constants
    // -----------------------------------------------------------------
    namespace SurfacePassConstants
    {
        // Compute shader workgroup size — must match instance_cull*.comp local_size_x.
        constexpr uint32_t kCullWorkgroupSize = 64;

        // Descriptor pool sizes for per-frame instance descriptor sets.
        constexpr uint32_t kInstancePoolMaxSets = 256;
        constexpr uint32_t kInstancePoolStorageBuffers = 512;

        // Descriptor pool sizes for compute culling descriptor sets.
        constexpr uint32_t kCullPoolMaxSets = 64;
        constexpr uint32_t kCullPoolStorageBuffers = kCullPoolMaxSets * 7;
    }

    class SurfacePass final : public IRenderFeature
    {
    public:
        SurfacePass() = default;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool, RHI::DescriptorLayout&) override
        {
            m_Device = &device;
            m_DescriptorPool = &descriptorPool;
        }

        void SetPipeline(RHI::GraphicsPipeline* p) { m_Pipeline = p; }
        void SetLinePipeline(RHI::GraphicsPipeline* p) { m_LinePipeline = p; }
        void SetPointPipeline(RHI::GraphicsPipeline* p) { m_PointPipeline = p; }
        void SetCullPipeline(RHI::ComputePipeline* p) { m_CullPipeline = p; }
        void SetGBufferPipeline(RHI::GraphicsPipeline* p) { m_GBufferPipeline = p; }

        void AddPasses(RenderPassContext& ctx) override;

        // Stage 1: provided by PipelineLibrary (must match pipeline layout set=2).
        void SetInstanceSetLayout(VkDescriptorSetLayout layout) { m_InstanceSetLayout = layout; }
        void SetCullSetLayout(VkDescriptorSetLayout layout) { m_CullSetLayout = layout; }

        void SetEnableGpuCulling(bool enable) { m_EnableGpuCulling = enable; }

        // -----------------------------------------------------------------
        // Transient triangle submission API (three-pass architecture)
        // -----------------------------------------------------------------
        // GPU-aligned triangle vertex: 32 bytes.
        struct alignas(16) TransientVertex
        {
            glm::vec3 Position;
            uint32_t Color;   // packed ABGR
            glm::vec3 Normal;
            float _pad;
        };
        static_assert(sizeof(TransientVertex) == 32, "TransientVertex must be 32 bytes for GPU SSBO alignment");

        // Submit a single triangle for transient (per-frame) rendering.
        // Transient triangles are rendered with the same pipeline as retained
        // surface geometry but from a host-visible SSBO that is rebuilt each frame.
        void SubmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                            const glm::vec3& normal, uint32_t color);

        // Reset transient triangle buffer. Called at the start of each frame
        // before any SubmitTriangle() calls.
        void ResetTransient();

        // Query transient triangle data for GPU upload.
        [[nodiscard]] std::span<const TransientVertex> GetTransientVertices() const
        {
            return {m_TransientVertices.data(), m_TransientVertices.size()};
        }
        [[nodiscard]] uint32_t GetTransientTriangleCount() const
        {
            return static_cast<uint32_t>(m_TransientVertices.size() / 3);
        }
        [[nodiscard]] bool HasTransientContent() const { return !m_TransientVertices.empty(); }

        // Set the rendered point size (px) for a specific geometry handle index.
        // Grows the table on demand; called by point-cloud sync/lifecycle owners.
        void SetPointSize(uint32_t handleIndex, float sizePx)
        {
            if (handleIndex >= m_PointSizePxByHandleIndex.size())
                m_PointSizePxByHandleIndex.resize(handleIndex + 1, 0.0f);
            m_PointSizePxByHandleIndex[handleIndex] = sizePx;
        }

    private:
        struct PassData
        {
            RGResourceHandle Color{};
            RGResourceHandle Depth{};
        };

        struct GBufferPassData
        {
            RGResourceHandle Normal{};
            RGResourceHandle Albedo{};
            RGResourceHandle Material{};
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
        RHI::GraphicsPipeline* m_LinePipeline = nullptr; // owned by PipelineLibrary
        RHI::GraphicsPipeline* m_PointPipeline = nullptr; // owned by PipelineLibrary
        RHI::ComputePipeline* m_CullPipeline = nullptr; // owned by PipelineLibrary
        RHI::GraphicsPipeline* m_GBufferPipeline = nullptr; // owned by PipelineLibrary

        // Stage 1: SSBO pull-model.
        // CRITICAL: must match VulkanDevice::MAX_FRAMES_IN_FLIGHT (3) exactly.
        // Previously this was 2, which caused frame slot 2 to alias slot 0 while
        // slot 0 was still in flight — the root cause of all descriptor/buffer hazards.
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();
        std::unique_ptr<RHI::VulkanBuffer> m_InstanceBuffer[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_Stage1VisibilityBuffer[FRAMES];

        // Stage 3: visibility/remap (GPU-written) lives in m_VisibilityBuffer[].
        std::unique_ptr<RHI::VulkanBuffer> m_VisibilityBuffer[FRAMES];

        // Cache the instance descriptor set per frame (updated in place and reused across submissions).
        VkDescriptorSet m_InstanceSet[FRAMES] = {};

        // Stage 3: compute culling.
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::PersistentDescriptorPool> m_CullSetPool;
        VkDescriptorSet m_CullSet[FRAMES] = {};

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

        // Geometry routing table: maps GeometryHandle.Index -> dense geometry id for this frame.
        // Resized per-frame via needsResize() — no shared capacity scalar (see packed buffers pattern).
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3HandleToDense[FRAMES];

        // Packed outputs (device-local): [GeometryCount * MaxDrawsPerGeometry]
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3VisibilityPacked[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3IndirectPacked[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_Stage3DrawCountsPacked[FRAMES];

        // Per-frame-slot tracking: each slot independently records the geometry count and
        // maxDrawsPerGeometry it was built with so that reallocation is triggered on any change,
        // not only on growth. Using a single shared value caused stale-buffer flicker when
        // loading additional meshes (new slot allocated at new size, other slots not updated).
        uint32_t m_Stage3LastGeometryCount[FRAMES]        = {};
        uint32_t m_Stage3LastMaxDrawsPerGeometry[FRAMES]  = {};

        // Stage 1: persistent descriptor sets are cached per frame slot and rewritten in place.
        VkDescriptorSetLayout m_InstanceSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::PersistentDescriptorPool> m_InstanceSetPool;

        bool m_EnableGpuCulling = true;

        // Transient triangle vertex buffer (rebuilt each frame).
        std::vector<TransientVertex> m_TransientVertices;

        // ---------------------------------------------------------------------
        // Unified draw-stream contract (long-term error-resistant design)
        // ---------------------------------------------------------------------
        struct DrawBatch
        {
            Geometry::GeometryHandle GeoHandle{};
            VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            // For VK_PRIMITIVE_TOPOLOGY_POINT_LIST batches: pixel-sized points.
            // Ignored for triangles/lines.
            float PointSizePx = 1.0f;

            VkBuffer IndexBuffer = VK_NULL_HANDLE;
            uint32_t IndexCount = 0;

            // Vertex buffer device addresses (SoA).
            uint64_t PtrPositions = 0;
            uint64_t PtrNormals = 0;
            uint64_t PtrAux = 0;

            // Per-face attribute buffer device address (0 = standard shading).
            uint64_t PtrFaceAttr = 0;

            // Per-vertex attribute buffer device address (0 = no per-vertex colors).
            uint64_t PtrVertexAttr = 0;

            // Packed-slice offsets (bytes) into Indirect buffers.
            VkDeviceSize IndirectOffsetBytes = 0;
            VkDeviceSize CountOffsetBytes = 0;

            // Base index into VisibleRemap[] for this geometry batch (passed via push constants).
            uint32_t VisibilityBase = 0;

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
        void AddRasterPass(RenderPassContext& ctx, RGResourceHandle sceneColor, RGResourceHandle depth, DrawStream&& stream);

        // Record a G-buffer raster pass (deferred path) that writes to MRT targets.
        void AddGBufferRasterPass(RenderPassContext& ctx,
                                  RGResourceHandle normal, RGResourceHandle albedo,
                                  RGResourceHandle material, RGResourceHandle depth,
                                  DrawStream&& stream);

        // Legacy helpers (will be folded into BuildDrawStream/AddRasterPass).
        void AddStage1And2Passes(RenderPassContext& ctx, RGResourceHandle backbuffer, RGResourceHandle depth);
        void AddStage3Passes(RenderPassContext& ctx, RGResourceHandle backbuffer, RGResourceHandle depth,
                             Geometry::GeometryHandle singleGeometry);

        // -----------------------------------------------------------------
        // Per-entity retained attribute buffers — persistent BDA-addressable
        // GPU buffers keyed by GeometryHandle::Index. Created lazily when an
        // entity with cached colors is encountered during draw stream build.
        // -----------------------------------------------------------------
        struct RetainedBufferEntry
        {
            std::unique_ptr<RHI::VulkanBuffer> Buffer;
            uint32_t Count = 0;
        };
        std::unordered_map<uint32_t, RetainedBufferEntry> m_FaceAttrBuffers;

        // Sparse table: GeometryHandle::Index → point size in pixels.
        // Grown on demand by SetPointSize(); default 0.0f means "not set" (falls back to 4.0f).
        std::vector<float> m_PointSizePxByHandleIndex;

        // Create or update a persistent per-face attribute buffer for a geometry.
        // Data is an array of packed ABGR uint32_t, one per face.
        // Returns the BDA device address, or 0 on failure.
        uint64_t EnsureFaceAttrBuffer(uint32_t geoIndex,
                                      const uint32_t* colorData,
                                      uint32_t faceCount);

        // Per-vertex attribute buffers — same pattern as face attr, same entry type.
        std::unordered_map<uint32_t, RetainedBufferEntry> m_VertexAttrBuffers;

        // Create or update a persistent per-vertex attribute buffer for a geometry.
        // Data is an array of packed ABGR uint32_t, one per vertex.
        // Returns the BDA device address, or 0 on failure.
        uint64_t EnsureVertexAttrBuffer(uint32_t geoIndex,
                                        const uint32_t* colorData,
                                        uint32_t vertexCount);
    };
}
