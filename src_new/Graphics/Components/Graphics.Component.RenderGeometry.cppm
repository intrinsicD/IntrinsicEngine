module;

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <variant>

export module Extrinsic.Graphics.Component.RenderGeometry;

import Extrinsic.Graphics.Colormap;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics::Components
{
    // Visualization config — describes HOW to display a scalar field.
    // Does not own any GPU resource; the actual buffer is looked up at
    // render extraction time via GpuSceneSlot::Find(BufferName).
    struct ScalarFieldConfig
    {
        Colormap::Type Map     = Colormap::Type::Viridis;
        float RangeMin         = 0.0f;
        float RangeMax         = 1.0f;
        bool  AutoRange        = true;

        struct Bins     { uint32_t Num = 0; } CachedBins;
        struct Isolines
        {
            uint32_t  Num   = 0;
            glm::vec4 Color = {0.0f, 0.0f, 0.0f, 1.0f};
            float     Width = 1.5f;
        } CachedIsolines;

        std::optional<std::variant<Bins, Isolines>> ActiveBinsOrIsolines;
    };

    // References a scalar field buffer uploaded by the lifecycle system.
    // BufferName is looked up in GpuSceneSlot::Find() at extraction time.
    struct ScalarFieldDataSource
    {
        std::string     BufferName;   // e.g. "scalars", "curvature", "geodesic"
        ScalarFieldConfig Config;
    };

    // Either a uniform colour or the name of a per-element colour buffer
    // uploaded to GpuSceneSlot (e.g. "colors").
    struct ColorDataSource
    {
        std::variant<glm::vec4, std::string> UniformOrBufferName =
            glm::vec4{1.0f, 0.6f, 0.0f, 1.0f};
    };

    // Render points as flat discs, impostor spheres, or surface-aligned surfels.
    struct RenderPoints
    {
        enum class RenderType : uint8_t
        {
            Flat,
            Sphere,  // impostor sphere — correct depth, occludes surfaces
            Surfel   // requires vertex normals
        } Type = RenderType::Sphere;

        std::variant<ColorDataSource, ScalarFieldDataSource> VertexColorOrScalarField;

        // Uniform float radius (world-space) or name of a per-point size buffer.
        std::variant<float, std::string> VertexSizeDataSource = 0.008f;
    };

    // Render lines as thick anti-aliased edges.
    // Color can be vertex-interpolated or edge-discrete; width uniform or varying.
    struct RenderLines
    {
        enum class SourceDomain : uint8_t
        {
            Vertex,  // colour interpolated across edge from per-vertex values
            Edge     // colour constant per edge from per-edge values
        } Domain = SourceDomain::Vertex;

        std::variant<ColorDataSource, ScalarFieldDataSource> VertexColorOrScalarField;
        std::variant<ColorDataSource, ScalarFieldDataSource> EdgeColorOrScalarField;

        // Uniform float width or name of a per-edge width buffer.
        std::variant<float, std::string> EdgeWidthDataSource = 0.008f;
    };

    // Render filled triangles / quads.
    struct RenderSurface
    {
        enum class SourceDomain : uint8_t
        {
            Vertex,
            Face
        } Domain = SourceDomain::Vertex;

        std::variant<ColorDataSource, ScalarFieldDataSource> VertexColorOrScalarField;
        std::variant<ColorDataSource, ScalarFieldDataSource> FaceColorOrScalarField;
    };
}
