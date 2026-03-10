#pragma once

// SandboxCommon.h — Shared types and utility functions used by Sandbox controllers.
// These are Sandbox-local (not part of the engine runtime), so they stay in the app.

#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <entt/entity/registry.hpp>

import Graphics;
import Geometry;

namespace Sandbox
{
    struct HiddenEditorEntityTag
    {
    };

    struct RetainedLineOverlaySlot
    {
        entt::entity Entity = entt::null;
        Geometry::GeometryHandle Geometry{};
    };

    // Maximum stack depth for iterative octree traversal. Octrees with depth > 16
    // are unusual; 512 entries covers 8-wide branching to depth 16+ with margin.
    constexpr std::size_t kMaxOctreeTraversalStack = 512;

    [[nodiscard]] inline bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
    {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                if (std::abs(a[c][r] - b[c][r]) > eps)
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool Vec3NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
    }

    [[nodiscard]] inline bool OctreeSettingsEqual(const Graphics::OctreeDebugDrawSettings& a,
                                                   const Graphics::OctreeDebugDrawSettings& b)
    {
        return a.Enabled == b.Enabled &&
            a.Overlay == b.Overlay &&
            a.ColorByDepth == b.ColorByDepth &&
            a.MaxDepth == b.MaxDepth &&
            a.LeafOnly == b.LeafOnly &&
            a.DrawInternal == b.DrawInternal &&
            a.OccupiedOnly == b.OccupiedOnly &&
            std::abs(a.Alpha - b.Alpha) <= 1e-4f &&
            Vec3NearlyEqual(a.BaseColor, b.BaseColor);
    }

    [[nodiscard]] inline glm::vec3 DepthRamp(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        constexpr std::array<glm::vec3, 5> k = {
            glm::vec3{0.267f, 0.005f, 0.329f},
            glm::vec3{0.230f, 0.322f, 0.546f},
            glm::vec3{0.128f, 0.566f, 0.550f},
            glm::vec3{0.369f, 0.788f, 0.382f},
            glm::vec3{0.993f, 0.906f, 0.144f},
        };

        const float x = t * 4.0f;
        const int i0 = std::clamp(static_cast<int>(x), 0, 3);
        const int i1 = i0 + 1;
        const float alpha = x - static_cast<float>(i0);
        return k[i0] * (1.0f - alpha) + k[i1] * alpha;
    }

    [[nodiscard]] inline uint32_t PackWithAlpha(const glm::vec3& rgb, float alpha)
    {
        return Graphics::DebugDraw::PackColorF(rgb.r, rgb.g, rgb.b, alpha);
    }

    inline void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                               glm::vec3& outLo, glm::vec3& outHi)
    {
        glm::vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
            {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
            {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
        };

        outLo = glm::vec3(std::numeric_limits<float>::max());
        outHi = glm::vec3(std::numeric_limits<float>::lowest());

        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 transformed = glm::vec3(m * glm::vec4(corner, 1.0f));
            outLo = glm::min(outLo, transformed);
            outHi = glm::max(outHi, transformed);
        }
    }

} // namespace Sandbox
