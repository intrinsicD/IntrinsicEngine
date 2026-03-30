module;
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

module Graphics.RenderPipeline;

import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;
import Graphics.Geometry;

namespace
{
    // Mirrors the existing FrustumCullSphere in PassUtils.hpp.
    // Duplicated here because PassUtils is a global-module-fragment header
    // and cannot be imported from a module implementation unit.
    [[nodiscard]] bool CullSphere(const glm::mat4& worldMatrix,
                                   const glm::vec4& localBounds,
                                   const Geometry::Frustum& frustum)
    {
        const float localRadius = localBounds.w;
        if (localRadius <= 0.0f)
            return false; // Empty/inactive geometry — treat as culled.

        const glm::vec3 localCenter{localBounds.x, localBounds.y, localBounds.z};
        const glm::vec3 worldCenter = glm::vec3(worldMatrix * glm::vec4(localCenter, 1.0f));

        const float sx = glm::length(glm::vec3(worldMatrix[0]));
        const float sy = glm::length(glm::vec3(worldMatrix[1]));
        const float sz = glm::length(glm::vec3(worldMatrix[2]));
        const float worldRadius = localRadius * glm::max(sx, glm::max(sy, sz));

        return Geometry::TestOverlap(frustum, Geometry::Sphere{worldCenter, worldRadius});
    }
}

namespace Graphics
{
    void ResolveDrawPacketBounds(
        std::span<LineDrawPacket> lines,
        std::span<PointDrawPacket> points,
        const GeometryPool& geometryStorage)
    {
        // Packets whose geometry is unresolvable retain {0,0,0,0} bounds
        // (zero radius).  CullDrawPackets treats zero-radius as culled, which
        // is safe because the pass's own GetIfValid check will also skip them.
        for (auto& line : lines)
        {
            if (!line.Geometry.IsValid())
                continue;
            if (const auto* geo = geometryStorage.GetIfValid(line.Geometry))
                line.LocalBoundingSphere = geo->GetLocalBoundingSphere();
        }

        for (auto& pt : points)
        {
            if (!pt.Geometry.IsValid())
                continue;
            if (const auto* geo = geometryStorage.GetIfValid(pt.Geometry))
                pt.LocalBoundingSphere = geo->GetLocalBoundingSphere();
        }
    }

    CulledDrawList CullDrawPackets(
        std::span<const LineDrawPacket> lines,
        std::span<const PointDrawPacket> points,
        const glm::mat4& cameraProj,
        const glm::mat4& cameraView,
        bool cullingEnabled)
    {
        CulledDrawList result;
        result.TotalLineCount = static_cast<uint32_t>(lines.size());
        result.TotalPointCount = static_cast<uint32_t>(points.size());
        result.Active = true;

        // When culling is disabled, include all indices.
        if (!cullingEnabled)
        {
            result.VisibleLineIndices.resize(lines.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(lines.size()); ++i)
                result.VisibleLineIndices[i] = i;

            result.VisiblePointIndices.resize(points.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(points.size()); ++i)
                result.VisiblePointIndices[i] = i;

            return result;
        }

        const Geometry::Frustum frustum =
            Geometry::Frustum::CreateFromMatrix(cameraProj * cameraView);

        // --- Lines ---
        result.VisibleLineIndices.reserve(lines.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(lines.size()); ++i)
        {
            if (CullSphere(lines[i].WorldMatrix, lines[i].LocalBoundingSphere, frustum))
                result.VisibleLineIndices.push_back(i);
        }

        // --- Points ---
        result.VisiblePointIndices.reserve(points.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(points.size()); ++i)
        {
            if (CullSphere(points[i].WorldMatrix, points[i].LocalBoundingSphere, frustum))
                result.VisiblePointIndices.push_back(i);
        }

        result.CulledLineCount = result.TotalLineCount -
            static_cast<uint32_t>(result.VisibleLineIndices.size());
        result.CulledPointCount = result.TotalPointCount -
            static_cast<uint32_t>(result.VisiblePointIndices.size());

        // Verify all produced indices are in bounds.
        assert(std::ranges::all_of(result.VisibleLineIndices,
            [&](uint32_t i) { return i < lines.size(); }));
        assert(std::ranges::all_of(result.VisiblePointIndices,
            [&](uint32_t i) { return i < points.size(); }));

        return result;
    }
}
