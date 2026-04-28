module;

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Graphics.BVHDebugDraw;

import Graphics.DebugDraw;

export namespace Graphics
{
    struct BVHDebugDrawSettings
    {
        bool Enabled = false;
        bool Overlay = true;

        // Inclusive depth limit. Root is depth 0.
        std::uint32_t MaxDepth = 12;

        // Draw policy for node AABBs.
        bool LeafOnly = false;
        bool DrawInternal = true;

        // Triangle budget per leaf used by debug-only builder.
        std::uint32_t LeafTriangleCount = 8;

        float Alpha = 0.65f;
        glm::vec3 LeafColor = {0.95f, 0.60f, 0.10f};
        glm::vec3 InternalColor = {0.65f, 0.20f, 1.00f};
    };

    // Builds a transient triangle BVH from local-space mesh data and emits node bounds to DebugDraw.
    void DrawBVH(DebugDraw& dd,
                 std::span<const glm::vec3> positions,
                 std::span<const uint32_t> indices,
                 const BVHDebugDrawSettings& settings,
                 const glm::mat4& worldTransform = glm::mat4(1.0f));
}
