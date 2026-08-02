module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.GeometryPlanBuilders;

import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.VertexChannelStreams;
import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    using ECS::Components::ProceduralGeometryKind;
    using ECS::Components::ProceduralGeometryParams;

    // This module is registered in ExtrinsicRuntime's PRIVATE C++ module file
    // set. It is one internal declaration surface for typed topology adapters;
    // no domain-specific upload-plan builder is part of the runtime's public API.
    struct GeometryPlanBuildRequest
    {
        Graphics::GeometryResidencyKey Key{};
        std::uint64_t Generation{0u};
        Graphics::GeometryUploadUpdateClass UpdateClass{
            Graphics::GeometryUploadUpdateClass::FullReplacement};
        Graphics::GpuWorld::GeometryChannelUpdateMask UpdateChannels{};
    };

    [[nodiscard]] inline std::optional<AttributeSourceType>
        ToAttributeSourceType(
            const Geometry::PropertyValueKind valueKind) noexcept
    {
        switch (valueKind)
        {
        case Geometry::PropertyValueKind::Float:
            return AttributeSourceType::Float32;
        case Geometry::PropertyValueKind::Vec2:
            return AttributeSourceType::Vec2;
        case Geometry::PropertyValueKind::Vec3:
            return AttributeSourceType::Vec3;
        case Geometry::PropertyValueKind::Vec4:
            return AttributeSourceType::Vec4;
        default:
            return std::nullopt;
        }
    }

    struct MeshVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 1.0f;
    };
    static_assert(sizeof(MeshVertex) == 32u);

    struct MeshPackBuffer
    {
        std::vector<std::byte> VertexBytes{};
        VertexChannelStreams Channels{};
        std::vector<std::uint32_t> PackedColors{};
        std::vector<std::uint32_t> SurfaceIndices{};

        void Clear() noexcept;
    };

    enum class MeshPackStatus : std::uint8_t
    {
        Success,
        WrongDomain,
        MissingPositions,
        MissingHalfedgeTopology,
        MissingFaceTopology,
        EmptyMesh,
        InvalidTopology,
        NonFinitePosition,
        MissingTexcoords,
        NonFiniteTexcoord,
        DegenerateAllFaces,
    };

    [[nodiscard]] const char* DebugNameForMeshPackStatus(
        MeshPackStatus status) noexcept;

    struct MeshPlanBuildResult
    {
        MeshPackStatus Status{MeshPackStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] MeshPlanBuildResult BuildMeshGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const GeometryPlanBuildRequest& request,
        MeshPackBuffer& outBuffer);
    [[nodiscard]] MeshPlanBuildResult BuildMeshGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        MeshPackBuffer& outBuffer);

    struct GraphVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(GraphVertex) == 20u);

    struct GraphPackBuffer
    {
        std::vector<std::byte> VertexBytes{};
        VertexChannelStreams Channels{};
        std::vector<std::uint32_t> PackedColors{};
        std::vector<std::uint32_t> LineIndices{};

        void Clear() noexcept;
    };

    enum class GraphPackStatus : std::uint8_t
    {
        Success,
        WrongDomain,
        NoRenderLane,
        MissingNodes,
        EmptyGraph,
        MissingEdgeTopology,
        InvalidEdge,
        NonFinitePosition,
    };

    [[nodiscard]] const char* DebugNameForGraphPackStatus(
        GraphPackStatus status) noexcept;

    struct GraphPlanBuildResult
    {
        GraphPackStatus Status{GraphPackStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] GraphPlanBuildResult BuildGraphGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        bool wantLines,
        bool wantPoints,
        const GeometryPlanBuildRequest& request,
        GraphPackBuffer& outBuffer);
    [[nodiscard]] GraphPlanBuildResult BuildGraphGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        bool wantLines,
        bool wantPoints,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        GraphPackBuffer& outBuffer);

    struct PointCloudVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(PointCloudVertex) == 20u);

    struct PointCloudPackBuffer
    {
        std::vector<std::byte> VertexBytes{};
        VertexChannelStreams Channels{};
        std::vector<std::uint32_t> PackedColors{};

        void Clear() noexcept;
    };

    enum class PointCloudPackStatus : std::uint8_t
    {
        Success,
        WrongDomain,
        MissingPositions,
        EmptyCloud,
        NonFinitePosition,
    };

    [[nodiscard]] const char* DebugNameForPointCloudPackStatus(
        PointCloudPackStatus status) noexcept;

    struct PointCloudPlanBuildResult
    {
        PointCloudPackStatus Status{PointCloudPackStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] PointCloudPlanBuildResult BuildPointCloudGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const GeometryPlanBuildRequest& request,
        PointCloudPackBuffer& outBuffer);
    [[nodiscard]] PointCloudPlanBuildResult BuildPointCloudGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        PointCloudPackBuffer& outBuffer);

    struct MeshPrimitiveVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(MeshPrimitiveVertex) == 20u);

    struct MeshPrimitiveViewBuffer
    {
        std::vector<std::byte> VertexBytes{};
        VertexChannelStreams Channels{};
        std::vector<std::uint32_t> LineIndices{};

        void Clear() noexcept;
    };

    enum class MeshPrimitiveViewStatus : std::uint8_t
    {
        Success,
        WrongDomain,
        MissingPositions,
        EmptyMesh,
        MissingEdgeTopology,
        InvalidEdge,
        NonFinitePosition,
    };

    [[nodiscard]] const char* DebugNameForMeshPrimitiveViewStatus(
        MeshPrimitiveViewStatus status) noexcept;

    struct MeshPrimitiveViewPlanBuildResult
    {
        MeshPrimitiveViewStatus Status{MeshPrimitiveViewStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] MeshPrimitiveViewPlanBuildResult BuildMeshEdgeViewPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const GeometryPlanBuildRequest& request,
        MeshPrimitiveViewBuffer& outBuffer);
    [[nodiscard]] MeshPrimitiveViewPlanBuildResult BuildMeshVertexViewPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const GeometryPlanBuildRequest& request,
        MeshPrimitiveViewBuffer& outBuffer);

    struct ProceduralVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 1.0f;
    };
    static_assert(sizeof(ProceduralVertex) == 32u);

    struct ProceduralGeometryPackBuffer
    {
        std::vector<std::byte> VertexBytes{};
        std::vector<std::uint32_t> SurfaceIndices{};
        std::vector<std::uint32_t> LineIndices{};

        void Clear() noexcept;
    };

    [[nodiscard]] std::uint64_t HashProceduralGeometryParams(
        const ECS::Components::ProceduralGeometryParams& params) noexcept;
    [[nodiscard]] const char* DebugNameForProceduralGeometryKind(
        ECS::Components::ProceduralGeometryKind kind) noexcept;
    [[nodiscard]] std::optional<Graphics::GeometryUploadPlan>
        BuildProceduralGeometryPlan(
            ECS::Components::ProceduralGeometryKind kind,
            const ECS::Components::ProceduralGeometryParams& params,
            const GeometryPlanBuildRequest& request,
            ProceduralGeometryPackBuffer& outBuffer);
}
