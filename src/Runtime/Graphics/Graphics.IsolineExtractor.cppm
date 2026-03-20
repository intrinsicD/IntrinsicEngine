module;

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Graphics.IsolineExtractor;

import Geometry;

// =============================================================================
// IsolineExtractor — extract contour lines from scalar fields on meshes.
//
// Given a Halfedge::Mesh and a scalar vertex property, produces line segments
// at evenly spaced iso-values by linear interpolation along mesh edges.
// The result is rendered via DebugDraw::AddLines() as transient overlay.
// =============================================================================

export namespace Graphics::IsolineExtractor
{
    struct IsolineResult
    {
        /// Line segments as pairs of points: [p0, p1, p0, p1, ...].
        /// Each consecutive pair forms one segment.
        std::vector<glm::vec3> Points;
    };

    /// Extract isolines from a scalar vertex property on a mesh.
    ///
    /// @param mesh           Source mesh
    /// @param scalarProperty Name of the float vertex property
    /// @param isoCount       Number of evenly spaced isolines
    /// @param rangeMin       Minimum value for the iso-range
    /// @param rangeMax       Maximum value for the iso-range
    /// @returns              Line segments (point pairs)
    [[nodiscard]] IsolineResult Extract(
        const Geometry::Halfedge::Mesh& mesh,
        const std::string& scalarProperty,
        uint32_t isoCount,
        float rangeMin,
        float rangeMax);
}
