module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.PropertyTextureBake;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        void TransitionTexture(
            RHI::ICommandContext& commandContext,
            const RHI::TextureHandle texture,
            const RHI::TextureLayout before,
            const RHI::TextureLayout after)
        {
            if (before != after)
                commandContext.TextureBarrier(texture, before, after);
        }

        [[nodiscard]] PropertyTextureBakePushConstants MakePushConstants(
            const PropertyTextureBakeRecordDesc& desc,
            const PropertyTextureBakePassMode mode) noexcept
        {
            return PropertyTextureBakePushConstants{
                .TexcoordBDA = desc.TexcoordBDA,
                .PropertyBDA = desc.PropertyBDA,
                .IndexBDA = desc.IndexBDA,
                .ChartBDA = desc.ChartBDA,
                .GutterEdgeBDA = desc.GutterEdgeBDA,
                .Domain = static_cast<std::uint32_t>(desc.Domain),
                .ValueKind = static_cast<std::uint32_t>(desc.ValueKind),
                .Encoding = static_cast<std::uint32_t>(desc.Encoding),
                .ColormapID = desc.ColormapID,
                .RangeMin = desc.RangeMin,
                .RangeMax = desc.RangeMax,
                .Mode = static_cast<std::uint32_t>(mode),
                .PaddingTexels = desc.PaddingTexels,
                .Width = desc.Width,
                .Height = desc.Height,
            };
        }

        [[nodiscard]] std::uint64_t EdgeKey(
            std::uint32_t a,
            std::uint32_t b) noexcept
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<std::uint64_t>(a) << 32u) |
                   static_cast<std::uint64_t>(b);
        }

        [[nodiscard]] std::uint32_t FindRoot(
            std::vector<std::uint32_t>& parent,
            std::uint32_t node) noexcept
        {
            while (parent[node] != node)
            {
                parent[node] = parent[parent[node]];
                node = parent[node];
            }
            return node;
        }

        // Texel-space triangle with the shared, exactly antisymmetric edge
        // function. Adjacent triangles evaluate a shared edge from the same
        // canonical endpoint order, so a texel centre on that edge belongs to
        // exactly one of them under the tie rule below.
        struct TexelTriangle
        {
            std::array<glm::dvec2, 3u> P{};
            double Sign{0.0};
            double AbsoluteArea{0.0};
        };

        [[nodiscard]] double EdgeFunction(
            const glm::dvec2 a,
            const glm::dvec2 b,
            const glm::dvec2 p) noexcept
        {
            const bool swapped = b.x < a.x || (b.x == a.x && b.y < a.y);
            const glm::dvec2 lo = swapped ? b : a;
            const glm::dvec2 hi = swapped ? a : b;
            const double value = (hi.x - lo.x) * (p.y - lo.y) -
                                 (hi.y - lo.y) * (p.x - lo.x);
            return swapped ? -value : value;
        }

        [[nodiscard]] glm::dvec2 ToTexel(
            const glm::vec2 uv,
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            return glm::dvec2{
                static_cast<double>(uv.x) * static_cast<double>(width),
                static_cast<double>(uv.y) * static_cast<double>(height),
            };
        }

        [[nodiscard]] TexelTriangle MakeTexelTriangle(
            const std::span<const glm::vec2> texcoords,
            const std::span<const std::uint32_t> indices,
            const std::size_t triangle,
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            TexelTriangle result{};
            for (std::size_t corner = 0u; corner < 3u; ++corner)
            {
                const std::uint32_t index = indices[triangle * 3u + corner];
                if (index >= texcoords.size())
                    return TexelTriangle{};
                result.P[corner] = ToTexel(texcoords[index], width, height);
            }
            const double area =
                EdgeFunction(result.P[0], result.P[1], result.P[2]);
            if (!(std::abs(area) > 0.0) || !std::isfinite(area))
                return TexelTriangle{};
            result.Sign = area > 0.0 ? 1.0 : -1.0;
            result.AbsoluteArea = std::abs(area);
            return result;
        }

        struct TexelSample
        {
            bool Inside{false};
            // Barycentric weights of corners 0, 1, 2.
            std::array<double, 3u> Weights{};
            // Smallest distance to an edge, in texels.
            double Margin{0.0};
        };

        [[nodiscard]] TexelSample SampleTriangle(
            const TexelTriangle& triangle,
            const glm::dvec2 p) noexcept
        {
            TexelSample sample{};
            if (triangle.Sign == 0.0)
                return sample;
            sample.Margin = std::numeric_limits<double>::infinity();
            constexpr std::array<std::array<std::size_t, 3u>, 3u> kEdges{{
                {1u, 2u, 0u},
                {2u, 0u, 1u},
                {0u, 1u, 2u},
            }};
            for (const auto& edge : kEdges)
            {
                const glm::dvec2 a = triangle.P[edge[0]];
                const glm::dvec2 b = triangle.P[edge[1]];
                const double w = EdgeFunction(a, b, p) * triangle.Sign;
                if (w < 0.0)
                    return TexelSample{};
                if (w == 0.0)
                {
                    const glm::dvec2 direction = (b - a) * triangle.Sign;
                    const bool owns = direction.y > 0.0 ||
                        (direction.y == 0.0 && direction.x < 0.0);
                    if (!owns)
                        return TexelSample{};
                }
                sample.Weights[edge[2]] = w / triangle.AbsoluteArea;
                const double length = glm::length(b - a);
                sample.Margin = std::min(
                    sample.Margin,
                    length > 0.0 ? w / length : 0.0);
            }
            sample.Inside = true;
            return sample;
        }

        struct TexelRange
        {
            std::uint32_t Begin{0u};
            std::uint32_t End{0u};
        };

        // Texel columns/rows whose centres lie within [lo, hi].
        [[nodiscard]] TexelRange CentreRange(
            const double lo,
            const double hi,
            const std::uint32_t extent) noexcept
        {
            const double first = std::ceil(lo - 0.5);
            const double last = std::floor(hi - 0.5);
            if (!(last >= 0.0) || !(first < static_cast<double>(extent)) ||
                last < first)
            {
                return {};
            }
            return TexelRange{
                .Begin = static_cast<std::uint32_t>(std::max(first, 0.0)),
                .End = static_cast<std::uint32_t>(std::min(
                           last,
                           static_cast<double>(extent) - 1.0)) +
                       1u,
            };
        }

        template <class Visit>
        void ForEachCoveredTexel(
            const TexelTriangle& triangle,
            const std::uint32_t width,
            const std::uint32_t height,
            Visit&& visit)
        {
            if (triangle.Sign == 0.0)
                return;
            const double minX = std::min(
                {triangle.P[0].x, triangle.P[1].x, triangle.P[2].x});
            const double maxX = std::max(
                {triangle.P[0].x, triangle.P[1].x, triangle.P[2].x});
            const double minY = std::min(
                {triangle.P[0].y, triangle.P[1].y, triangle.P[2].y});
            const double maxY = std::max(
                {triangle.P[0].y, triangle.P[1].y, triangle.P[2].y});
            const TexelRange columns = CentreRange(minX, maxX, width);
            const TexelRange rows = CentreRange(minY, maxY, height);
            for (std::uint32_t y = rows.Begin; y < rows.End; ++y)
            {
                for (std::uint32_t x = columns.Begin; x < columns.End; ++x)
                {
                    const TexelSample sample = SampleTriangle(
                        triangle,
                        glm::dvec2{
                            static_cast<double>(x) + 0.5,
                            static_cast<double>(y) + 0.5,
                        });
                    if (sample.Inside)
                        visit(x, y, sample);
                }
            }
        }

        // Chebyshev distance from `p` to segment [a, b]. The minimum of this
        // convex piecewise-linear function lies at an endpoint, an axis zero,
        // or a |dx| == |dy| crossing; the shader evaluates the same candidates.
        [[nodiscard]] double ChebyshevSegmentDistance(
            const glm::dvec2 p,
            const glm::dvec2 a,
            const glm::dvec2 b) noexcept
        {
            const glm::dvec2 d = b - a;
            const glm::dvec2 o = a - p;
            const auto at = [&](const double t) noexcept
            {
                return std::max(
                    std::abs(o.x + t * d.x),
                    std::abs(o.y + t * d.y));
            };
            double best = std::min(at(0.0), at(1.0));
            const auto consider = [&](const double numerator,
                                      const double denominator) noexcept
            {
                if (denominator == 0.0)
                    return;
                const double t = numerator / denominator;
                if (t > 0.0 && t < 1.0)
                    best = std::min(best, at(t));
            };
            consider(-o.x, d.x);
            consider(-o.y, d.y);
            consider(o.y - o.x, d.x - d.y);
            consider(-(o.x + o.y), d.x + d.y);
            return best;
        }

        // Gutter ordering key: Chebyshev distance with a Euclidean tie-break,
        // so equal-Chebyshev candidates resolve to the geometrically nearer edge and
        // remaining exact ties project to the same shared corner.
        [[nodiscard]] double GutterDepth(
            const double chebyshev,
            const double euclidean,
            const double padding) noexcept
        {
            return (chebyshev + euclidean / 64.0 + 1.0) /
                   (padding + padding / 32.0 + 2.0);
        }

        [[nodiscard]] double ProjectOntoSegment(
            const glm::dvec2 p,
            const glm::dvec2 a,
            const glm::dvec2 b) noexcept
        {
            const glm::dvec2 d = b - a;
            const double lengthSquared = glm::dot(d, d);
            return lengthSquared > 0.0
                ? std::clamp(glm::dot(p - a, d) / lengthSquared, 0.0, 1.0)
                : 0.0;
        }

        [[nodiscard]] glm::vec3 LabelColor(const std::uint32_t label) noexcept
        {
            std::uint32_t hash = label * 747796405u + 2891336453u;
            hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
            hash = (hash >> 22u) ^ hash;
            return glm::vec3{
                static_cast<float>((hash >> 0u) & 255u),
                static_cast<float>((hash >> 8u) & 255u),
                static_cast<float>((hash >> 16u) & 255u),
            } / 255.0f;
        }

        // Mirrors the fragment encoder. `value.xyz` of interpolated vertex
        // normals is pre-scaled by 1/8 exactly like the vertex shader does.
        [[nodiscard]] glm::vec4 EncodeReferenceValue(
            const PropertyTextureBakeReferenceInput& input,
            const glm::vec4 value) noexcept
        {
            switch (input.Encoding)
            {
            case PropertyTextureBakeEncoding::Raw:
                return value;
            case PropertyTextureBakeEncoding::Normal:
            {
                const float scale = std::max(
                    std::abs(value.x),
                    std::max(std::abs(value.y), std::abs(value.z)));
                const glm::vec3 scaled =
                    glm::vec3{value} / std::max(scale, 1.0e-20f);
                const float scaledLength = glm::length(scaled);
                const float cutoff =
                    input.Domain == PropertyTextureBakeDomain::Vertex
                        ? 1.25e-7f
                        : 1.0e-6f;
                const glm::vec3 normal =
                    scaledLength > 0.0f && scale > cutoff / scaledLength
                        ? scaled / scaledLength
                        : glm::vec3{0.0f, 0.0f, 1.0f};
                return glm::vec4{normal * 0.5f + 0.5f, 1.0f};
            }
            case PropertyTextureBakeEncoding::LabelPalette:
                return glm::vec4{
                    LabelColor(std::bit_cast<std::uint32_t>(value.x)),
                    1.0f};
            case PropertyTextureBakeEncoding::LinearScalar:
            {
                const float t = (value.x - input.RangeMin) /
                                (input.RangeMax - input.RangeMin);
                return glm::vec4{std::clamp(t, 0.0f, 1.0f), 0.0f, 0.0f, 1.0f};
            }
            case PropertyTextureBakeEncoding::RgbaColor:
                return glm::clamp(value, glm::vec4{0.0f}, glm::vec4{1.0f});
            case PropertyTextureBakeEncoding::ScalarColormap:
                break;
            }
            return glm::vec4{0.0f};
        }

        [[nodiscard]] glm::vec4 ScaledVertexValue(
            const PropertyTextureBakeReferenceInput& input,
            glm::vec4 value) noexcept
        {
            if (input.Domain == PropertyTextureBakeDomain::Vertex &&
                input.Encoding == PropertyTextureBakeEncoding::Normal)
            {
                value = glm::vec4{glm::vec3{value} * 0.125f, value.w};
            }
            return value;
        }

        [[nodiscard]] double UvDistanceSquared(
            const glm::dvec2 a,
            const glm::dvec2 b) noexcept
        {
            const glm::dvec2 delta = a - b;
            return glm::dot(delta, delta);
        }

        [[nodiscard]] double UvSegmentDistanceSquared(
            const glm::dvec2 p,
            const glm::dvec2 a,
            const glm::dvec2 b) noexcept
        {
            const double t = ProjectOntoSegment(p, a, b);
            return UvDistanceSquared(p, a + t * (b - a));
        }
    }

    RHI::PipelineDesc MakePropertyTextureBakePipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        const RHI::Format colorFormat)
    {
        RHI::PipelineDesc desc{};
        desc.VertexShaderPath = std::move(vertexShaderPath);
        desc.FragmentShaderPath = std::move(fragmentShaderPath);
        desc.Rasterizer.Culling = RHI::CullMode::None;
        // Interior triangles write depth 0 and gutter fragments write a depth
        // that grows with boundary distance, so interior data always wins and
        // the nearest chart boundary owns each gutter texel.
        desc.DepthStencil.DepthTestEnable = true;
        desc.DepthStencil.DepthWriteEnable = true;
        desc.DepthStencil.DepthFunc = RHI::DepthOp::Less;
        desc.DepthTargetFormat = kPropertyTextureBakeDepthFormat;
        desc.ColorTargetCount = 2u;
        desc.ColorTargetFormats[0] = colorFormat;
        desc.ColorTargetFormats[1] = kPropertyTextureBakeCoverageFormat;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(PropertyTextureBakePushConstants));
        desc.DebugName = "PropertyTextureBake";
        return desc;
    }

    RHI::TextureDesc MakePropertyTextureBakeCoverageTextureDesc(
        const std::uint32_t width,
        const std::uint32_t height,
        const char* const debugName) noexcept
    {
        return RHI::TextureDesc{
            .Width = width,
            .Height = height,
            .MipLevels = 1u,
            .Fmt = kPropertyTextureBakeCoverageFormat,
            .Usage = RHI::TextureUsage::Sampled |
                     RHI::TextureUsage::ColorTarget |
                     RHI::TextureUsage::TransferSrc,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .DebugName = debugName != nullptr
                ? debugName
                : "PropertyTextureBake.Coverage",
        };
    }

    RHI::TextureDesc MakePropertyTextureBakeDepthTextureDesc(
        const std::uint32_t width,
        const std::uint32_t height,
        const char* const debugName) noexcept
    {
        return RHI::TextureDesc{
            .Width = width,
            .Height = height,
            .MipLevels = 1u,
            .Fmt = kPropertyTextureBakeDepthFormat,
            .Usage = RHI::TextureUsage::DepthTarget,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .DebugName = debugName != nullptr
                ? debugName
                : "PropertyTextureBake.Depth",
        };
    }

    Core::Result RecordPropertyTextureBake(
        RHI::ICommandContext& commandContext,
        const PropertyTextureBakeRecordDesc& desc)
    {
        const bool drawGutter =
            desc.PaddingTexels != 0u && desc.GutterEdgeCount != 0u;
        if (!desc.Pipeline.IsValid() ||
            !desc.OutputTexture.IsValid() ||
            !desc.CoverageTexture.IsValid() ||
            !desc.DepthTexture.IsValid() ||
            desc.OutputTexture == desc.CoverageTexture ||
            desc.OutputTexture == desc.DepthTexture ||
            desc.CoverageTexture == desc.DepthTexture ||
            !desc.IndexBuffer.IsValid() ||
            desc.TexcoordBDA == 0u ||
            desc.PropertyBDA == 0u ||
            desc.IndexBDA == 0u ||
            desc.ChartBDA == 0u ||
            desc.IndexCount == 0u ||
            (desc.IndexCount % 3u) != 0u ||
            desc.Width == 0u ||
            desc.Height == 0u ||
            desc.PaddingTexels > kPropertyTextureBakeMaxPaddingTexels ||
            (drawGutter && desc.GutterEdgeBDA == 0u) ||
            (drawGutter &&
             desc.GutterEdgeCount >
                 std::numeric_limits<std::uint32_t>::max() / 6u) ||
            (desc.Encoding == PropertyTextureBakeEncoding::ScalarColormap &&
             desc.ColormapID == 0u) ||
            !(desc.RangeMin < desc.RangeMax))
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        TransitionTexture(
            commandContext,
            desc.OutputTexture,
            desc.InitialLayout,
            RHI::TextureLayout::ColorAttachment);
        TransitionTexture(
            commandContext,
            desc.CoverageTexture,
            desc.CoverageInitialLayout,
            RHI::TextureLayout::ColorAttachment);
        TransitionTexture(
            commandContext,
            desc.DepthTexture,
            desc.DepthInitialLayout,
            RHI::TextureLayout::DepthAttachment);

        const std::array<RHI::ColorAttachment, 2u> attachments{{
            RHI::ColorAttachment{
                .Target = desc.OutputTexture,
                .Load = RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::Store,
                .ClearR = 0.0f,
                .ClearG = 0.0f,
                .ClearB = 0.0f,
                .ClearA = 0.0f,
            },
            RHI::ColorAttachment{
                .Target = desc.CoverageTexture,
                .Load = RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::Store,
                .ClearR = 0.0f,
                .ClearG = 0.0f,
                .ClearB = 0.0f,
                .ClearA = 0.0f,
            },
        }};
        commandContext.BeginRenderPass(RHI::RenderPassDesc{
            .ColorTargets = attachments,
            .Depth = RHI::DepthAttachment{
                .Target = desc.DepthTexture,
                .Load = RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::DontCare,
                .ClearDepth = 1.0f,
            },
        });
        commandContext.SetViewport(
            0.0f,
            0.0f,
            static_cast<float>(desc.Width),
            static_cast<float>(desc.Height),
            0.0f,
            1.0f);
        commandContext.SetScissor(0, 0, desc.Width, desc.Height);
        commandContext.BindPipeline(desc.Pipeline);
        commandContext.BindIndexBuffer(
            desc.IndexBuffer,
            0u,
            RHI::IndexType::Uint32);

        const PropertyTextureBakePushConstants interior =
            MakePushConstants(desc, PropertyTextureBakePassMode::Interior);
        commandContext.PushConstants(
            &interior,
            static_cast<std::uint32_t>(sizeof(interior)),
            0u);
        commandContext.DrawIndexed(
            desc.IndexCount,
            1u,
            desc.FirstIndex,
            0,
            0u);

        if (drawGutter)
        {
            const PropertyTextureBakePushConstants gutter =
                MakePushConstants(desc, PropertyTextureBakePassMode::Gutter);
            commandContext.PushConstants(
                &gutter,
                static_cast<std::uint32_t>(sizeof(gutter)),
                0u);
            commandContext.Draw(desc.GutterEdgeCount * 6u, 1u, 0u, 0u);
        }
        commandContext.EndRenderPass();

        TransitionTexture(
            commandContext,
            desc.OutputTexture,
            RHI::TextureLayout::ColorAttachment,
            desc.FinalLayout);
        TransitionTexture(
            commandContext,
            desc.CoverageTexture,
            RHI::TextureLayout::ColorAttachment,
            desc.CoverageFinalLayout);
        return Core::Ok();
    }

    PropertyTextureBakeCharts BuildPropertyTextureBakeCharts(
        const std::span<const std::uint32_t> indices)
    {
        const std::size_t triangleCount = indices.size() / 3u;
        std::vector<std::uint32_t> parent(triangleCount);
        std::iota(parent.begin(), parent.end(), 0u);
        std::unordered_map<std::uint64_t, std::uint32_t> firstOwner{};
        firstOwner.reserve(indices.size());
        for (std::uint32_t triangle = 0u; triangle < triangleCount; ++triangle)
        {
            for (std::uint32_t side = 0u; side < 3u; ++side)
            {
                const std::uint32_t a =
                    indices[triangle * 3u + (side + 1u) % 3u];
                const std::uint32_t b =
                    indices[triangle * 3u + (side + 2u) % 3u];
                if (a == b)
                    continue;
                const auto [owner, inserted] =
                    firstOwner.try_emplace(EdgeKey(a, b), triangle);
                if (inserted)
                    continue;
                const std::uint32_t lhs = FindRoot(parent, owner->second);
                const std::uint32_t rhs = FindRoot(parent, triangle);
                if (lhs != rhs)
                    parent[std::max(lhs, rhs)] = std::min(lhs, rhs);
            }
        }

        PropertyTextureBakeCharts charts{};
        charts.TriangleChart.resize(triangleCount);
        std::vector<std::uint32_t> chartForRoot(
            triangleCount,
            std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t triangle = 0u; triangle < triangleCount; ++triangle)
        {
            const std::uint32_t root = FindRoot(parent, triangle);
            if (chartForRoot[root] == std::numeric_limits<std::uint32_t>::max())
                chartForRoot[root] = charts.ChartCount++;
            charts.TriangleChart[triangle] = chartForRoot[root];
        }
        return charts;
    }

    std::vector<PropertyTextureBakeGutterEdge>
        BuildPropertyTextureBakeGutterEdges(
            const std::span<const std::uint32_t> indices)
    {
        const std::size_t triangleCount = indices.size() / 3u;
        std::unordered_map<std::uint64_t, std::uint32_t> uses{};
        uses.reserve(indices.size());
        for (std::size_t index = 0u; index < triangleCount * 3u; index += 3u)
        {
            for (std::uint32_t side = 0u; side < 3u; ++side)
            {
                const std::uint32_t a = indices[index + (side + 1u) % 3u];
                const std::uint32_t b = indices[index + (side + 2u) % 3u];
                if (a != b)
                    ++uses[EdgeKey(a, b)];
            }
        }

        std::vector<PropertyTextureBakeGutterEdge> edges{};
        for (std::uint32_t triangle = 0u; triangle < triangleCount; ++triangle)
        {
            for (std::uint32_t side = 0u; side < 3u; ++side)
            {
                const std::uint32_t a =
                    indices[triangle * 3u + (side + 1u) % 3u];
                const std::uint32_t b =
                    indices[triangle * 3u + (side + 2u) % 3u];
                if (a == b || uses[EdgeKey(a, b)] != 1u)
                    continue;
                edges.push_back(PropertyTextureBakeGutterEdge{
                    .A = a,
                    .B = b,
                    .Primitive = triangle,
                    .Side = side,
                });
            }
        }
        return edges;
    }

    PropertyTextureBakeCoverageReport MeasurePropertyTextureBakeCoverage(
        const std::span<const glm::vec2> texcoords,
        const std::span<const std::uint32_t> indices,
        const PropertyTextureBakeCharts& charts,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        // Centres closer than this to an edge of the second triangle are
        // treated as shared-edge ties rather than overlap.
        constexpr double kOverlapMarginTexels = 1.0e-4;

        PropertyTextureBakeCoverageReport report{};
        report.ChartCount = charts.ChartCount;
        const std::size_t triangleCount = indices.size() / 3u;
        if (width == 0u || height == 0u ||
            charts.TriangleChart.size() != triangleCount)
        {
            return report;
        }

        const std::size_t texelCount =
            static_cast<std::size_t>(width) * height;
        std::vector<std::uint64_t> covered((texelCount + 63u) / 64u, 0u);
        std::vector<std::uint8_t> chartCovered(charts.ChartCount, 0u);
        for (std::size_t triangle = 0u; triangle < triangleCount; ++triangle)
        {
            const TexelTriangle texel = MakeTexelTriangle(
                texcoords, indices, triangle, width, height);
            const std::uint32_t chart = charts.TriangleChart[triangle];
            ForEachCoveredTexel(
                texel,
                width,
                height,
                [&](const std::uint32_t x,
                    const std::uint32_t y,
                    const TexelSample& sample)
                {
                    const std::size_t index =
                        static_cast<std::size_t>(y) * width + x;
                    std::uint64_t& word = covered[index / 64u];
                    const std::uint64_t bit = std::uint64_t{1u}
                                              << (index % 64u);
                    if ((word & bit) != 0u)
                    {
                        if (sample.Margin > kOverlapMarginTexels)
                            ++report.OverlapTexels;
                        return;
                    }
                    word |= bit;
                    ++report.CoveredTexels;
                    if (chart < chartCovered.size())
                        chartCovered[chart] = 1u;
                });
        }

        for (std::uint32_t chart = 0u; chart < charts.ChartCount; ++chart)
        {
            if (chartCovered[chart] != 0u)
                continue;
            if (report.UnderresolvedChartCount == 0u)
                report.FirstUnderresolvedChart = chart;
            ++report.UnderresolvedChartCount;
        }
        return report;
    }

    Core::Expected<PropertyTextureBakeReferenceImage>
        RasterizePropertyTextureBakeReference(
            const PropertyTextureBakeReferenceInput& input)
    {
        const std::size_t triangleCount = input.Indices.size() / 3u;
        const std::size_t expectedValues =
            input.Domain == PropertyTextureBakeDomain::Vertex
                ? input.Texcoords.size()
                : input.Domain == PropertyTextureBakeDomain::Face
                    ? triangleCount
                    : triangleCount * 3u;
        if (input.Width == 0u || input.Height == 0u ||
            input.Charts == nullptr ||
            input.Charts->TriangleChart.size() != triangleCount ||
            (input.Indices.size() % 3u) != 0u ||
            input.Values.size() != expectedValues ||
            input.PaddingTexels > kPropertyTextureBakeMaxPaddingTexels ||
            input.Encoding == PropertyTextureBakeEncoding::ScalarColormap ||
            !(input.RangeMin < input.RangeMax) ||
            std::ranges::any_of(
                input.Indices,
                [&](const std::uint32_t index)
                {
                    return index >= input.Texcoords.size();
                }))
        {
            return Core::Err<PropertyTextureBakeReferenceImage>(
                Core::ErrorCode::InvalidArgument);
        }

        PropertyTextureBakeReferenceImage image{
            .Width = input.Width,
            .Height = input.Height,
        };
        const std::size_t texelCount =
            static_cast<std::size_t>(input.Width) * input.Height;
        image.Values.assign(texelCount, glm::vec4{0.0f});
        image.Coverage.assign(texelCount, 0.0f);
        std::vector<double> depth(texelCount, 1.0);
        const glm::dvec2 extent{
            static_cast<double>(input.Width),
            static_cast<double>(input.Height),
        };
        const bool label =
            input.ValueKind == PropertyTextureBakeValueKind::Label;

        for (std::size_t triangle = 0u; triangle < triangleCount; ++triangle)
        {
            const TexelTriangle texel = MakeTexelTriangle(
                input.Texcoords,
                input.Indices,
                triangle,
                input.Width,
                input.Height);
            const std::array<std::uint32_t, 3u> corners{
                input.Indices[triangle * 3u + 0u],
                input.Indices[triangle * 3u + 1u],
                input.Indices[triangle * 3u + 2u],
            };
            const float chart =
                static_cast<float>(input.Charts->TriangleChart[triangle]) +
                1.0f;
            ForEachCoveredTexel(
                texel,
                input.Width,
                input.Height,
                [&](const std::uint32_t x,
                    const std::uint32_t y,
                    const TexelSample& sample)
                {
                    const std::size_t index =
                        static_cast<std::size_t>(y) * input.Width + x;
                    if (depth[index] <= 0.0)
                        return;
                    // Nearest-vertex/edge lookups run in UV space, as the
                    // fragment shader does.
                    const glm::dvec2 uv =
                        glm::dvec2{
                            static_cast<double>(x) + 0.5,
                            static_cast<double>(y) + 0.5,
                        } /
                        extent;
                    const std::array<glm::dvec2, 3u> cornerUv{
                        texel.P[0] / extent,
                        texel.P[1] / extent,
                        texel.P[2] / extent,
                    };
                    glm::vec4 value{0.0f};
                    if (input.Domain == PropertyTextureBakeDomain::Face)
                    {
                        value = input.Values[triangle];
                    }
                    else if (input.Domain ==
                             PropertyTextureBakeDomain::NearestEdge)
                    {
                        const std::array<double, 3u> distances{
                            UvSegmentDistanceSquared(
                                uv, cornerUv[1], cornerUv[2]),
                            UvSegmentDistanceSquared(
                                uv, cornerUv[2], cornerUv[0]),
                            UvSegmentDistanceSquared(
                                uv, cornerUv[0], cornerUv[1]),
                        };
                        const std::size_t side =
                            distances[0] <= distances[1] &&
                                    distances[0] <= distances[2]
                                ? 0u
                                : (distances[1] <= distances[2] ? 1u : 2u);
                        value = input.Values[triangle * 3u + side];
                    }
                    else if (label)
                    {
                        const std::array<double, 3u> distances{
                            UvDistanceSquared(uv, cornerUv[0]),
                            UvDistanceSquared(uv, cornerUv[1]),
                            UvDistanceSquared(uv, cornerUv[2]),
                        };
                        const std::size_t corner =
                            distances[0] <= distances[1] &&
                                    distances[0] <= distances[2]
                                ? 0u
                                : (distances[1] <= distances[2] ? 1u : 2u);
                        value = input.Values[corners[corner]];
                    }
                    else
                    {
                        glm::dvec4 interpolated{0.0};
                        for (std::size_t corner = 0u; corner < 3u; ++corner)
                        {
                            interpolated +=
                                sample.Weights[corner] *
                                glm::dvec4{ScaledVertexValue(
                                    input,
                                    input.Values[corners[corner]])};
                        }
                        value = glm::vec4{interpolated};
                    }
                    image.Values[index] = EncodeReferenceValue(input, value);
                    image.Coverage[index] = chart;
                    depth[index] = 0.0;
                });
        }

        if (input.PaddingTexels == 0u)
            return image;

        const double padding = static_cast<double>(input.PaddingTexels);
        for (const PropertyTextureBakeGutterEdge& edge : input.GutterEdges)
        {
            if (edge.A >= input.Texcoords.size() ||
                edge.B >= input.Texcoords.size() ||
                edge.Primitive >= triangleCount ||
                edge.Side >= 3u)
            {
                return Core::Err<PropertyTextureBakeReferenceImage>(
                    Core::ErrorCode::InvalidArgument);
            }
            const glm::dvec2 a =
                ToTexel(input.Texcoords[edge.A], input.Width, input.Height);
            const glm::dvec2 b =
                ToTexel(input.Texcoords[edge.B], input.Width, input.Height);
            const TexelRange columns = CentreRange(
                std::min(a.x, b.x) - padding,
                std::max(a.x, b.x) + padding,
                input.Width);
            const TexelRange rows = CentreRange(
                std::min(a.y, b.y) - padding,
                std::max(a.y, b.y) + padding,
                input.Height);
            const float chart =
                -(static_cast<float>(
                      input.Charts->TriangleChart[edge.Primitive]) +
                  1.0f);
            for (std::uint32_t y = rows.Begin; y < rows.End; ++y)
            {
                for (std::uint32_t x = columns.Begin; x < columns.End; ++x)
                {
                    const std::size_t index =
                        static_cast<std::size_t>(y) * input.Width + x;
                    const glm::dvec2 p{
                        static_cast<double>(x) + 0.5,
                        static_cast<double>(y) + 0.5,
                    };
                    const double distance =
                        ChebyshevSegmentDistance(p, a, b);
                    if (distance > padding)
                        continue;
                    const double t = ProjectOntoSegment(p, a, b);
                    const double candidateDepth = GutterDepth(
                        distance,
                        glm::length(p - (a + t * (b - a))),
                        padding);
                    if (!(candidateDepth < depth[index]))
                        continue;

                    glm::vec4 value{0.0f};
                    if (input.Domain == PropertyTextureBakeDomain::Face)
                        value = input.Values[edge.Primitive];
                    else if (input.Domain ==
                             PropertyTextureBakeDomain::NearestEdge)
                        value = input.Values[edge.Primitive * 3u + edge.Side];
                    else if (label)
                        value = input.Values[t < 0.5 ? edge.A : edge.B];
                    else
                        value = glm::vec4{glm::mix(
                            glm::dvec4{ScaledVertexValue(
                                input, input.Values[edge.A])},
                            glm::dvec4{ScaledVertexValue(
                                input, input.Values[edge.B])},
                            t)};
                    image.Values[index] = EncodeReferenceValue(input, value);
                    image.Coverage[index] = chart;
                    depth[index] = candidateDepth;
                }
            }
        }
        return image;
    }
}
