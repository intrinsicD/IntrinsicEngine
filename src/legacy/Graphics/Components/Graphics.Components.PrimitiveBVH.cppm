// -------------------------------------------------------------------------
// PrimitiveBVH::Data — optional entity-attached local-space primitive BVH.
// -------------------------------------------------------------------------
//
// Acceleration structure cache for CPU/GPU-adjacent spatial queries such as
// picking and future broadphase/collision workloads. The BVH itself is built
// over primitive AABBs in local space; exact primitive refinement stays in the
// consumer. Presence/absence of this component is the opt-in toggle.

module;
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

export module Graphics.Components.PrimitiveBVH;

import Geometry.BVH;
import Geometry.AABB;

export namespace ECS::PrimitiveBVH
{
    enum class SourceKind : uint8_t
    {
        None = 0,
        MeshTriangles,
        GraphSegments,
        PointCloudPoints,
    };

    enum class Backend : uint8_t
    {
        CPU = 0,
        CUDA = 1,
    };

    struct TrianglePrimitive
    {
        Geometry::AABB Bounds{};
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};
        glm::vec3 C{0.0f};
        uint32_t I0 = 0;
        uint32_t I1 = 0;
        uint32_t I2 = 0;
        uint32_t FaceIndex = 0;
    };

    struct SegmentPrimitive
    {
        Geometry::AABB Bounds{};
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};
        uint32_t EdgeIndex = 0;
    };

    struct PointPrimitive
    {
        Geometry::AABB Bounds{};
        glm::vec3 Position{0.0f};
        uint32_t PointIndex = 0;
    };

    struct Data
    {
        Geometry::BVH LocalBVH{};
        Geometry::AABB LocalBounds{};
        Geometry::BVHBuildParams BuildParams{};
        SourceKind Source = SourceKind::None;
        Backend ActualBackend = Backend::CPU;
        bool Dirty = true;
        uint32_t PrimitiveCount = 0;

        std::vector<TrianglePrimitive> Triangles{};
        std::vector<SegmentPrimitive> Segments{};
        std::vector<PointPrimitive> Points{};

        void Clear()
        {
            LocalBVH.Clear();
            LocalBounds = {};
            Source = SourceKind::None;
            PrimitiveCount = 0;
            Triangles.clear();
            Segments.clear();
            Points.clear();
        }

        [[nodiscard]] bool HasValidBVH() const noexcept
        {
            return PrimitiveCount > 0 && !LocalBVH.Nodes().empty() && LocalBounds.IsValid();
        }
    };
}
