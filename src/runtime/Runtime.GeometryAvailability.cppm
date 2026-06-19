module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Extrinsic.Runtime.GeometryAvailability;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    namespace GeometrySources = Extrinsic::ECS::Components::GeometrySources;
    namespace RenderComponents = Extrinsic::Graphics::Components;

    enum class GeometryElementDomain : std::uint8_t
    {
        Unknown,
        MeshVertex,
        MeshEdge,
        MeshHalfedge,
        MeshFace,
        GraphNode,
        GraphEdge,
        PointCloudPoint,
    };

    enum class GeometryRenderLane : std::uint8_t
    {
        Surface,
        Edges,
        Points,
    };

    enum class GeometryAvailabilityStatus : std::uint8_t
    {
        Supported,
        NotRequested,
        NoGeometrySource,
        UnsupportedProvenance,
        MissingPointSource,
        MissingEdgeSource,
        MissingHalfedgeSource,
        MissingFaceSource,
        MissingPropertySource,
    };

    struct GeometryRenderLaneAvailability
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        bool Requested{false};
        bool Supported{false};
        GeometryAvailabilityStatus Status{GeometryAvailabilityStatus::NotRequested};
        GeometrySources::Domain ProvenanceDomain{GeometrySources::Domain::None};

        [[nodiscard]] bool Ready() const noexcept
        {
            return Requested && Supported;
        }
    };

    struct GeometryEntityAvailability
    {
        GeometrySources::ConstSourceView SourceView{};
        GeometrySources::SourceAvailability Sources{};

        std::optional<RenderComponents::RenderSurface> Surface{};
        std::optional<RenderComponents::RenderEdges> Edges{};
        std::optional<RenderComponents::RenderPoints> Points{};

        [[nodiscard]] bool HasGeometry() const noexcept
        {
            return Sources.Capabilities != GeometrySources::SourceCapability::None;
        }

        [[nodiscard]] bool HasRenderLaneRequest() const noexcept
        {
            return Surface.has_value() || Edges.has_value() || Points.has_value();
        }
    };

    [[nodiscard]] std::string_view ToString(GeometryElementDomain domain) noexcept;
    [[nodiscard]] std::string_view ToString(GeometryRenderLane lane) noexcept;
    [[nodiscard]] std::string_view ToString(GeometryAvailabilityStatus status) noexcept;

    [[nodiscard]] GeometryEntityAvailability BuildGeometryAvailability(
        const entt::registry& registry,
        entt::entity entity);

    [[nodiscard]] GeometryEntityAvailability BuildGeometryAvailability(
        const GeometrySources::ConstSourceView& view) noexcept;

    [[nodiscard]] GeometryRenderLaneAvailability ResolveRenderLaneAvailability(
        const GeometryEntityAvailability& availability,
        GeometryRenderLane lane) noexcept;

    [[nodiscard]] bool SupportsGeometryElementDomain(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain) noexcept;

    [[nodiscard]] const Geometry::PropertySet* ResolveGeometryPropertySet(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain) noexcept;

    [[nodiscard]] std::size_t ResolveGeometryElementCount(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain) noexcept;
}
