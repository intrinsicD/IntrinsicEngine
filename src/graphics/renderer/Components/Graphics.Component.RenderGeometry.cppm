module;

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <variant>

export module Extrinsic.Graphics.Component.RenderGeometry;

import Extrinsic.Graphics.Colormap;

// ============================================================
// Render-geometry hint components.
//
// These describe *how* to render an entity's geometry (topology,
// primitive mode, size/width sources) but say nothing about colour.
// Colour is governed by:
//   1. VisualizationConfig  — scientific-visualisation overrides
//                             (scalar fields, colormaps, isolation lines)
//   2. MaterialInstance     — baseline PBR / custom material appearance
//
// Render components compose per geometric domain. Meshes may carry
// RenderSurface, RenderEdges, and/or RenderPoints; graphs may carry
// RenderEdges and/or RenderPoints; point clouds may carry RenderPoints.
// Component presence is the toggle — no boolean flags.
// ============================================================

export namespace Extrinsic::Graphics::Components
{
    // ----------------------------------------------------------------
    // ScalarFieldConfig — colourmap + range + isolines + binning.
    //
    // Pure CPU metadata; no GPU resource owned.
    // Consumed by VisualizationConfig to configure GPU rendering.
    // ----------------------------------------------------------------
    struct ScalarFieldConfig
    {
        Colormap::Type Map      = Colormap::Type::Viridis;
        float          RangeMin = 0.f;
        float          RangeMax = 1.f;

        /// When true, VisualizationSyncSystem derives RangeMin/Max from the
        /// scalar buffer's actual data range on each dirty frame.
        bool AutoRange = true;

        /// Discretise the scalar range into N equal-width bands.
        /// 0 = continuous (smooth gradient).
        std::uint32_t BinCount = 0;

        struct Isolines
        {
            /// Number of evenly-spaced isocontour lines.  0 = none.
            std::uint32_t Num   = 0;
            glm::vec4     Color = {0.f, 0.f, 0.f, 1.f};
            float         Width = 1.5f;  ///< Screen-space pixels.
        } Isolines;
    };

    // ----------------------------------------------------------------
    // RenderSurface — filled triangle / quad rendering hint.
    // ----------------------------------------------------------------
    struct RenderSurface
    {
        enum class SourceDomain : std::uint8_t
        {
            Vertex,  ///< Interpolate colour / normals from per-vertex data.
            Face     ///< Constant colour / flat-shaded per face.
        } Domain = SourceDomain::Vertex;
    };

    // ----------------------------------------------------------------
    // RenderEdges — thick anti-aliased edge rendering hint.
    // ----------------------------------------------------------------
    struct RenderEdges
    {
        enum class SourceDomain : std::uint8_t
        {
            Vertex,  ///< Colour interpolated across edge endpoints.
            Edge     ///< Colour constant per edge.
        } Domain = SourceDomain::Vertex;

        /// Uniform edge width or name of a per-edge
        /// width buffer in GpuSceneSlot::Buffers.
        std::variant<float, std::string> WidthSource = 1.0f;
    };

    // ----------------------------------------------------------------
    // RenderPoints — point sprite / impostor sphere rendering hint.
    // ----------------------------------------------------------------
    struct RenderPoints
    {
        enum class RenderType : std::uint8_t
        {
            Flat,    ///< Screen-aligned flat disc.
            Sphere,  ///< Impostor sphere — correct depth, occludes surfaces.
            Surfel   ///< Surface-aligned disc; requires vertex normals.
        } Type = RenderType::Sphere;

        /// Uniform screen-space diameter in pixels or name of a per-point size buffer
        /// in GpuSceneSlot::Buffers.
        std::variant<float, std::string> SizeSource = 6.0f;
    };
}
