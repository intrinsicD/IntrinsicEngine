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

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugBoundsWireframes(
        std::span<const SpatialDebugAabb> bounds,
        const SpatialDebugVisualizerOptions& options = {});

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugHierarchyWireframes(
        std::span<const SpatialDebugHierarchyNode> nodes,
        const SpatialDebugVisualizerOptions& options = {});

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugSplitPlaneWireframes(
        std::span<const SpatialDebugSplitPlane> planes,
        const SpatialDebugVisualizerOptions& options = {});

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugConvexHullWireframe(
        std::span<const glm::vec3> vertices,
        std::span<const SpatialDebugWireEdge> edges,
        const SpatialDebugVisualizerOptions& options = {});

    export [[nodiscard]] SpatialDebugPacketResult BuildSpatialDebugPointMarkers(
        std::span<const glm::vec3> points,
        const SpatialDebugVisualizerOptions& options = {});
}
