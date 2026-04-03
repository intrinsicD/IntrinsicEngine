module;
#include <algorithm>
#include <cassert>
#include <cmath>
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
    std::array<float, ShadowParams::MaxCascades> ComputeCascadeSplitDistances(
        float nearPlane,
        float farPlane,
        uint32_t cascadeCount,
        float splitLambda)
    {
        std::array<float, ShadowParams::MaxCascades> splits{};
        splits.fill(1.0f);

        const uint32_t clampedCascadeCount = std::clamp(cascadeCount, 1u, ShadowParams::MaxCascades);
        const float clampedLambda = std::clamp(splitLambda, 0.0f, 1.0f);
        const float n = std::max(nearPlane, 1.0e-4f);
        const float f = std::max(farPlane, n + 1.0e-4f);
        const float range = f - n;
        const float ratio = f / n;

        for (uint32_t i = 0; i < clampedCascadeCount; ++i)
        {
            const float p = static_cast<float>(i + 1) / static_cast<float>(clampedCascadeCount);
            const float logSplit = n * std::pow(ratio, p);
            const float uniformSplit = n + range * p;
            const float blended = clampedLambda * logSplit + (1.0f - clampedLambda) * uniformSplit;
            splits[i] = std::clamp((blended - n) / range, 0.0f, 1.0f);
            if (i > 0)
                splits[i] = std::max(splits[i], splits[i - 1]);
        }

        return splits;
    }

    ShadowCascadeData PackShadowCascadeData(
        const ShadowParams& shadowParams,
        std::span<const glm::mat4> cascadeMatrices)
    {
        ShadowCascadeData packed{};
        packed.CascadeCount = std::clamp(shadowParams.CascadeCount, 1u, ShadowParams::MaxCascades);
        packed.DepthBias = shadowParams.DepthBias;
        packed.NormalBias = shadowParams.NormalBias;
        packed.PcfFilterRadius = shadowParams.PcfFilterRadius;
        packed.SplitDistances = shadowParams.CascadeSplits;

        // Enforce monotonic normalized split distances for robust shader-side
        // cascade selection even when edited values are partially invalid.
        packed.SplitDistances[0] = std::clamp(packed.SplitDistances[0], 0.0f, 1.0f);
        for (uint32_t i = 1; i < ShadowParams::MaxCascades; ++i)
        {
            packed.SplitDistances[i] = std::clamp(packed.SplitDistances[i], 0.0f, 1.0f);
            packed.SplitDistances[i] = std::max(packed.SplitDistances[i], packed.SplitDistances[i - 1]);
        }
        packed.SplitDistances[ShadowParams::MaxCascades - 1] = 1.0f;

        for (uint32_t i = 0; i < ShadowParams::MaxCascades; ++i)
        {
            if (i < cascadeMatrices.size())
                packed.LightViewProjection[i] = cascadeMatrices[i];
            else
                packed.LightViewProjection[i] = glm::mat4(1.0f);
        }

        return packed;
    }

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
