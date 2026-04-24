module;

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

export module Extrinsic.Graphics.Component.VisualizationConfig;

import Extrinsic.Graphics.Component.RenderGeometry;  // ScalarFieldConfig

// ============================================================
// VisualizationConfig — per-entity scientific-visualisation override.
//
// When this component is present on an entity that has both a
// MaterialInstance and a GpuSceneSlot, VisualizationSyncSystem
// selects an appropriate GPU rendering mode each frame and writes
// MaterialInstance::EffectiveSlot accordingly.
//
// When the component is absent, MaterialInstance governs appearance.
//
// Colour source priority (descending):
//   ScalarField       — GPU colourmap with optional isolines / binning
//   PerVertexBuffer   — raw RGBA vec4 per vertex from a named buffer
//   PerEdgeBuffer     — raw RGBA vec4 per edge from a named buffer
//   PerFaceBuffer     — raw RGBA vec4 per face from a named buffer
//   UniformColor      — flat single RGBA, no shading (Unlit flag set)
//   Material          — no override; MaterialInstance governs colour
//
// Buffer names are keys in GpuSceneSlot::Buffers (set by lifecycle
// systems).  Well-known names:
//   "scalars"        — float per-vertex/element generic scalar field
//   "geodesic"       — float geodesic distance from source vertices
//   "curvature"      — float Gaussian or mean curvature
//   "colors"         — vec4 per-vertex RGBA colours
//   "edge_colors"    — vec4 per-edge RGBA colours
//   "face_colors"    — vec4 per-face RGBA colours
//
// ScalarField GPU path:
//   VisualizationSyncSystem patches the entity's override material
//   with MaterialTypeID = kMaterialTypeID_SciVis and packs the
//   colourmap BDA + range + isoline parameters into CustomData[0..2].
//   The surface shader branches on MaterialTypeID to apply the
//   colourmap at fragment level.  Line/Point passes receive CPU-baked
//   RGBA colours uploaded to "vis_colors_baked" in GpuSceneSlot.
//
// Usage — geodesic distance on a mesh:
//   auto& vis          = registry.emplace<VisualizationConfig>(entity);
//   vis.Source         = VisualizationConfig::ColorSource::ScalarField;
//   vis.ScalarFieldName = "geodesic";
//   vis.Scalar.Map     = Colormap::Type::Plasma;
//   vis.Scalar.AutoRange = true;
//   vis.Scalar.Isolines.Num = 20;
//   vis.ScalarDomain   = VisualizationConfig::Domain::Vertex;
// ============================================================

export namespace Extrinsic::Graphics::Components
{
    struct VisualizationConfig
    {
        enum class ColorSource : std::uint8_t
        {
            Material,         ///< No override — MaterialInstance governs colour.
            UniformColor,     ///< Flat single RGBA value (MaterialFlags::Unlit set).
            ScalarField,      ///< GPU colourmap applied to a named float buffer.
            PerVertexBuffer,  ///< Raw RGBA vec4 per vertex from a named buffer.
            PerEdgeBuffer,    ///< Raw RGBA vec4 per edge from a named buffer.
            PerFaceBuffer,    ///< Raw RGBA vec4 per face from a named buffer.
        };

        ColorSource Source = ColorSource::Material;

        // ----- UniformColor -------------------------------------------------
        /// Flat RGBA colour used when Source == UniformColor.
        glm::vec4 Color{1.f, 0.6f, 0.f, 1.f};

        // ----- ScalarField --------------------------------------------------
        /// Key in GpuSceneSlot::Buffers identifying the float scalar buffer.
        /// e.g. "geodesic", "curvature", "scalars".
        std::string ScalarFieldName;

        /// Colourmap configuration (map type, range, isolines, binning).
        ScalarFieldConfig Scalar;

        /// Which element domain the scalar buffer addresses.
        enum class Domain : std::uint8_t
        {
            Vertex = 0,
            Edge   = 1,
            Face   = 2,
        } ScalarDomain = Domain::Vertex;

        // ----- PerDomainBuffer ----------------------------------------------
        /// Key in GpuSceneSlot::Buffers identifying the RGBA colour buffer.
        /// Used when Source is PerVertex/Edge/FaceBuffer.
        /// e.g. "colors", "edge_colors", "face_colors".
        std::string ColorBufferName;
    };
}

