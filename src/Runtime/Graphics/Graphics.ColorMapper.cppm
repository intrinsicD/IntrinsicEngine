module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module Graphics.ColorMapper;

import Graphics.VisualizationConfig;
import Geometry.Properties;

// =============================================================================
// Generic PropertySet → packed ABGR color mapping.
//
// Reads a named property from a PropertySet and produces packed ABGR colors
// suitable for direct upload to the GPU aux buffer. Handles three property
// types:
//   float    → normalize to [0,1] via range, apply colormap + optional binning
//   glm::vec3 → interpret as RGB, pack with alpha=1
//   glm::vec4 → interpret as RGBA, pack directly
//
// When AutoRange is true, min/max are computed from the data and written back
// to the ColorSource for UI display.
// =============================================================================

export namespace Graphics::ColorMapper
{
    struct MappingResult
    {
        std::vector<uint32_t> Colors;          ///< Packed ABGR, one per element
        float ComputedMin = 0.0f;              ///< Auto-range min (for UI feedback)
        float ComputedMax = 1.0f;              ///< Auto-range max (for UI feedback)
    };

    /// Map a named property to packed ABGR colors.
    ///
    /// @param ps          PropertySet to read from
    /// @param config      Color source configuration (property name, colormap, range, bins)
    /// @param skipDeleted Optional predicate: returns true for elements to skip
    ///                    (used for Graph iteration where deleted elements exist in the array).
    ///                    When null, all elements are included.
    /// @returns           MappingResult on success, nullopt if property not found or type mismatch.
    [[nodiscard]] std::optional<MappingResult> MapProperty(
        const Geometry::PropertySet& ps,
        ColorSource& config,
        std::function<bool(size_t)> skipDeleted = {});
}
