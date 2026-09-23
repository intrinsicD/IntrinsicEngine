// Records the GPU property-to-atlas texture bake and defines its CPU reference
// raster contract (coverage, chart identity, chart-aware gutters).
module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.PropertyTextureBake;

import Extrinsic.Core.Error;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t
        kPropertyTextureBakeMaxPaddingTexels = 32u;

    // Raster contract shared by the GPU pass and the CPU reference:
    // - texel (x, y) samples atlas point ((x + 0.5) / W, (y + 0.5) / H);
    // - the coverage target stores +(chart + 1) for texel centres inside a
    //   triangle, -(chart + 1) for gutter texels and 0 for uncovered texels, so
    //   coverage never depends on the value channels (zero and negative raw
    //   data stay legitimate);
    // - a gutter texel lies within Chebyshev distance `PaddingTexels` of a
    //   chart boundary edge and takes the value at its nearest point on the
    //   edge nearest by chebyshev + euclidean / 64 (first drawn edge on exact
    //   ties), so a gutter texel carries exactly one chart's value;
    // - output textures have one mip level. Gutters make linear filtering
    //   chart-safe for footprints within `PaddingTexels`; they do not make
    //   mip reduction safe.
    inline constexpr RHI::Format kPropertyTextureBakeCoverageFormat =
        RHI::Format::R32_FLOAT;
    inline constexpr RHI::Format kPropertyTextureBakeDepthFormat =
        RHI::Format::D32_FLOAT;
    // Chart ids plus one must stay exactly representable in float coverage.
    inline constexpr std::uint32_t kPropertyTextureBakeMaxCharts =
        (1u << 24u) - 1u;

    enum class PropertyTextureBakeDomain : std::uint32_t
    {
        Vertex = 0u,
        Face = 1u,
        NearestEdge = 2u,
    };

    enum class PropertyTextureBakeValueKind : std::uint32_t
    {
        Scalar = 0u,
        Label = 1u,
        Vector2 = 2u,
        Vector3 = 3u,
        Vector4 = 4u,
    };

    enum class PropertyTextureBakeEncoding : std::uint32_t
    {
        Raw = 0u,
        Normal = 1u,
        RgbaColor = 2u,
        ScalarColormap = 3u,
        LabelPalette = 4u,
        LinearScalar = 5u,
    };

    enum class PropertyTextureBakePassMode : std::uint32_t
    {
        Interior = 0u,
        Gutter = 1u,
    };

    struct alignas(8) PropertyTextureBakePushConstants
    {
        std::uint64_t TexcoordBDA{0u};
        std::uint64_t PropertyBDA{0u};
        std::uint64_t IndexBDA{0u};
        std::uint64_t ChartBDA{0u};
        std::uint64_t GutterEdgeBDA{0u};
        std::uint32_t Domain{0u};
        std::uint32_t ValueKind{0u};
        std::uint32_t Encoding{0u};
        std::uint32_t ColormapID{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t Mode{0u};
        std::uint32_t PaddingTexels{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
    };
    static_assert(sizeof(PropertyTextureBakePushConstants) == 80u);

    // One chart boundary edge (an index-buffer edge used by exactly one
    // triangle). `Side` is the triangle-local edge opposite corner `Side`.
    struct PropertyTextureBakeGutterEdge
    {
        std::uint32_t A{0u};
        std::uint32_t B{0u};
        std::uint32_t Primitive{0u};
        std::uint32_t Side{0u};
    };
    static_assert(sizeof(PropertyTextureBakeGutterEdge) == 16u);

    struct PropertyTextureBakeRecordDesc
    {
        RHI::PipelineHandle Pipeline{};
        RHI::TextureHandle OutputTexture{};
        RHI::TextureHandle CoverageTexture{};
        RHI::TextureHandle DepthTexture{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t TexcoordBDA{0u};
        std::uint64_t PropertyBDA{0u};
        std::uint64_t IndexBDA{0u};
        std::uint64_t ChartBDA{0u};
        std::uint64_t GutterEdgeBDA{0u};
        std::uint32_t FirstIndex{0u};
        std::uint32_t IndexCount{0u};
        std::uint32_t GutterEdgeCount{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t PaddingTexels{0u};
        PropertyTextureBakeDomain Domain{PropertyTextureBakeDomain::Vertex};
        PropertyTextureBakeValueKind ValueKind{
            PropertyTextureBakeValueKind::Scalar};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Raw};
        std::uint32_t ColormapID{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        RHI::TextureLayout InitialLayout{RHI::TextureLayout::Undefined};
        RHI::TextureLayout FinalLayout{RHI::TextureLayout::ShaderReadOnly};
        RHI::TextureLayout CoverageInitialLayout{
            RHI::TextureLayout::Undefined};
        RHI::TextureLayout CoverageFinalLayout{
            RHI::TextureLayout::ShaderReadOnly};
        RHI::TextureLayout DepthInitialLayout{RHI::TextureLayout::Undefined};
    };

    [[nodiscard]] RHI::PipelineDesc MakePropertyTextureBakePipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        RHI::Format colorFormat);

    [[nodiscard]] RHI::TextureDesc MakePropertyTextureBakeCoverageTextureDesc(
        std::uint32_t width,
        std::uint32_t height,
        const char* debugName = "PropertyTextureBake.Coverage") noexcept;

    [[nodiscard]] RHI::TextureDesc MakePropertyTextureBakeDepthTextureDesc(
        std::uint32_t width,
        std::uint32_t height,
        const char* debugName = "PropertyTextureBake.Depth") noexcept;

    // Records the interior raster and, when padding is requested, the gutter
    // draw into the output/coverage targets. Fails before recording when the
    // resources or parameters violate the raster contract.
    [[nodiscard]] Core::Result RecordPropertyTextureBake(
        RHI::ICommandContext& commandContext,
        const PropertyTextureBakeRecordDesc& desc);

    struct PropertyTextureBakeCharts
    {
        // One chart id per triangle; charts are edge-connected triangle
        // components of the index buffer, numbered by first triangle.
        std::vector<std::uint32_t> TriangleChart{};
        std::uint32_t ChartCount{0u};
    };

    [[nodiscard]] PropertyTextureBakeCharts BuildPropertyTextureBakeCharts(
        std::span<const std::uint32_t> indices);

    // Boundary edges in triangle order, corner order within a triangle.
    [[nodiscard]] std::vector<PropertyTextureBakeGutterEdge>
        BuildPropertyTextureBakeGutterEdges(
            std::span<const std::uint32_t> indices);

    struct PropertyTextureBakeCoverageReport
    {
        std::uint64_t CoveredTexels{0u};
        // Texel centres strictly inside two triangles (ties on shared edges
        // are not overlap).
        std::uint64_t OverlapTexels{0u};
        std::uint32_t ChartCount{0u};
        // Charts whose triangles cover no texel centre at this extent.
        std::uint32_t UnderresolvedChartCount{0u};
        std::uint32_t FirstUnderresolvedChart{0u};
    };

    // Measures the exact interior coverage with one bit per texel.
    [[nodiscard]] PropertyTextureBakeCoverageReport
        MeasurePropertyTextureBakeCoverage(
            std::span<const glm::vec2> texcoords,
            std::span<const std::uint32_t> indices,
            const PropertyTextureBakeCharts& charts,
            std::uint32_t width,
            std::uint32_t height);

    struct PropertyTextureBakeReferenceInput
    {
        std::span<const glm::vec2> Texcoords{};
        // Vertex: one per texcoord slot; Face: one per triangle;
        // NearestEdge: three per triangle (edge opposite each corner).
        std::span<const glm::vec4> Values{};
        std::span<const std::uint32_t> Indices{};
        const PropertyTextureBakeCharts* Charts{};
        std::span<const PropertyTextureBakeGutterEdge> GutterEdges{};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t PaddingTexels{0u};
        PropertyTextureBakeDomain Domain{PropertyTextureBakeDomain::Vertex};
        PropertyTextureBakeValueKind ValueKind{
            PropertyTextureBakeValueKind::Scalar};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Raw};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
    };

    struct PropertyTextureBakeReferenceImage
    {
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        // Pre-quantization output values, row-major, row 0 at v = 0.
        std::vector<glm::vec4> Values{};
        std::vector<float> Coverage{};
    };

    // Small-extent CPU reference of the GPU pass for tests. Scalar colormap
    // encoding depends on a GPU LUT and is rejected.
    [[nodiscard]] Core::Expected<PropertyTextureBakeReferenceImage>
        RasterizePropertyTextureBakeReference(
            const PropertyTextureBakeReferenceInput& input);
}
