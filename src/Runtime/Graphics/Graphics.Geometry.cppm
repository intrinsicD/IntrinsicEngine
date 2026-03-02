// src/Runtime/Graphics/Graphics.Geometry.cppm
module;
#include <vector>
#include <span>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <queue>
#include <expected>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics:Geometry;

import RHI;
import Geometry;
import Core.ResourcePool;

export namespace Graphics
{
    // --- Data Structures ---

    enum class PrimitiveTopology
    {
        Triangles,
        Lines,
        Points
    };

    enum class GeometryUploadMode
    {
        Staged,
        Direct
    };

    struct GeometryUploadRequest
    {
        std::span<const glm::vec3> Positions;
        std::span<const glm::vec3> Normals;
        std::span<const glm::vec4> Aux; // UVs packed in xy, Color/Data in zw
        std::span<const uint32_t> Indices;

        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
        GeometryUploadMode UploadMode = GeometryUploadMode::Staged;

        // If valid, reuse the vertex buffer (and its layout) from an existing geometry.
        // When set, Positions/Normals/Aux are ignored. Indices are still uploaded for this view.
        Geometry::GeometryHandle ReuseVertexBuffersFrom{};
    };

    struct GeometryCpuData
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        std::vector<glm::vec4> Aux;
        std::vector<uint32_t> Indices;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;

        [[nodiscard]] GeometryUploadRequest ToUploadRequest(GeometryUploadMode mode = GeometryUploadMode::Staged) const
        {
            return {Positions, Normals, Aux, Indices, Topology, mode, {}};
        }
    };

    struct GeometryCollisionData
    {
        Geometry::AABB LocalAABB;
        Geometry::Octree LocalOctree; // Static octree of mesh vertices

        // Optional: Keep positions for precise ray intersection tests after Octree broadphase
        std::vector<glm::vec3> Positions;
        std::vector<uint32_t> Indices;
    };

    struct GeometryBufferLayout
    {
        VkDeviceSize PositionsOffset = 0;
        VkDeviceSize PositionsSize = 0;

        VkDeviceSize NormalsOffset = 0;
        VkDeviceSize NormalsSize = 0;

        VkDeviceSize AuxOffset = 0;
        VkDeviceSize AuxSize = 0;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    };

    // Use the core StrongHandle template for type safety and consistency.
    // RetirementFrames=3 matches VulkanDevice::MAX_FRAMES_IN_FLIGHT — a removed slot is safe to
    // reclaim once 3 frames have elapsed, guaranteeing the GPU is no longer referencing the buffer.
    using GeometryPool = Core::ResourcePool<class GeometryGpuData, Geometry::GeometryHandle, 3>;

    // --- The GPU Resource ---

    class GeometryGpuData
    {
    public:
        GeometryGpuData() = default;

        ~GeometryGpuData() = default;

        [[nodiscard]] static std::pair<std::unique_ptr<GeometryGpuData>, RHI::TransferToken>
        CreateAsync(std::shared_ptr<RHI::VulkanDevice> device,
                    RHI::TransferManager& transferManager,
                    const GeometryUploadRequest& data,
                    const GeometryPool* existingPool = nullptr);

        [[nodiscard]] RHI::VulkanBuffer* GetVertexBuffer() const { return m_VertexBuffer.get(); }
        [[nodiscard]] RHI::VulkanBuffer* GetIndexBuffer() const { return m_IndexBuffer.get(); }
        [[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }
        [[nodiscard]] const GeometryBufferLayout& GetLayout() const { return m_Layout; }
        [[nodiscard]] PrimitiveTopology GetTopology() const { return m_Layout.Topology; }

        // Local-space bounding sphere: (center.xyz, radius).
        // Computed from CPU vertex positions at upload time.
        // radius > 0 = valid bounds; radius <= 0 = empty/unknown geometry.
        [[nodiscard]] glm::vec4 GetLocalBoundingSphere() const { return m_LocalBoundingSphere; }

    private:
        // Shared ownership enables multiple GeometryGpuData instances (views) to reference the same heavy buffers.
        std::shared_ptr<RHI::VulkanBuffer> m_VertexBuffer;
        std::shared_ptr<RHI::VulkanBuffer> m_IndexBuffer;

        GeometryBufferLayout m_Layout{};
        uint32_t m_IndexCount = 0;
        glm::vec4 m_LocalBoundingSphere{0.0f, 0.0f, 0.0f, 0.0f};
    };

}
