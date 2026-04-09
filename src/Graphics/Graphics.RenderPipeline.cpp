module;
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
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

    std::array<glm::mat4, ShadowParams::MaxCascades>
    ComputeCascadeViewProjections(
        const glm::mat4& cameraView,
        const glm::mat4& cameraProj,
        const glm::vec3& lightDir,
        const std::array<float, ShadowParams::MaxCascades>& splits,
        uint32_t cascadeCount,
        uint32_t cascadeResolution,
        float nearPlane,
        float farPlane)
    {
        std::array<glm::mat4, ShadowParams::MaxCascades> result{};
        for (auto& m : result) m = glm::mat4(1.0f);

        const uint32_t count = std::clamp(cascadeCount, 1u, ShadowParams::MaxCascades);
        const float n = std::max(nearPlane, 1e-4f);
        const float f = std::max(farPlane, n + 1e-4f);

        // Camera inverse VP to unproject NDC corners back to world space.
        const glm::mat4 invVP = glm::inverse(cameraProj * cameraView);

        // Light view matrix: look from origin along the light direction.
        // lightDir points *toward* the light, so the light looks along -lightDir.
        const glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f)
            ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -lightDir, up);

        for (uint32_t c = 0; c < count; ++c)
        {
            // Cascade near/far in absolute depth.
            const float cascadeNear = (c == 0) ? n : glm::mix(n, f, splits[c - 1]);
            const float cascadeFar  = glm::mix(n, f, splits[c]);

            // Convert near/far to NDC z (Vulkan: depth in [0,1]).
            // z_ndc = (Proj[2][2]*ze + Proj[3][2]) / (-ze)  where ze is view-space z (negative).
            auto depthToNDC = [&](float depth) -> float {
                float ze = -depth; // view-space z is negative
                return (cameraProj[2][2] * ze + cameraProj[3][2]) / (-ze);
            };
            const float zNear = depthToNDC(cascadeNear);
            const float zFar  = depthToNDC(cascadeFar);

            // 8 corners of the cascade frustum slice in NDC, then unproject to world space.
            glm::vec3 corners[8];
            int idx = 0;
            for (float z : {zNear, zFar})
            {
                for (float y : {-1.0f, 1.0f})
                {
                    for (float x : {-1.0f, 1.0f})
                    {
                        glm::vec4 ndc(x, y, z, 1.0f);
                        glm::vec4 world = invVP * ndc;
                        corners[idx++] = glm::vec3(world) / world.w;
                    }
                }
            }

            // Transform corners to light space and find AABB.
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            for (int i = 0; i < 8; ++i)
            {
                glm::vec3 lsCorner = glm::vec3(lightView * glm::vec4(corners[i], 1.0f));
                minBounds = glm::min(minBounds, lsCorner);
                maxBounds = glm::max(maxBounds, lsCorner);
            }

            // Extend Z range for shadow casters behind the camera frustum.
            const float zMargin = (maxBounds.z - minBounds.z) * 0.5f;
            minBounds.z -= zMargin;

            // Orthographic projection in light space.
            glm::mat4 lightProj = glm::ortho(
                minBounds.x, maxBounds.x,
                minBounds.y, maxBounds.y,
                -maxBounds.z, -minBounds.z); // negate z: glm::ortho expects near < far (positive)
            lightProj[1][1] *= -1.0f; // Vulkan Y-flip

            // --- Texel snapping: quantize the shadow-map origin in NDC space ---
            // The standard technique (used by MJP / TheRealMJP) rounds the XY
            // translation of the final LightViewProj to the nearest texel boundary
            // in post-projection shadow-map space.  This eliminates sub-texel jitter
            // from camera movement while preserving the cascade extent.
            glm::mat4 shadowMatrix = lightProj * lightView;
            if (cascadeResolution > 0u)
            {
                const float halfRes = static_cast<float>(cascadeResolution) * 0.5f;

                // Project the origin through the shadow matrix to find its position
                // in shadow-map texels, round to the nearest texel, then compute
                // the fractional offset to apply back.
                glm::vec4 originNDC = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                // NDC xy in [-1,1] → texel coords in [0, resolution].
                float texelX = originNDC.x * halfRes;
                float texelY = originNDC.y * halfRes;
                float roundedX = std::round(texelX);
                float roundedY = std::round(texelY);
                float offsetX = (roundedX - texelX) / halfRes;
                float offsetY = (roundedY - texelY) / halfRes;

                // Apply the correction to the projection's translation component.
                shadowMatrix[3][0] += offsetX;
                shadowMatrix[3][1] += offsetY;
            }

            result[c] = shadowMatrix;
        }

        return result;
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
