module;

#include <cstddef>

export module Geometry.PointCloudFwd;

import Geometry.Properties;

export namespace Geometry::PointCloud
{
    struct CloudProperties
    {
        PropertySet Vertices{};

        std::size_t DeletedVertices{0};
    };
}