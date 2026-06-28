module;

#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.SpatialDebugClosestFace;

import Geometry.HalfedgeMesh;
import Geometry.MeshClosestFace;
import Geometry.Properties;
import Geometry.SpatialQueries;

export namespace Extrinsic::Runtime
{
    enum class SpatialDebugClosestFaceStatus : std::uint8_t
    {
        Resolved,
        NoActiveMesh,
        NoMeshSource,
        EmptyMesh,
        InvalidProbe,
        IndexBuildFailed,
        QueryFailed,
    };

    [[nodiscard]] const char* DebugName(SpatialDebugClosestFaceStatus status) noexcept;

    struct SpatialDebugClosestFaceMeshSource
    {
        const Geometry::HalfedgeMesh::Mesh* Mesh{nullptr};
        std::uint64_t                       MeshKey{0u};
        std::uint64_t                       Revision{0u};
        bool                                Active{false};
    };

    struct SpatialDebugClosestFaceOverlay
    {
        bool                                Found{false};
        Geometry::FaceHandle                Face{};
        glm::vec3                           ProbePoint{0.0f};
        glm::vec3                           ClosestPoint{0.0f};
        glm::vec3                           Normal{0.0f, 1.0f, 0.0f};
        float                               Distance{0.0f};
        float                               SquaredDistance{0.0f};
        std::uint32_t                       PrimitiveIndex{0u};
        std::uint64_t                       MeshKey{0u};
        std::uint64_t                       MeshRevision{0u};
        SpatialDebugClosestFaceStatus       Status{SpatialDebugClosestFaceStatus::NoActiveMesh};
        Geometry::MeshClosestFaceStatus     QueryStatus{Geometry::MeshClosestFaceStatus::UnbuiltIndex};
        Geometry::SpatialKNNResult          Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Found && Status == SpatialDebugClosestFaceStatus::Resolved;
        }
    };

    class SpatialDebugClosestFaceConsumer
    {
    public:
        SpatialDebugClosestFaceConsumer() = default;
        ~SpatialDebugClosestFaceConsumer() = default;

        SpatialDebugClosestFaceConsumer(SpatialDebugClosestFaceConsumer&&) noexcept = default;
        SpatialDebugClosestFaceConsumer& operator=(SpatialDebugClosestFaceConsumer&&) noexcept = default;
        SpatialDebugClosestFaceConsumer(const SpatialDebugClosestFaceConsumer&) = delete;
        SpatialDebugClosestFaceConsumer& operator=(const SpatialDebugClosestFaceConsumer&) = delete;

        void Invalidate();

        [[nodiscard]] SpatialDebugClosestFaceOverlay Resolve(
            const SpatialDebugClosestFaceMeshSource& source,
            const glm::vec3&                         probePoint);

        [[nodiscard]] bool HasCachedIndex() const noexcept;
        [[nodiscard]] std::uint64_t CachedMeshKey() const noexcept;
        [[nodiscard]] std::uint64_t CachedMeshRevision() const noexcept;
        [[nodiscard]] std::uint32_t RebuildCount() const noexcept;

    private:
        Geometry::MeshClosestFaceIndex m_Index{};
        std::uint64_t                  m_CachedMeshKey{0u};
        std::uint64_t                  m_CachedMeshRevision{0u};
        std::uint32_t                  m_RebuildCount{0u};
        bool                           m_HasCachedIndex{false};
    };
}
