module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.SpatialDebugVisualizers;

import Extrinsic.Graphics.RenderWorld;

namespace Extrinsic::Graphics
{
    export enum class SpatialDebugSplitAxis : std::uint8_t
    {
        X = 0,
        Y = 1,
        Z = 2,
    };

    export struct SpatialDebugAabb
    {
        glm::vec3 Min{0.f};
        glm::vec3 Max{0.f};
    };

    export struct SpatialDebugHierarchyNode
    {
        SpatialDebugAabb Bounds{};
        std::uint32_t Depth{0u};
        bool IsLeaf{true};
    };

    export struct SpatialDebugSplitPlane
    {
        SpatialDebugAabb Bounds{};
        SpatialDebugSplitAxis Axis{SpatialDebugSplitAxis::X};
        float Position{0.f};
    };

    export struct SpatialDebugWireEdge
    {
        std::uint32_t A{0u};
        std::uint32_t B{0u};
    };

    export struct SpatialDebugVisualizerOptions
    {
        std::uint32_t MaxLinePackets{4096u};
        std::uint32_t MaxPointPackets{4096u};
        std::uint32_t MaxDepth{32u};
        float LineWidth{1.f};
        float PointRadius{0.01f};
        glm::vec4 BranchColor{1.f, 0.85f, 0.1f, 1.f};
        glm::vec4 LeafColor{0.25f, 0.9f, 1.f, 1.f};
        glm::vec4 SplitPlaneColor{0.9f, 0.25f, 1.f, 1.f};
        glm::vec4 HullColor{0.4f, 1.f, 0.4f, 1.f};
        bool DepthTested{true};
    };

    export struct SpatialDebugVisualizerDiagnostics
    {
        std::uint32_t InputRecordCount{0u};
        std::uint32_t EmittedLineCount{0u};
        std::uint32_t EmittedPointCount{0u};
        std::uint32_t RejectedInvalidBoundsCount{0u};
        std::uint32_t RejectedInvalidCoordinateCount{0u};
        std::uint32_t RejectedDepthLimitCount{0u};
        std::uint32_t RejectedTopologyCount{0u};
        bool TruncatedLineBudget{false};
        bool TruncatedPointBudget{false};
    };

    export struct SpatialDebugPacketResult
    {
        std::vector<DebugLinePacket> Lines{};
        std::vector<DebugPointPacket> Points{};
        std::vector<DebugTrianglePacket> Triangles{};
        SpatialDebugVisualizerDiagnostics Diagnostics{};
    };

    namespace Detail
    {
        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] bool IsValid(const SpatialDebugAabb& bounds) noexcept
        {
            return IsFinite(bounds.Min) && IsFinite(bounds.Max) &&
                   bounds.Min.x <= bounds.Max.x &&
                   bounds.Min.y <= bounds.Max.y &&
                   bounds.Min.z <= bounds.Max.z;
        }

        [[nodiscard]] bool CanAppendLine(const SpatialDebugVisualizerOptions& options,
                                         SpatialDebugVisualizerDiagnostics& diagnostics,
                                         const std::vector<DebugLinePacket>& lines) noexcept
        {
            if (lines.size() < options.MaxLinePackets)
            {
                return true;
            }
            diagnostics.TruncatedLineBudget = true;
            return false;
        }

        [[nodiscard]] std::array<glm::vec3, 8> Corners(const SpatialDebugAabb& bounds) noexcept
        {
            return {
                glm::vec3{bounds.Min.x, bounds.Min.y, bounds.Min.z},
                glm::vec3{bounds.Max.x, bounds.Min.y, bounds.Min.z},
                glm::vec3{bounds.Max.x, bounds.Max.y, bounds.Min.z},
                glm::vec3{bounds.Min.x, bounds.Max.y, bounds.Min.z},
                glm::vec3{bounds.Min.x, bounds.Min.y, bounds.Max.z},
                glm::vec3{bounds.Max.x, bounds.Min.y, bounds.Max.z},
                glm::vec3{bounds.Max.x, bounds.Max.y, bounds.Max.z},
                glm::vec3{bounds.Min.x, bounds.Max.y, bounds.Max.z},
            };
        }

        void AppendLine(SpatialDebugPacketResult& result,
                        const SpatialDebugVisualizerOptions& options,
                        const glm::vec3 start,
                        const glm::vec3 end,
                        const glm::vec4 color)
        {
            if (!CanAppendLine(options, result.Diagnostics, result.Lines))
            {
                return;
            }
            result.Lines.push_back(DebugLinePacket{
                .Start = start,
                .End = end,
                .Color = color,
                .Width = std::max(options.LineWidth, 0.0001f),
                .DepthTested = options.DepthTested,
            });
            ++result.Diagnostics.EmittedLineCount;
        }

        void AppendAabb(SpatialDebugPacketResult& result,
                        const SpatialDebugVisualizerOptions& options,
                        const SpatialDebugAabb& bounds,
                        const glm::vec4 color)
        {
            constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 12> kEdges{{
                {0u, 1u}, {1u, 2u}, {2u, 3u}, {3u, 0u},
                {4u, 5u}, {5u, 6u}, {6u, 7u}, {7u, 4u},
                {0u, 4u}, {1u, 5u}, {2u, 6u}, {3u, 7u},
            }};

            const auto corners = Corners(bounds);
            for (const auto [a, b] : kEdges)
            {
                AppendLine(result, options, corners[a], corners[b], color);
            }
        }

        [[nodiscard]] bool IsValidSplitPosition(const SpatialDebugSplitPlane& plane) noexcept
        {
            if (!std::isfinite(plane.Position))
            {
                return false;
            }

            switch (plane.Axis)
            {
            case SpatialDebugSplitAxis::X: return plane.Position >= plane.Bounds.Min.x && plane.Position <= plane.Bounds.Max.x;
            case SpatialDebugSplitAxis::Y: return plane.Position >= plane.Bounds.Min.y && plane.Position <= plane.Bounds.Max.y;
            case SpatialDebugSplitAxis::Z: return plane.Position >= plane.Bounds.Min.z && plane.Position <= plane.Bounds.Max.z;
            }
            return false;
        }

        [[nodiscard]] std::array<glm::vec3, 4> SplitPlaneCorners(const SpatialDebugSplitPlane& plane) noexcept
        {
            const auto min = plane.Bounds.Min;
            const auto max = plane.Bounds.Max;
            switch (plane.Axis)
            {
            case SpatialDebugSplitAxis::X:
                return {
                    glm::vec3{plane.Position, min.y, min.z}, glm::vec3{plane.Position, max.y, min.z},
                    glm::vec3{plane.Position, max.y, max.z}, glm::vec3{plane.Position, min.y, max.z},
                };
            case SpatialDebugSplitAxis::Y:
                return {
                    glm::vec3{min.x, plane.Position, min.z}, glm::vec3{max.x, plane.Position, min.z},
                    glm::vec3{max.x, plane.Position, max.z}, glm::vec3{min.x, plane.Position, max.z},
                };
            case SpatialDebugSplitAxis::Z:
                return {
                    glm::vec3{min.x, min.y, plane.Position}, glm::vec3{max.x, min.y, plane.Position},
                    glm::vec3{max.x, max.y, plane.Position}, glm::vec3{min.x, max.y, plane.Position},
                };
            }
            return {};
        }
    }

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugBoundsWireframes(
        std::span<const SpatialDebugAabb> bounds,
        const SpatialDebugVisualizerOptions& options = {})
    {
        SpatialDebugPacketResult result{};
        result.Diagnostics.InputRecordCount = static_cast<std::uint32_t>(bounds.size());
        result.Lines.reserve(std::min<std::size_t>(bounds.size() * 12u, options.MaxLinePackets));

        for (const SpatialDebugAabb& box : bounds)
        {
            if (!Detail::IsValid(box))
            {
                ++result.Diagnostics.RejectedInvalidBoundsCount;
                continue;
            }
            if (!Detail::IsFinite(options.LeafColor))
            {
                ++result.Diagnostics.RejectedInvalidCoordinateCount;
                continue;
            }
            Detail::AppendAabb(result, options, box, options.LeafColor);
        }
        return result;
    }

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugHierarchyWireframes(
        std::span<const SpatialDebugHierarchyNode> nodes,
        const SpatialDebugVisualizerOptions& options = {})
    {
        SpatialDebugPacketResult result{};
        result.Diagnostics.InputRecordCount = static_cast<std::uint32_t>(nodes.size());
        result.Lines.reserve(std::min<std::size_t>(nodes.size() * 12u, options.MaxLinePackets));

        for (const SpatialDebugHierarchyNode& node : nodes)
        {
            if (node.Depth > options.MaxDepth)
            {
                ++result.Diagnostics.RejectedDepthLimitCount;
                continue;
            }
            if (!Detail::IsValid(node.Bounds))
            {
                ++result.Diagnostics.RejectedInvalidBoundsCount;
                continue;
            }
            const glm::vec4 color = node.IsLeaf ? options.LeafColor : options.BranchColor;
            if (!Detail::IsFinite(color))
            {
                ++result.Diagnostics.RejectedInvalidCoordinateCount;
                continue;
            }
            Detail::AppendAabb(result, options, node.Bounds, color);
        }
        return result;
    }

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugSplitPlaneWireframes(
        std::span<const SpatialDebugSplitPlane> planes,
        const SpatialDebugVisualizerOptions& options = {})
    {
        SpatialDebugPacketResult result{};
        result.Diagnostics.InputRecordCount = static_cast<std::uint32_t>(planes.size());
        result.Lines.reserve(std::min<std::size_t>(planes.size() * 4u, options.MaxLinePackets));

        for (const SpatialDebugSplitPlane& plane : planes)
        {
            if (!Detail::IsValid(plane.Bounds))
            {
                ++result.Diagnostics.RejectedInvalidBoundsCount;
                continue;
            }
            if (!Detail::IsValidSplitPosition(plane) || !Detail::IsFinite(options.SplitPlaneColor))
            {
                ++result.Diagnostics.RejectedInvalidCoordinateCount;
                continue;
            }
            const auto corners = Detail::SplitPlaneCorners(plane);
            Detail::AppendLine(result, options, corners[0], corners[1], options.SplitPlaneColor);
            Detail::AppendLine(result, options, corners[1], corners[2], options.SplitPlaneColor);
            Detail::AppendLine(result, options, corners[2], corners[3], options.SplitPlaneColor);
            Detail::AppendLine(result, options, corners[3], corners[0], options.SplitPlaneColor);
        }
        return result;
    }

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugConvexHullWireframe(
        std::span<const glm::vec3> vertices,
        std::span<const SpatialDebugWireEdge> edges,
        const SpatialDebugVisualizerOptions& options = {})
    {
        SpatialDebugPacketResult result{};
        result.Diagnostics.InputRecordCount = static_cast<std::uint32_t>(edges.size());
        result.Lines.reserve(std::min<std::size_t>(edges.size(), options.MaxLinePackets));

        if (!Detail::IsFinite(options.HullColor))
        {
            result.Diagnostics.RejectedInvalidCoordinateCount = static_cast<std::uint32_t>(edges.size());
            return result;
        }

        for (const SpatialDebugWireEdge edge : edges)
        {
            if (edge.A >= vertices.size() || edge.B >= vertices.size() || edge.A == edge.B)
            {
                ++result.Diagnostics.RejectedTopologyCount;
                continue;
            }
            const glm::vec3 a = vertices[edge.A];
            const glm::vec3 b = vertices[edge.B];
            if (!Detail::IsFinite(a) || !Detail::IsFinite(b))
            {
                ++result.Diagnostics.RejectedInvalidCoordinateCount;
                continue;
            }
            Detail::AppendLine(result, options, a, b, options.HullColor);
        }
        return result;
    }

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugPointMarkers(
        std::span<const glm::vec3> points,
        const SpatialDebugVisualizerOptions& options = {})
    {
        SpatialDebugPacketResult result{};
        result.Diagnostics.InputRecordCount = static_cast<std::uint32_t>(points.size());
        result.Points.reserve(std::min<std::size_t>(points.size(), options.MaxPointPackets));

        for (const glm::vec3 point : points)
        {
            if (!Detail::IsFinite(point) || !Detail::IsFinite(options.LeafColor))
            {
                ++result.Diagnostics.RejectedInvalidCoordinateCount;
                continue;
            }
            if (result.Points.size() >= options.MaxPointPackets)
            {
                result.Diagnostics.TruncatedPointBudget = true;
                continue;
            }
            result.Points.push_back(DebugPointPacket{
                .Position = point,
                .Color = options.LeafColor,
                .Radius = std::max(options.PointRadius, 0.0001f),
                .DepthTested = options.DepthTested,
            });
            ++result.Diagnostics.EmittedPointCount;
        }
        return result;
    }
}



