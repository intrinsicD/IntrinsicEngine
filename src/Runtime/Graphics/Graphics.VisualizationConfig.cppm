module;

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Graphics.VisualizationConfig;

import Graphics.Colormap;

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

    /// Domain used by vector-field overlays.
    /// The selected domain determines both the source PropertySet and the
    /// base-point sampling strategy used by the line renderer.
    enum class VectorFieldDomain : unsigned char
    {
        Vertex,
        Edge,
        Face,
    };

    /// Vector field overlay — each active field spawns a child Graph entity.
    /// The field is attached to one present domain on the source entity and
    /// can choose a vec3 property for the base points, a vec3 property for
    /// the vector source, and an optional property-driven color source.
    ///
    /// The child graph stores explicit base/end points baked on the CPU so
    /// the retained line pipeline can render them as ordinary line segments.
    struct VectorFieldEntry
    {
        VectorFieldDomain Domain = VectorFieldDomain::Vertex;
        std::string BasePropertyName;                ///< vec3 base-point property; empty = canonical domain positions
        std::string VectorPropertyName;              ///< vec3 vector source; empty = inactive / destroy child
        float Scale = 1.0f;                          ///< Arrow length multiplier
        glm::vec4 Color = {0.2f, 0.6f, 1.0f, 1.0f};
        float EdgeWidth = 1.5f;
        bool Overlay = true;                         ///< No depth test
        entt::entity ChildEntity = entt::null;       ///< Managed child Graph entity
        ColorSource ArrowColor;                      ///< "" = uniform Color; scalar/vec3/vec4 property for per-arrow color
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

        /// When true, per-vertex colors are not interpolated across triangles.
        /// Instead, each fragment picks the color of the closest triangle vertex
        /// (by Euclidean distance in object space), producing Voronoi-like regions.
        /// Used for cluster label visualization (e.g. K-Means).
        bool UseNearestVertexColors = false;

        /// Multiple vector fields can be active simultaneously.
        /// Each spawns a separate child Graph entity.
        std::vector<VectorFieldEntry> VectorFields = std::vector<VectorFieldEntry>{};

        /// Set true when VectorFields entries change or source data changes.
        /// Consumed by PropertySetDirtySync to trigger SyncVectorFields.
        bool VectorFieldsDirty = false;
    };
}
