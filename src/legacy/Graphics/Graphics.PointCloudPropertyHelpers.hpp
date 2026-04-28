#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// PointCloudPropertyHelpers — shared point-cloud property extraction utilities.
//
// Two flavours:
//
//   Cloud-backed  (ExtractPointColors / ExtractPointRadii)
//     Take a const Cloud& — used when the caller holds a Cloud object.
//
//   PropertySet-backed  (ExtractPointColorsFromPropertySet / …)
//     Take a const PropertySet& — used with authoritative GeometrySources
//     Vertices components (already compacted, no deleted gaps).
// =============================================================================

namespace Graphics::PointCloudPropertyHelpers
{
    // -------------------------------------------------------------------------
    // Cloud-backed helpers
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // PropertySet-backed helpers (compacted GeometrySources — no deleted gaps)
    // Cloud stores positions/colors under "p:*" prefix; "v:*" is the canonical
    // GeometrySources key.  We probe both so the PropertySet from PopulateFromCloud
    // (which copies the full cloud PropertySet AND writes "v:position") works.
    // -------------------------------------------------------------------------

    [[nodiscard]] inline std::vector<uint32_t> ExtractPointColorsFromPropertySet(
        const Geometry::PropertySet& properties,
        Graphics::ColorSource& config)
    {
        if (config.PropertyName.empty())
        {
            if (properties.Exists("p:color"))      config.PropertyName = "p:color";
            else if (properties.Exists("v:color")) config.PropertyName = "v:color";
        }

        if (auto mapped = ColorMapper::MapProperty(properties, config))
            return std::move(mapped->Colors);

        return {};
    }

    [[nodiscard]] inline std::vector<float> ExtractPointRadiiFromPropertySet(
        const Geometry::PropertySet& properties)
    {
        std::string_view radiiName;
        if      (properties.Exists("p:radius")) radiiName = "p:radius";
        else if (properties.Exists("v:radius")) radiiName = "v:radius";
        else return {};

        auto prop = properties.Get<float>(radiiName);
        if (!prop)
            return {};

        return std::vector<float>(prop.Vector().begin(), prop.Vector().end());
    }
}
