// Private topology upload adapters and shared explicit vertex-channel preparation.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.GeometryPlanBuilders;

import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.GeometryAvailability;
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

    // Bound structural streams for graph and point-cloud vertices. An
    // unresolved binding leaves the channel absent. Topology owners clear full
    // buffers before channel preparation.
    void PrepareBoundVertexChannels(
        const Geometry::PropertySet& properties, GeometryElementDomain domain,
        const VertexChannelBindingSet* channelBindings, std::size_t vertexCount,
        VertexChannelStreams& channels);

    // The properties a mesh surface draws its shading streams from after
    // structural bindings (RUNTIME-315): a bound source that does not resolve
    // (stale name, wrong kind or count) falls back to the canonical one. A
    // vertex-domain binding suppresses the canonical corner stream; a corner
    // binding replaces it. Empty names mean "no such stream". Shared by the
    // mesh plan builder and render-extraction revision tracking.
    struct MeshShadingSources
    {
        std::string_view VertexNormal{};
        std::string_view CornerNormal{};
        std::string_view VertexTexcoord{};
        std::string_view CornerTexcoord{};
    };
    [[nodiscard]] MeshShadingSources ResolveMeshShadingSources(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings) noexcept;

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
        // Non-empty when corner UV/normal seams duplicate mesh vertices for
        // the GPU surface. Entry i names the canonical mesh vertex backing
        // GPU vertex i so vertex-domain visualization data can follow the
        // exact same split.
        std::vector<std::uint32_t> SourceVertexForGpuVertex{};
        // Face attributes index source polygons; the GPU draws their fan triangles.
        std::vector<std::uint32_t> SourceFaceForGpuTriangle{};

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


    struct MeshPlanBuildResult
    {
        MeshPackStatus Status{MeshPackStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] MeshPlanBuildResult BuildMeshGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        MeshPackBuffer& outBuffer);

    // Packed position+texcoord AoS vertex shared by graph, point-cloud and
    // mesh-primitive-view uploads; graphics splits it at offsets 0 and 12.
    struct PositionUvVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(PositionUvVertex) == 20u);
    static_assert(offsetof(PositionUvVertex, Px) == 0u);
    static_assert(offsetof(PositionUvVertex, Py) == 4u);
    static_assert(offsetof(PositionUvVertex, Pz) == 8u);
    static_assert(offsetof(PositionUvVertex, U) == 12u);
    static_assert(offsetof(PositionUvVertex, V) == 16u);

    struct GraphPackBuffer
    {
        std::vector<std::byte> VertexBytes{};
        VertexChannelStreams Channels{};
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


    struct GraphPlanBuildResult
    {
        GraphPackStatus Status{GraphPackStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] GraphPlanBuildResult BuildGraphGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        bool wantLines,
        bool wantPoints,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        GraphPackBuffer& outBuffer);

    struct PointCloudPackBuffer
    {
        std::vector<std::byte> VertexBytes{};
        VertexChannelStreams Channels{};

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


    struct PointCloudPlanBuildResult
    {
        PointCloudPackStatus Status{PointCloudPackStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] PointCloudPlanBuildResult BuildPointCloudGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        PointCloudPackBuffer& outBuffer);

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


    struct MeshPrimitiveViewPlanBuildResult
    {
        MeshPrimitiveViewStatus Status{MeshPrimitiveViewStatus::Success};
        std::optional<Graphics::GeometryUploadPlan> Plan{};
    };

    [[nodiscard]] MeshPrimitiveViewPlanBuildResult BuildMeshEdgeViewPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        MeshPrimitiveViewBuffer& outBuffer);
    [[nodiscard]] MeshPrimitiveViewPlanBuildResult BuildMeshVertexViewPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
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
