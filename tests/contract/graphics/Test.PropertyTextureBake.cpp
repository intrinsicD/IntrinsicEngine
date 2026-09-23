#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.PropertyTextureBake;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    // Atlas mesh assembled in texel coordinates for readable raster cases.
    struct TexelAtlas
    {
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::vector<glm::vec2> Texcoords{};
        std::vector<std::uint32_t> Indices{};

        // Axis-aligned quad split along its (x0, y1)-(x1, y0) anti-diagonal.
        // Returns the first slot index.
        std::uint32_t AddQuad(
            const float x0,
            const float y0,
            const float x1,
            const float y1)
        {
            const auto base = static_cast<std::uint32_t>(Texcoords.size());
            const auto uv = [this](const float x, const float y)
            {
                return glm::vec2{
                    x / static_cast<float>(Width),
                    y / static_cast<float>(Height),
                };
            };
            Texcoords.push_back(uv(x0, y0));
            Texcoords.push_back(uv(x1, y0));
            Texcoords.push_back(uv(x1, y1));
            Texcoords.push_back(uv(x0, y1));
            Indices.insert(
                Indices.end(),
                {base + 0u, base + 1u, base + 3u,
                 base + 1u, base + 2u, base + 3u});
            return base;
        }

        void AddTriangle(const glm::vec2 a, const glm::vec2 b, const glm::vec2 c)
        {
            const auto base = static_cast<std::uint32_t>(Texcoords.size());
            for (const glm::vec2 texel : {a, b, c})
            {
                Texcoords.push_back(glm::vec2{
                    texel.x / static_cast<float>(Width),
                    texel.y / static_cast<float>(Height),
                });
            }
            Indices.insert(Indices.end(), {base, base + 1u, base + 2u});
        }
    };

    [[nodiscard]] std::size_t TexelIndex(
        const Graphics::PropertyTextureBakeReferenceImage& image,
        const std::uint32_t x,
        const std::uint32_t y)
    {
        return static_cast<std::size_t>(y) * image.Width + x;
    }

    [[nodiscard]] Graphics::PropertyTextureBakeReferenceImage Rasterize(
        const TexelAtlas& atlas,
        const Graphics::PropertyTextureBakeCharts& charts,
        const std::vector<Graphics::PropertyTextureBakeGutterEdge>& edges,
        const std::vector<glm::vec4>& values,
        const std::uint32_t padding,
        const Graphics::PropertyTextureBakeDomain domain =
            Graphics::PropertyTextureBakeDomain::Vertex,
        const Graphics::PropertyTextureBakeValueKind kind =
            Graphics::PropertyTextureBakeValueKind::Scalar,
        const Graphics::PropertyTextureBakeEncoding encoding =
            Graphics::PropertyTextureBakeEncoding::Raw)
    {
        auto image = Graphics::RasterizePropertyTextureBakeReference(
            Graphics::PropertyTextureBakeReferenceInput{
                .Texcoords = atlas.Texcoords,
                .Values = values,
                .Indices = atlas.Indices,
                .Charts = &charts,
                .GutterEdges = edges,
                .Width = atlas.Width,
                .Height = atlas.Height,
                .PaddingTexels = padding,
                .Domain = domain,
                .ValueKind = kind,
                .Encoding = encoding,
            });
        EXPECT_TRUE(image.has_value());
        return image.has_value()
            ? std::move(*image)
            : Graphics::PropertyTextureBakeReferenceImage{};
    }

    [[nodiscard]] Graphics::PropertyTextureBakeRecordDesc MakeRecordDesc()
    {
        return Graphics::PropertyTextureBakeRecordDesc{
            .Pipeline = RHI::PipelineHandle{4u, 1u},
            .OutputTexture = RHI::TextureHandle{5u, 1u},
            .CoverageTexture = RHI::TextureHandle{7u, 1u},
            .DepthTexture = RHI::TextureHandle{8u, 1u},
            .IndexBuffer = RHI::BufferHandle{6u, 1u},
            .TexcoordBDA = 0x1100u,
            .PropertyBDA = 0x2200u,
            .IndexBDA = 0x3300u,
            .ChartBDA = 0x4400u,
            .GutterEdgeBDA = 0x5500u,
            .IndexCount = 6u,
            .GutterEdgeCount = 4u,
            .Width = 32u,
            .Height = 16u,
            .PaddingTexels = 2u,
            .RangeMin = -2.0f,
            .RangeMax = 6.0f,
            .FinalLayout = RHI::TextureLayout::TransferSrc,
        };
    }
}

TEST(PropertyTextureBake, PipelineWritesValueCoverageAndOrderingDepth)
{
    const RHI::PipelineDesc desc =
        Graphics::MakePropertyTextureBakePipelineDesc(
            "property.vert.spv",
            "property.frag.spv",
            RHI::Format::R32_FLOAT);

    EXPECT_EQ(desc.VertexShaderPath, "property.vert.spv");
    EXPECT_EQ(desc.FragmentShaderPath, "property.frag.spv");
    EXPECT_EQ(desc.Rasterizer.Culling, RHI::CullMode::None);
    EXPECT_TRUE(desc.DepthStencil.DepthTestEnable);
    EXPECT_TRUE(desc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(desc.DepthStencil.DepthFunc, RHI::DepthOp::Less);
    EXPECT_EQ(desc.DepthTargetFormat, RHI::Format::D32_FLOAT);
    ASSERT_EQ(desc.ColorTargetCount, 2u);
    EXPECT_EQ(desc.ColorTargetFormats[0], RHI::Format::R32_FLOAT);
    EXPECT_EQ(
        desc.ColorTargetFormats[1],
        Graphics::kPropertyTextureBakeCoverageFormat);
    EXPECT_FALSE(desc.ColorBlend[0].Enable);
    EXPECT_FALSE(desc.ColorBlend[1].Enable);
    EXPECT_EQ(
        desc.PushConstantSize,
        sizeof(Graphics::PropertyTextureBakePushConstants));

    const RHI::TextureDesc coverage =
        Graphics::MakePropertyTextureBakeCoverageTextureDesc(32u, 16u);
    EXPECT_EQ(coverage.Width, 32u);
    EXPECT_EQ(coverage.Height, 16u);
    EXPECT_EQ(coverage.MipLevels, 1u);
    EXPECT_EQ(coverage.Fmt, RHI::Format::R32_FLOAT);
    const RHI::TextureDesc depth =
        Graphics::MakePropertyTextureBakeDepthTextureDesc(32u, 16u);
    EXPECT_EQ(depth.Fmt, RHI::Format::D32_FLOAT);
    EXPECT_NE(
        static_cast<std::uint32_t>(depth.Usage) &
            static_cast<std::uint32_t>(RHI::TextureUsage::DepthTarget),
        0u);
}

TEST(PropertyTextureBake, InvalidResourcesFailBeforeRecordingCommands)
{
    const auto expectRejected =
        [](const Graphics::PropertyTextureBakeRecordDesc& desc)
    {
        Tests::MockCommandContext commands{};
        const Core::Result result =
            Graphics::RecordPropertyTextureBake(commands, desc);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), Core::ErrorCode::InvalidArgument);
        EXPECT_TRUE(commands.Events.empty());
        EXPECT_TRUE(commands.TextureBarrierCalls.empty());
    };

    auto desc = MakeRecordDesc();
    desc.Encoding = Graphics::PropertyTextureBakeEncoding::ScalarColormap;
    expectRejected(desc);

    desc = MakeRecordDesc();
    desc.IndexCount = 4u;
    expectRejected(desc);

    desc = MakeRecordDesc();
    desc.CoverageTexture = desc.OutputTexture;
    expectRejected(desc);

    desc = MakeRecordDesc();
    desc.DepthTexture = {};
    expectRejected(desc);

    desc = MakeRecordDesc();
    desc.ChartBDA = 0u;
    expectRejected(desc);

    desc = MakeRecordDesc();
    desc.GutterEdgeBDA = 0u;
    expectRejected(desc);

    desc = MakeRecordDesc();
    desc.PaddingTexels = Graphics::kPropertyTextureBakeMaxPaddingTexels + 1u;
    expectRejected(desc);
}

TEST(PropertyTextureBake, RecordsInteriorAndGutterDrawsInOneDepthOrderedPass)
{
    Tests::MockCommandContext commands{};
    const Graphics::PropertyTextureBakeRecordDesc desc = MakeRecordDesc();

    ASSERT_TRUE(
        Graphics::RecordPropertyTextureBake(commands, desc).has_value());
    ASSERT_EQ(commands.TextureBarrierCalls.size(), 5u);
    EXPECT_EQ(commands.TextureBarrierCalls[0].Texture, desc.OutputTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls[0].After,
        RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(commands.TextureBarrierCalls[1].Texture, desc.CoverageTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls[1].After,
        RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(commands.TextureBarrierCalls[2].Texture, desc.DepthTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls[2].After,
        RHI::TextureLayout::DepthAttachment);
    EXPECT_EQ(commands.TextureBarrierCalls[3].Texture, desc.OutputTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls[3].After,
        RHI::TextureLayout::TransferSrc);
    EXPECT_EQ(commands.TextureBarrierCalls[4].Texture, desc.CoverageTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls[4].After,
        RHI::TextureLayout::ShaderReadOnly);

    EXPECT_EQ(commands.BindPipelineCalls, 1);
    EXPECT_EQ(commands.LastIndexBuffer, desc.IndexBuffer);
    EXPECT_EQ(commands.DrawIndexedCalls, 1);
    EXPECT_EQ(commands.LastDrawIndexed.IndexCount, desc.IndexCount);
    EXPECT_EQ(commands.DrawCalls, 1);
    EXPECT_EQ(commands.LastDraw.VertexCount, desc.GutterEdgeCount * 6u);
    EXPECT_TRUE(commands.SampledTextureBindings.empty());

    ASSERT_EQ(commands.PushConstantPayloads.size(), 2u);
    for (std::size_t pass = 0u; pass < 2u; ++pass)
    {
        ASSERT_EQ(
            commands.PushConstantPayloads[pass].size(),
            sizeof(Graphics::PropertyTextureBakePushConstants));
        Graphics::PropertyTextureBakePushConstants push{};
        std::memcpy(
            &push,
            commands.PushConstantPayloads[pass].data(),
            sizeof(push));
        EXPECT_EQ(push.Mode, static_cast<std::uint32_t>(pass));
        EXPECT_EQ(push.TexcoordBDA, desc.TexcoordBDA);
        EXPECT_EQ(push.PropertyBDA, desc.PropertyBDA);
        EXPECT_EQ(push.IndexBDA, desc.IndexBDA);
        EXPECT_EQ(push.ChartBDA, desc.ChartBDA);
        EXPECT_EQ(push.GutterEdgeBDA, desc.GutterEdgeBDA);
        EXPECT_EQ(push.PaddingTexels, desc.PaddingTexels);
        EXPECT_EQ(push.Width, desc.Width);
        EXPECT_EQ(push.Height, desc.Height);
        EXPECT_FLOAT_EQ(push.RangeMin, desc.RangeMin);
        EXPECT_FLOAT_EQ(push.RangeMax, desc.RangeMax);
    }
}

TEST(PropertyTextureBake, ZeroPaddingOrClosedAtlasRecordsOnlyTheInteriorDraw)
{
    for (const bool zeroPadding : {true, false})
    {
        Tests::MockCommandContext commands{};
        auto desc = MakeRecordDesc();
        if (zeroPadding)
            desc.PaddingTexels = 0u;
        else
            desc.GutterEdgeCount = 0u;
        desc.GutterEdgeBDA = 0u;
        ASSERT_TRUE(
            Graphics::RecordPropertyTextureBake(commands, desc).has_value());
        EXPECT_EQ(commands.DrawIndexedCalls, 1);
        EXPECT_EQ(commands.DrawCalls, 0);
        EXPECT_EQ(commands.PushConstantPayloads.size(), 1u);
    }
}

TEST(PropertyTextureBake, ChartsFollowSharedSlotEdgesAndSeamsBecomeGutterEdges)
{
    // One quad sharing its diagonal slots: one chart, four boundary edges.
    const std::vector<std::uint32_t> shared{0u, 1u, 3u, 1u, 2u, 3u};
    const auto sharedCharts = Graphics::BuildPropertyTextureBakeCharts(shared);
    EXPECT_EQ(sharedCharts.ChartCount, 1u);
    EXPECT_EQ(
        sharedCharts.TriangleChart,
        (std::vector<std::uint32_t>{0u, 0u}));
    const auto sharedEdges =
        Graphics::BuildPropertyTextureBakeGutterEdges(shared);
    ASSERT_EQ(sharedEdges.size(), 4u);
    for (const auto& edge : sharedEdges)
    {
        EXPECT_FALSE(
            (edge.A == 1u && edge.B == 3u) || (edge.A == 3u && edge.B == 1u))
            << "the shared diagonal is interior, not a gutter edge";
        const std::uint32_t base = edge.Primitive * 3u;
        EXPECT_EQ(shared[base + (edge.Side + 1u) % 3u], edge.A);
        EXPECT_EQ(shared[base + (edge.Side + 2u) % 3u], edge.B);
    }

    // A UV seam duplicates the diagonal's slots: two charts, six edges.
    const std::vector<std::uint32_t> seam{0u, 1u, 3u, 4u, 2u, 5u};
    const auto seamCharts = Graphics::BuildPropertyTextureBakeCharts(seam);
    EXPECT_EQ(seamCharts.ChartCount, 2u);
    EXPECT_EQ(
        seamCharts.TriangleChart,
        (std::vector<std::uint32_t>{0u, 1u}));
    EXPECT_EQ(Graphics::BuildPropertyTextureBakeGutterEdges(seam).size(), 6u);
}

TEST(PropertyTextureBake, CoverageCountsSharedEdgeTexelCentresExactlyOnce)
{
    TexelAtlas atlas{.Width = 8u, .Height = 8u};
    // Integer corners: the anti-diagonal x + y = 8 passes exactly through the
    // texel centres (3.5, 4.5), (4.5, 3.5), ...
    atlas.AddQuad(2.0f, 2.0f, 6.0f, 6.0f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    const auto report = Graphics::MeasurePropertyTextureBakeCoverage(
        atlas.Texcoords, atlas.Indices, charts, atlas.Width, atlas.Height);
    EXPECT_EQ(report.CoveredTexels, 16u);
    EXPECT_EQ(report.OverlapTexels, 0u);
    EXPECT_EQ(report.ChartCount, 1u);
    EXPECT_EQ(report.UnderresolvedChartCount, 0u);
}

TEST(PropertyTextureBake, CoverageReportsOverlapAndUnderresolvedCharts)
{
    TexelAtlas overlapping{.Width = 8u, .Height = 8u};
    overlapping.AddQuad(1.0f, 1.0f, 5.0f, 5.0f);
    overlapping.AddQuad(3.0f, 3.0f, 7.0f, 7.0f);
    const auto overlapCharts =
        Graphics::BuildPropertyTextureBakeCharts(overlapping.Indices);
    const auto overlap = Graphics::MeasurePropertyTextureBakeCoverage(
        overlapping.Texcoords,
        overlapping.Indices,
        overlapCharts,
        overlapping.Width,
        overlapping.Height);
    EXPECT_EQ(overlap.ChartCount, 2u);
    // Centres (3..4, 3..4) lie strictly inside both charts.
    EXPECT_EQ(overlap.OverlapTexels, 4u);

    TexelAtlas tiny{.Width = 8u, .Height = 8u};
    tiny.AddQuad(1.0f, 1.0f, 5.0f, 5.0f);
    // A valid chart lying between texel centres covers none of them.
    tiny.AddTriangle({6.1f, 6.1f}, {6.4f, 6.1f}, {6.1f, 6.4f});
    const auto tinyCharts = Graphics::BuildPropertyTextureBakeCharts(tiny.Indices);
    const auto underresolved = Graphics::MeasurePropertyTextureBakeCoverage(
        tiny.Texcoords, tiny.Indices, tinyCharts, tiny.Width, tiny.Height);
    EXPECT_EQ(underresolved.OverlapTexels, 0u);
    EXPECT_EQ(underresolved.CoveredTexels, 16u);
    EXPECT_EQ(underresolved.UnderresolvedChartCount, 1u);
    EXPECT_EQ(underresolved.FirstUnderresolvedChart, 1u);
}

TEST(PropertyTextureBake, ReferenceKeepsRawZeroAndNegativeValuesDistinctFromUncovered)
{
    TexelAtlas atlas{.Width = 12u, .Height = 8u};
    atlas.AddQuad(1.0f, 1.0f, 5.0f, 7.0f);
    atlas.AddQuad(7.0f, 1.0f, 11.0f, 7.0f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    std::vector<glm::vec4> values(8u, glm::vec4{-3.5f, 0.0f, 0.0f, 1.0f});
    for (std::size_t slot = 4u; slot < 8u; ++slot)
        values[slot] = glm::vec4{0.0f};

    const auto image = Rasterize(atlas, charts, {}, values, 0u);
    std::size_t negative = 0u;
    std::size_t zero = 0u;
    for (std::uint32_t y = 0u; y < atlas.Height; ++y)
    {
        for (std::uint32_t x = 0u; x < atlas.Width; ++x)
        {
            const std::size_t index = TexelIndex(image, x, y);
            if (image.Coverage[index] == 1.0f)
            {
                EXPECT_NEAR(image.Values[index].x, -3.5f, 1.0e-6f);
                ++negative;
            }
            else if (image.Coverage[index] == 2.0f)
            {
                EXPECT_EQ(image.Values[index], glm::vec4{0.0f});
                ++zero;
            }
            else
            {
                EXPECT_EQ(image.Coverage[index], 0.0f);
            }
        }
    }
    EXPECT_EQ(negative, 24u);
    EXPECT_EQ(zero, 24u);
}

TEST(PropertyTextureBake, ReferenceInterpolatesAffineVertexFieldsExactly)
{
    TexelAtlas atlas{.Width = 16u, .Height = 16u};
    atlas.AddQuad(2.25f, 3.0f, 13.25f, 12.75f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    const auto field = [](const glm::vec2 uv)
    {
        return 2.0f * uv.x - 3.0f * uv.y + 0.25f;
    };
    std::vector<glm::vec4> values{};
    for (const glm::vec2 uv : atlas.Texcoords)
        values.push_back(glm::vec4{field(uv), -field(uv), 0.0f, 1.0f});

    const auto image = Rasterize(
        atlas, charts, {}, values, 0u,
        Graphics::PropertyTextureBakeDomain::Vertex,
        Graphics::PropertyTextureBakeValueKind::Vector2);
    std::size_t covered = 0u;
    for (std::uint32_t y = 0u; y < atlas.Height; ++y)
    {
        for (std::uint32_t x = 0u; x < atlas.Width; ++x)
        {
            const std::size_t index = TexelIndex(image, x, y);
            if (image.Coverage[index] <= 0.0f)
                continue;
            const glm::vec2 centre{
                (static_cast<float>(x) + 0.5f) / 16.0f,
                (static_cast<float>(y) + 0.5f) / 16.0f,
            };
            EXPECT_NEAR(image.Values[index].x, field(centre), 1.0e-5f);
            EXPECT_NEAR(image.Values[index].y, -field(centre), 1.0e-5f);
            ++covered;
        }
    }
    EXPECT_EQ(covered, 11u * 10u);
}

TEST(PropertyTextureBake, ReferenceKeepsFaceLabelsDiscreteAcrossTheSharedEdge)
{
    TexelAtlas atlas{.Width = 8u, .Height = 8u};
    atlas.AddQuad(0.0f, 0.0f, 8.0f, 8.0f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    const std::vector<glm::vec4> labels{
        glm::vec4{std::bit_cast<float>(3u), 0.0f, 0.0f, 1.0f},
        glm::vec4{std::bit_cast<float>(7u), 0.0f, 0.0f, 1.0f},
    };
    const auto image = Rasterize(
        atlas, charts, {}, labels, 0u,
        Graphics::PropertyTextureBakeDomain::Face,
        Graphics::PropertyTextureBakeValueKind::Label,
        Graphics::PropertyTextureBakeEncoding::LabelPalette);

    // Triangle 0 lies below the anti-diagonal x + y = 8 in texel space.
    const glm::vec4 first = image.Values[TexelIndex(image, 1u, 1u)];
    const glm::vec4 second = image.Values[TexelIndex(image, 6u, 6u)];
    EXPECT_NE(first, second);
    EXPECT_EQ(first.w, 1.0f);
    for (std::uint32_t y = 0u; y < 8u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            const glm::vec4 value = image.Values[TexelIndex(image, x, y)];
            if (x + y < 7u)
                EXPECT_EQ(value, first) << x << "," << y;
            else if (x + y > 7u)
                EXPECT_EQ(value, second) << x << "," << y;
            else
                EXPECT_TRUE(value == first || value == second)
                    << "shared-edge texel blended two labels";
        }
    }
}

TEST(PropertyTextureBake, ReferenceGutterTakesTheNearestChartWithoutMixing)
{
    TexelAtlas atlas{.Width = 12u, .Height = 12u};
    atlas.AddQuad(1.0f, 1.0f, 4.0f, 7.0f);
    atlas.AddQuad(6.0f, 1.0f, 9.0f, 7.0f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    const auto edges = Graphics::BuildPropertyTextureBakeGutterEdges(atlas.Indices);
    ASSERT_EQ(edges.size(), 8u);
    std::vector<glm::vec4> values(8u, glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f});
    for (std::size_t slot = 4u; slot < 8u; ++slot)
        values[slot] = glm::vec4{5.0f, 0.0f, 0.0f, 0.0f};

    const auto image = Rasterize(atlas, charts, edges, values, 2u);
    for (std::uint32_t y = 0u; y < atlas.Height; ++y)
    {
        for (std::uint32_t x = 0u; x < atlas.Width; ++x)
        {
            const std::size_t index = TexelIndex(image, x, y);
            const float coverage = image.Coverage[index];
            const float value = image.Values[index].x;
            if (coverage == 0.0f)
            {
                EXPECT_EQ(value, 0.0f);
                continue;
            }
            const float chart = std::abs(coverage);
            EXPECT_EQ(value, chart == 1.0f ? -1.0f : 5.0f)
                << "texel " << x << "," << y << " mixed charts";
        }
    }
    // The two-texel gap splits by distance: x = 4 belongs to the left chart,
    // x = 5 to the right one; both are gutter, not interior.
    EXPECT_EQ(image.Coverage[TexelIndex(image, 4u, 3u)], -1.0f);
    EXPECT_EQ(image.Coverage[TexelIndex(image, 5u, 3u)], -2.0f);
    EXPECT_EQ(image.Coverage[TexelIndex(image, 10u, 3u)], -2.0f);
    // Chebyshev padding reaches diagonally but no farther than two texels.
    EXPECT_EQ(image.Coverage[TexelIndex(image, 10u, 8u)], -2.0f);
    EXPECT_EQ(image.Coverage[TexelIndex(image, 11u, 3u)], 0.0f);
    EXPECT_EQ(image.Coverage[TexelIndex(image, 3u, 10u)], 0.0f);
}

TEST(PropertyTextureBake, ReferenceGutterExtrapolatesFromTheNearestBoundaryPoint)
{
    TexelAtlas atlas{.Width = 16u, .Height = 16u};
    atlas.AddQuad(2.0f, 2.0f, 10.0f, 10.0f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    const auto edges = Graphics::BuildPropertyTextureBakeGutterEdges(atlas.Indices);
    // f = texel x + 10 * texel y, affine in UV.
    std::vector<glm::vec4> values{};
    for (const glm::vec2 uv : atlas.Texcoords)
    {
        values.push_back(glm::vec4{
            uv.x * 16.0f + 10.0f * uv.y * 16.0f, 0.0f, 0.0f, 1.0f});
    }
    const auto image = Rasterize(atlas, charts, edges, values, 2u);

    // Centre (11.5, 5.5) is 1.5 texels right of the x = 10 edge and at least
    // 3.5 from the others; its nearest boundary point is (10, 5.5).
    const std::size_t right = TexelIndex(image, 11u, 5u);
    EXPECT_EQ(image.Coverage[right], -1.0f);
    EXPECT_NEAR(image.Values[right].x, 10.0f + 55.0f, 1.0e-4f);
    // Centre (5.5, 0.5) sits 1.5 texels above the y = 2 edge.
    const std::size_t top = TexelIndex(image, 5u, 0u);
    EXPECT_EQ(image.Coverage[top], -1.0f);
    EXPECT_NEAR(image.Values[top].x, 5.5f + 20.0f, 1.0e-4f);
}

TEST(PropertyTextureBake, ReferenceRejectsLutEncodingsAndMismatchedValues)
{
    TexelAtlas atlas{.Width = 4u, .Height = 4u};
    atlas.AddQuad(0.0f, 0.0f, 4.0f, 4.0f);
    const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
    const std::vector<glm::vec4> values(4u, glm::vec4{1.0f});
    Graphics::PropertyTextureBakeReferenceInput input{
        .Texcoords = atlas.Texcoords,
        .Values = values,
        .Indices = atlas.Indices,
        .Charts = &charts,
        .Width = 4u,
        .Height = 4u,
        .Encoding = Graphics::PropertyTextureBakeEncoding::ScalarColormap,
    };
    EXPECT_FALSE(
        Graphics::RasterizePropertyTextureBakeReference(input).has_value());
    input.Encoding = Graphics::PropertyTextureBakeEncoding::Raw;
    input.Domain = Graphics::PropertyTextureBakeDomain::Face;
    EXPECT_FALSE(
        Graphics::RasterizePropertyTextureBakeReference(input).has_value());
}
