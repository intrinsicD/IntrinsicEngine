module;

#include <cstdint>
#include <memory>
#include <vector>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.PointCloud;

import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import :GpuColor;
import Core.Hash;
import RHI;
import Geometry;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // PointCloudRenderPass — GPU point cloud rendering with multi-mode splatting.
    // -------------------------------------------------------------------------
    //
    // Architecture:
    // - Collects point cloud data from ECS entities with PointCloudRenderer::Component.
    // - Uploads point data to a host-visible SSBO each frame (consolidated across entities).
    // - Vertex shader expands each point into a screen-space billboard quad (6 verts).
    // - Fragment shader renders circular/elliptical splats with anti-aliasing.
    //
    // Rendering Modes (selectable via push constants):
    //   0 = Flat Disc     — screen-aligned billboard, constant pixel radius.
    //   1 = Surfel         — oriented disc aligned to surface normal with lighting.
    //   2 = EWA Splatting  — perspective-correct Gaussian elliptical splats (Zwicker et al. 2001).
    //
    // Integration:
    // - Registered in DefaultPipeline after Forward, before SelectionOutline.
    // - Gated by FeatureRegistry ("PointCloudRenderPass"_id).
    // - Reads camera UBO from global descriptor set (set 0).
    // - Point SSBO bound at set 1.
    //
    // GPU data layout: 32 bytes per point (2 x vec4):
    //   struct PointData {
    //       vec3  Position;    // 12 bytes
    //       float Size;        //  4 bytes (world-space radius)
    //       vec3  Normal;      // 12 bytes
    //       uint  Color;       //  4 bytes (packed RGBA8)
    //   };

    class PointCloudRenderPass final : public IRenderFeature
    {
    public:
        // GPU-aligned point data: 32 bytes (2 x vec4).
        struct alignas(16) GpuPointData
        {
            float PosX, PosY, PosZ;
            float Size;          // world-space radius
            float NormX, NormY, NormZ;
            uint32_t Color;      // packed ABGR (Vulkan: R in low bits)
        };
        static_assert(sizeof(GpuPointData) == 32, "GpuPointData must be 32 bytes for GPU SSBO alignment");

        // Configuration
        float SizeMultiplier = 1.0f;   // Global point size multiplier.
        Geometry::PointCloud::RenderMode RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout) override;

        void SetShaderRegistry(const ShaderRegistry& reg) { m_ShaderRegistry = &reg; }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;

        // ---- Point Cloud Data Submission ----
        // Call before AddPasses() each frame.

        // Append points to the frame buffer. Caller transforms to world space.
        // Uses the pass-global RenderMode.
        void SubmitPoints(const GpuPointData* data, uint32_t count);

        // Append points with an explicit render mode (allows batching by mode).
        void SubmitPoints(Geometry::PointCloud::RenderMode mode, const GpuPointData* data, uint32_t count);

        // Reset all per-frame point accumulators.
        void ResetPoints()
        {
            m_StagingPoints.clear();
            m_StagingPointsByMode[0].clear();
            m_StagingPointsByMode[1].clear();
            m_StagingPointsByMode[2].clear();
        }

        // True if any mode has content.
        [[nodiscard]] bool HasContent() const
        {
            return !m_StagingPoints.empty() ||
                   !m_StagingPointsByMode[0].empty() ||
                   !m_StagingPointsByMode[1].empty() ||
                   !m_StagingPointsByMode[2].empty();
        }

        // Total number of accumulated points across all modes.
        [[nodiscard]] uint32_t GetPointCount() const
        {
            return static_cast<uint32_t>(m_StagingPoints.size() +
                                         m_StagingPointsByMode[0].size() +
                                         m_StagingPointsByMode[1].size() +
                                         m_StagingPointsByMode[2].size());
        }

        // Convenience: pack a single point from components.
        static GpuPointData PackPoint(float x, float y, float z,
                                       float nx, float ny, float nz,
                                       float size, uint32_t color);

        // Color packing (delegates to GpuColor — same convention as DebugDraw).
        static constexpr uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept
        {
            return GpuColor::PackColor(r, g, b, a);
        }

        static constexpr uint32_t PackColorF(float r, float g, float b, float a = 1.0f) noexcept
        {
            return GpuColor::PackColorF(r, g, b, a);
        }

    private:
        struct PointCloudPassData
        {
            RGResourceHandle Color;
            RGResourceHandle Depth;
        };

        RHI::VulkanDevice* m_Device = nullptr;
        RHI::DescriptorAllocator* m_DescriptorPool = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        // Descriptor set layout for point SSBO (set 1, binding 0).
        VkDescriptorSetLayout m_PointSetLayout = VK_NULL_HANDLE;

        // Global camera layout (set 0) — borrowed.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Per-frame descriptor sets.
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();
        VkDescriptorSet m_PointDescSets[FRAMES] = {};

        // Per-frame host-visible SSBOs.
        std::unique_ptr<RHI::VulkanBuffer> m_PointBuffers[FRAMES];
        uint32_t m_BufferCapacity = 0; // in points

        // Lazily-built pipeline (with depth test).
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        // CPU-side staging buffer — accumulated per frame.
        std::vector<GpuPointData> m_StagingPoints;

        // New: per-mode staging (0..2). Used when callers want different modes in the same frame.
        std::vector<GpuPointData> m_StagingPointsByMode[3];

        // Ensure SSBO has capacity for the given point count.
        bool EnsureBuffer(uint32_t requiredPoints);

        // Build the graphics pipeline.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat);

        // Record draw commands.
        void RecordDraw(VkCommandBuffer cmd, VkDescriptorSet pointSet,
                        VkDescriptorSet globalSet, uint32_t dynamicOffset,
                        VkExtent2D extent, uint32_t pointCount);
    };
}
