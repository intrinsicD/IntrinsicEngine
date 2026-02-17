module;

#include <cstdint>

#include <glm/glm.hpp>

export module Graphics:KDTreeDebugDraw;

import Geometry;
import :DebugDraw;

export namespace Graphics
{
    struct KDTreeDebugDrawSettings
    {
        bool Enabled = false;
        bool Overlay = true;

        // Inclusive depth limit. Root is depth 0.
        std::uint32_t MaxDepth = 12;

        // Draw policy for node AABBs.
        bool LeafOnly = false;
        bool DrawInternal = true;
        bool OccupiedOnly = true;

        // Optional split-plane visualization for internal nodes.
        bool DrawSplitPlanes = true;

        float Alpha = 0.65f;
        glm::vec3 LeafColor = {0.10f, 0.80f, 0.30f};
        glm::vec3 InternalColor = {0.10f, 0.55f, 1.00f};
        glm::vec3 SplitPlaneColor = {1.00f, 0.25f, 0.10f};
    };

    // Emits KD-tree node AABBs and optional split planes into DebugDraw.
    void DrawKDTree(DebugDraw& dd, const Geometry::KDTree& tree, const KDTreeDebugDrawSettings& settings);

    // Overload that transforms local-space lines into world space.
    void DrawKDTree(DebugDraw& dd, const Geometry::KDTree& tree, const KDTreeDebugDrawSettings& settings,
                    const glm::mat4& worldTransform);
}

