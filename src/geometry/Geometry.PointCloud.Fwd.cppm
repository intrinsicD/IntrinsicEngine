module;

#include <cstddef>

export module Geometry.PointCloud.Fwd;

import Geometry.Properties;

export namespace Geometry::PointCloud
{
    struct Properties
    {
        PropertySet Vertices{};

        std::size_t DeletedVertices{0};
    };
}