module;

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Graphics:VisualizationConfig;

import :Colormap;

// =============================================================================
// Per-entity visualization configuration for PropertySet-driven color rendering.
//
// ColorSource selects which property drives color output on a given domain
// (vertex, edge, face). When PropertyName is empty, the default behavior
// applies (hardcoded "v:color" / "e:color" / "f:color" or uniform color).
//
// VisualizationConfig aggregates per-domain color sources plus optional
// isoline and vector field overlay settings.
// =============================================================================

export namespace Graphics
{
    /// Selects a PropertySet property and maps it to colors.
    struct ColorSource
    {
        std::string PropertyName;                    ///< "" = none / use default
        Colormap::Type Map = Colormap::Type::Viridis;
        float RangeMin = 0.0f;
        float RangeMax = 1.0f;
        bool  AutoRange = true;                      ///< Recompute min/max from data
        uint32_t Bins = 0;                           ///< 0 = continuous, >0 = quantized
    };

    /// Isoline overlay for scalar fields (vertex-domain, meshes only).
    struct IsolineConfig
    {
        bool Enabled = false;
        std::string PropertyName;                    ///< Which scalar property
        uint32_t Count = 10;                         ///< Number of isolines
        float RangeMin = 0.0f;
        float RangeMax = 1.0f;
        bool AutoRange = true;
        glm::vec4 Color = {0.0f, 0.0f, 0.0f, 1.0f};
        float Width = 1.5f;
    };

    /// Vector field overlay — each active field spawns a child Graph entity.
    struct VectorFieldEntry
    {
        std::string PropertyName;                    ///< vec3 vertex property
        float Scale = 1.0f;                          ///< Arrow length multiplier
        glm::vec4 Color = {0.2f, 0.6f, 1.0f, 1.0f};
        float EdgeWidth = 1.5f;
        bool Overlay = true;                         ///< No depth test
        entt::entity ChildEntity = entt::null;       ///< Managed child Graph entity
        std::string ColorPropertyName;               ///< "" = uniform Color; scalar/vec3/vec4 property for per-arrow color
        std::string LengthPropertyName;              ///< "" = uniform Scale; scalar property for per-arrow length
    };

    /// Per-entity visualization configuration.
    /// Stored on data authority components (Mesh::Data, Graph::Data, PointCloud::Data).
    struct VisualizationConfig
    {
        ColorSource VertexColors;                    ///< Points/nodes
        ColorSource EdgeColors;                      ///< Edges (graph, mesh wireframe)
        ColorSource FaceColors;                      ///< Faces (mesh only)

        IsolineConfig Isolines;

        /// Multiple vector fields can be active simultaneously.
        /// Each spawns a separate child Graph entity.
        std::vector<VectorFieldEntry> VectorFields;
    };
}
