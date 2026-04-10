// src/Runtime/Graphics/Graphics.Geometry.cppm
module;
#include <vector>
#include <span>
#include <memory>
#include <optional>
#include <utility>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics.Geometry;

import RHI.Buffer;
import RHI.Device;
import RHI.Transfer;
import Geometry.AABB;
import Geometry.Handle;
import Geometry.HalfedgeMesh;
import Geometry.KDTree;
import Geometry.Octree;
import Geometry.Properties;
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
        std::span<const glm::vec3> Positions{};
        std::span<const glm::vec3> Normals{};
        std::span<const glm::vec4> Aux{}; // UVs packed in xy, Color/Data in zw
        std::span<const uint32_t> Indices{};

        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
        GeometryUploadMode UploadMode = GeometryUploadMode::Staged;

        // If valid, reuse the vertex buffer (and its layout) from an existing geometry.
        // When set, Positions/Normals/Aux are ignored. Indices are still uploaded for this view.
        Geometry::GeometryHandle ReuseVertexBuffersFrom{};
    };

    // -------------------------------------------------------------------------
    // GeometryCpuData — I/O boundary type (loader output / exporter input).
    //
    // PropertySet-backed per-vertex data.  All per-vertex attributes live in
    // VertexProperties under canonical keys:
    //
    //   "v:position"  (glm::vec3) — mandatory vertex positions
    //   "v:normal"    (glm::vec3) — surface / point normals
    //   "v:attr"      (glm::vec4) — opaque attribute slot:
    //                               mesh importers store UV in .xy
    //                               point-cloud importers store RGBA color
    //
    // Loaders write to Positions()/Normals()/Attrs() mutable accessors.
    // Exporters and upload code read from the const span accessors.
    // Arbitrary additional per-vertex data can be stored in VertexProperties
    // directly under user-defined keys.
    //
    //   Import:  bytes → GeometryCpuData → Halfedge::Mesh → PopulateFromMesh
    //               → ECS::Components::GeometrySources   ← runtime authority
    //   Export:  GeometrySources → GeometryCpuData → IAssetExporter → bytes
    //
    // DO NOT use this struct as a runtime data store.
    // -------------------------------------------------------------------------
    struct GeometryCpuData
    {
        // All per-vertex attributes — use the accessors below for common slots.
        Geometry::PropertySet VertexProperties{};

        // Index buffer and primitive type (no PropertySet concept for these).
        std::vector<uint32_t> Indices{};
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;

        // ---- Mutable accessors (for importers / generators) -----------------
        // Returns a reference to the internal vector, creating the property on
        // first access.  Use these when building data during import.

        [[nodiscard]] std::vector<glm::vec3>& Positions()
        { return VertexProperties.GetOrAdd<glm::vec3>("v:position", {}).Vector(); }

        [[nodiscard]] std::vector<glm::vec3>& Normals()
        { return VertexProperties.GetOrAdd<glm::vec3>("v:normal", {}).Vector(); }

        [[nodiscard]] std::vector<glm::vec4>& Attrs()
        { return VertexProperties.GetOrAdd<glm::vec4>("v:attr", {}).Vector(); }

        // ---- Const span accessors (for exporters / GPU upload) --------------
        // Returns an empty span when the property is absent.

        [[nodiscard]] std::span<const glm::vec3> Positions() const noexcept
        {
            auto p = VertexProperties.Get<glm::vec3>("v:position");
            return p ? p.Span() : std::span<const glm::vec3>{};
        }
        [[nodiscard]] std::span<const glm::vec3> Normals() const noexcept
        {
            auto p = VertexProperties.Get<glm::vec3>("v:normal");
            return p ? p.Span() : std::span<const glm::vec3>{};
        }
        [[nodiscard]] std::span<const glm::vec4> Attrs() const noexcept
        {
            auto p = VertexProperties.Get<glm::vec4>("v:attr");
            return p ? p.Span() : std::span<const glm::vec4>{};
        }

        // ---- GPU upload helper ----------------------------------------------
        [[nodiscard]] GeometryUploadRequest ToUploadRequest(GeometryUploadMode mode = GeometryUploadMode::Staged) const
        {
            return {Positions(), Normals(), Attrs(), Indices, Topology, mode, {}};
        }
    };

    struct GeometryCollisionData
    {
        Geometry::AABB LocalAABB{};
        Geometry::Octree LocalOctree{}; // Static octree of triangle primitive bounds

        // Local-space nearest-vertex lookup for picker refinement.
        // Element i in LocalVertexLookupPoints maps to authoritative vertex index
        // LocalVertexLookupIndices[i]. When SourceMesh is available these are
        // Halfedge::VertexHandle::Index values; otherwise they fall back to raw
        // Positions[] indices.
        Geometry::KDTree LocalVertexKdTree{};
        std::vector<glm::vec3> LocalVertexLookupPoints{};
        std::vector<uint32_t> LocalVertexLookupIndices{};

        // Optional: Keep CPU-side collision geometry and an authoritative editable mesh.
        std::vector<glm::vec3> Positions{};
        std::vector<glm::vec4> Aux{};
        std::vector<uint32_t> Indices{};
        std::shared_ptr<Geometry::Halfedge::Mesh> SourceMesh{};
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

        GeometryGpuData(const GeometryGpuData&) = delete;
        GeometryGpuData& operator=(const GeometryGpuData&) = delete;
        GeometryGpuData(GeometryGpuData&&) noexcept = default;
        GeometryGpuData& operator=(GeometryGpuData&&) noexcept = default;

        [[nodiscard]] static std::pair<std::optional<GeometryGpuData>, RHI::TransferToken>
        CreateAsync(std::shared_ptr<RHI::VulkanDevice> device,
                    RHI::TransferManager& transferManager,
                    RHI::BufferManager& bufferManager,
                    const GeometryUploadRequest& data,
                    const GeometryPool* existingPool = nullptr);

        [[nodiscard]] RHI::VulkanBuffer* GetVertexBuffer() const { return m_VertexBuffer.Get(); }
        [[nodiscard]] RHI::VulkanBuffer* GetIndexBuffer() const { return m_IndexBuffer.Get(); }
        [[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }
        [[nodiscard]] const GeometryBufferLayout& GetLayout() const { return m_Layout; }
        [[nodiscard]] PrimitiveTopology GetTopology() const { return m_Layout.Topology; }

        // Local-space bounding sphere: (center.xyz, radius).
        // Computed from CPU vertex positions at upload time.
        // radius > 0 = valid bounds; radius <= 0 = empty/unknown geometry.
        [[nodiscard]] glm::vec4 GetLocalBoundingSphere() const { return m_LocalBoundingSphere; }

    private:
        // Buffer leases keep retained GPU memory alive across reused geometry views.
        RHI::BufferLease m_VertexBuffer{};
        RHI::BufferLease m_IndexBuffer{};

        GeometryBufferLayout m_Layout{};
        uint32_t m_IndexCount = 0;
        glm::vec4 m_LocalBoundingSphere{0.0f, 0.0f, 0.0f, 0.0f};
    };

}
