module;
#include <cstdint>

export module Graphics.Components.PointKDTree;

import Geometry.KDTree;

export namespace ECS::PointKDTree
{
    struct Data
    {
        Geometry::KDTree Tree{};
        Geometry::KDTreeBuildParams BuildParams{};
        bool Dirty = true;
        uint32_t PointCount = 0;

        void Clear()
        {
            Tree = {};
            Dirty = true;
            PointCount = 0;
        }

        [[nodiscard]] bool HasValidTree() const noexcept
        {
            return PointCount > 0 && !Tree.Nodes().empty();
        }
    };
}
