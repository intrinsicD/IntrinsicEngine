#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// =============================================================================
// PointCloudPropertyHelpers — shared point-cloud property extraction utilities.
//
// Consolidates duplicated property extraction logic between
// PointCloudGeometrySync (full upload) and PropertySetDirtySync (incremental
// sync). This header is intended for inclusion in point-cloud related system
// .cpp files only.
// =============================================================================

namespace Graphics::PointCloudPropertyHelpers
{
    // Extract per-point colors via ColorMapper with the canonical p:color
    // fallback when no property is explicitly selected.
    [[nodiscard]] inline std::vector<uint32_t> ExtractPointColors(
        const Geometry::PointCloud::Cloud& cloud,
        Graphics::ColorSource& config)
    {
        if (config.PropertyName.empty() && cloud.HasColors())
            config.PropertyName = "p:color";

        if (auto mapped = ColorMapper::MapProperty(cloud.PointProperties(), config))
            return std::move(mapped->Colors);

        return {};
    }

    // Extract per-point radii from the canonical authoritative cloud storage.
    [[nodiscard]] inline std::vector<float> ExtractPointRadii(
        const Geometry::PointCloud::Cloud& cloud)
    {
        std::vector<float> radii;
        if (!cloud.HasRadii())
            return radii;

        const auto values = cloud.Radii();
        radii.assign(values.begin(), values.end());
        return radii;
    }
}
