module;

#include <string>
#include <vector>
#include <cstdint>

export module Graphics:PropertyEnumerator;

import Geometry;

// =============================================================================
// PropertySet property enumeration for color visualization UI.
//
// Enumerates all properties of a PropertySet that are suitable for color
// mapping (float for scalar fields, vec3/vec4 for direct RGB/RGBA), filtering
// out internal properties (positions, normals, halfedge connectivity).
// =============================================================================

export namespace Graphics
{
    /// Type classification for colorable properties.
    enum class PropertyDataType : uint8_t
    {
        Scalar,   ///< float — mapped through colormap
        Vec3,     ///< glm::vec3 — interpreted as RGB
        Vec4,     ///< glm::vec4 — interpreted as RGBA
    };

    /// A colorable property discovered in a PropertySet.
    struct PropertyInfo
    {
        std::string Name;
        PropertyDataType Type;
    };

    /// Returns all float/vec3/vec4 properties suitable for color mapping.
    /// Filters out internal properties (positions, normals, connectivity).
    [[nodiscard]] std::vector<PropertyInfo> EnumerateColorableProperties(
        const Geometry::PropertySet& ps);

    /// Returns only scalar (float) properties suitable for scalar field viz.
    [[nodiscard]] std::vector<PropertyInfo> EnumerateScalarProperties(
        const Geometry::PropertySet& ps);

    /// Returns only vec3 properties suitable for vector field viz.
    [[nodiscard]] std::vector<PropertyInfo> EnumerateVectorProperties(
        const Geometry::PropertySet& ps);
}
