// Mesh-field property capture/restore; include after Geometry.Properties and
// standard type declarations. Only double and bool have shared instantiations;
// other field types and all definitions stay in the curvature implementation.
#pragma once

namespace Extrinsic::Runtime::MeshFieldDetail
{
    template <typename T>
    [[nodiscard]] bool CaptureCurvatureProperty(
        Geometry::PropertySet& properties, std::string_view name,
        std::size_t expectedCount, bool& hadProperty,
        std::vector<T>& values, std::string& diagnostic);

    // An unauthored property is removed rather than left stale.
    template <typename T>
    [[nodiscard]] bool ApplyCurvatureProperty(
        Geometry::PropertySet& properties, std::string_view name,
        bool hasProperty, const std::vector<T>& values, const T& defaultValue);
}
