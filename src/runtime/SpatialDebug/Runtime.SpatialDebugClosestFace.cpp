module;

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

module Extrinsic.Runtime.SpatialDebugClosestFace;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) &&
                   std::isfinite(p.y) &&
                   std::isfinite(p.z);
        }

        [[nodiscard]] SpatialDebugClosestFaceOverlay MakeFailure(
            const SpatialDebugClosestFaceMeshSource& source,
            const glm::vec3&                         probePoint,
            const SpatialDebugClosestFaceStatus      status,
            const Geometry::MeshClosestFaceStatus    queryStatus =
                Geometry::MeshClosestFaceStatus::UnbuiltIndex) noexcept
        {
            SpatialDebugClosestFaceOverlay overlay{};
            overlay.MeshKey = source.MeshKey;
            overlay.MeshRevision = source.Revision;
            overlay.ProbePoint = IsFinite(probePoint) ? probePoint : glm::vec3{0.0f};
            overlay.Status = status;
            overlay.QueryStatus = queryStatus;
            return overlay;
        }
    }

    const char* DebugName(const SpatialDebugClosestFaceStatus status) noexcept
    {
        switch (status)
        {
        case SpatialDebugClosestFaceStatus::Resolved:
            return "Resolved";
        case SpatialDebugClosestFaceStatus::NoActiveMesh:
            return "NoActiveMesh";
        case SpatialDebugClosestFaceStatus::NoMeshSource:
            return "NoMeshSource";
        case SpatialDebugClosestFaceStatus::EmptyMesh:
            return "EmptyMesh";
        case SpatialDebugClosestFaceStatus::InvalidProbe:
            return "InvalidProbe";
        case SpatialDebugClosestFaceStatus::IndexBuildFailed:
            return "IndexBuildFailed";
        case SpatialDebugClosestFaceStatus::QueryFailed:
            return "QueryFailed";
        }
        return "Unknown";
    }

    void SpatialDebugClosestFaceConsumer::Invalidate()
    {
        m_Index = Geometry::MeshClosestFaceIndex{};
        m_CachedMeshKey = 0u;
        m_CachedMeshRevision = 0u;
        m_HasCachedIndex = false;
    }

    SpatialDebugClosestFaceOverlay SpatialDebugClosestFaceConsumer::Resolve(
        const SpatialDebugClosestFaceMeshSource& source,
        const glm::vec3&                         probePoint)
    {
        if (!source.Active)
        {
            return MakeFailure(
                source,
                probePoint,
                SpatialDebugClosestFaceStatus::NoActiveMesh);
        }

        if (source.Mesh == nullptr)
        {
            return MakeFailure(
                source,
                probePoint,
                SpatialDebugClosestFaceStatus::NoMeshSource);
        }

        if (!IsFinite(probePoint))
        {
            return MakeFailure(
                source,
                probePoint,
                SpatialDebugClosestFaceStatus::InvalidProbe,
                Geometry::MeshClosestFaceStatus::InvalidQueryPoint);
        }

        const bool requiresRebuild =
            !m_HasCachedIndex ||
            m_CachedMeshKey != source.MeshKey ||
            m_CachedMeshRevision != source.Revision;

        if (requiresRebuild)
        {
            m_Index = Geometry::MeshClosestFaceIndex{};
            m_CachedMeshKey = source.MeshKey;
            m_CachedMeshRevision = source.Revision;
            m_HasCachedIndex = false;
            ++m_RebuildCount;

            if (!m_Index.Build(*source.Mesh))
            {
                const SpatialDebugClosestFaceStatus status =
                    source.Mesh->FacesSize() == 0u
                        ? SpatialDebugClosestFaceStatus::EmptyMesh
                        : SpatialDebugClosestFaceStatus::IndexBuildFailed;
                return MakeFailure(
                    source,
                    probePoint,
                    status,
                    Geometry::MeshClosestFaceStatus::EmptyIndex);
            }

            m_HasCachedIndex = true;
        }

        const Geometry::MeshClosestFaceResult query = m_Index.Query(probePoint);
        if (!query.Found || query.Status != Geometry::MeshClosestFaceStatus::Success)
        {
            SpatialDebugClosestFaceOverlay overlay = MakeFailure(
                source,
                probePoint,
                SpatialDebugClosestFaceStatus::QueryFailed,
                query.Status);
            overlay.Diagnostics = query.Diagnostics;
            return overlay;
        }

        SpatialDebugClosestFaceOverlay overlay{};
        overlay.Found = true;
        overlay.Face = query.Face;
        overlay.ProbePoint = probePoint;
        overlay.ClosestPoint = query.Point;
        overlay.Normal = query.Normal;
        overlay.SquaredDistance = query.SquaredDistance;
        overlay.Distance = std::sqrt(std::max(0.0f, query.SquaredDistance));
        overlay.PrimitiveIndex = query.PrimitiveIndex;
        overlay.MeshKey = source.MeshKey;
        overlay.MeshRevision = source.Revision;
        overlay.Status = SpatialDebugClosestFaceStatus::Resolved;
        overlay.QueryStatus = query.Status;
        overlay.Diagnostics = query.Diagnostics;
        return overlay;
    }

    bool SpatialDebugClosestFaceConsumer::HasCachedIndex() const noexcept
    {
        return m_HasCachedIndex;
    }

    std::uint64_t SpatialDebugClosestFaceConsumer::CachedMeshKey() const noexcept
    {
        return m_CachedMeshKey;
    }

    std::uint64_t SpatialDebugClosestFaceConsumer::CachedMeshRevision() const noexcept
    {
        return m_CachedMeshRevision;
    }

    std::uint32_t SpatialDebugClosestFaceConsumer::RebuildCount() const noexcept
    {
        return m_RebuildCount;
    }
}
