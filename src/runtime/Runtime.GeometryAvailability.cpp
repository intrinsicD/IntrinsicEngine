module;

#include <array>
#include <string_view>
#include <utility>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.GeometryAvailability;

namespace Extrinsic::Runtime
{
    namespace GS = GeometrySources;
    namespace G = RenderComponents;

    std::string_view ToString(const GeometryElementDomain domain) noexcept
    {
        switch (domain)
        {
        case GeometryElementDomain::Unknown:         return "Unknown";
        case GeometryElementDomain::MeshVertex:      return "MeshVertex";
        case GeometryElementDomain::MeshEdge:        return "MeshEdge";
        case GeometryElementDomain::MeshHalfedge:    return "MeshHalfedge";
        case GeometryElementDomain::MeshFace:        return "MeshFace";
        case GeometryElementDomain::GraphNode:       return "GraphNode";
        case GeometryElementDomain::GraphEdge:       return "GraphEdge";
        case GeometryElementDomain::PointCloudPoint: return "PointCloudPoint";
        }
        return "Unknown";
    }

    std::string_view ToString(const GeometryRenderLane lane) noexcept
    {
        switch (lane)
        {
        case GeometryRenderLane::Surface: return "Surface";
        case GeometryRenderLane::Edges:   return "Edges";
        case GeometryRenderLane::Points:  return "Points";
        }
        return "Unknown";
    }

    std::string_view ToString(const GeometryAvailabilityStatus status) noexcept
    {
        switch (status)
        {
        case GeometryAvailabilityStatus::Supported:             return "Supported";
        case GeometryAvailabilityStatus::NotRequested:          return "NotRequested";
        case GeometryAvailabilityStatus::NoGeometrySource:      return "NoGeometrySource";
        case GeometryAvailabilityStatus::UnsupportedProvenance: return "UnsupportedProvenance";
        case GeometryAvailabilityStatus::MissingPointSource:    return "MissingPointSource";
        case GeometryAvailabilityStatus::MissingEdgeSource:     return "MissingEdgeSource";
        case GeometryAvailabilityStatus::MissingHalfedgeSource: return "MissingHalfedgeSource";
        case GeometryAvailabilityStatus::MissingFaceSource:     return "MissingFaceSource";
        case GeometryAvailabilityStatus::MissingPropertySource: return "MissingPropertySource";
        }
        return "Unknown";
    }

    GeometryEntityAvailability BuildGeometryAvailability(
        const entt::registry& registry,
        const entt::entity entity)
    {
        GeometryEntityAvailability availability{};
        if (entity == entt::null || !registry.valid(entity))
            return availability;

        availability.SourceView = GS::BuildConstView(registry, entity);
        availability.Sources = GS::BuildSourceAvailability(availability.SourceView);

        if (const auto* surface = registry.try_get<G::RenderSurface>(entity))
            availability.Surface = *surface;
        if (const auto* edges = registry.try_get<G::RenderEdges>(entity))
            availability.Edges = *edges;
        if (const auto* points = registry.try_get<G::RenderPoints>(entity))
            availability.Points = *points;

        return availability;
    }

    GeometryEntityAvailability BuildGeometryAvailability(
        const GS::ConstSourceView& view) noexcept
    {
        GeometryEntityAvailability availability{};
        availability.SourceView = view;
        availability.Sources = GS::BuildSourceAvailability(view);
        return availability;
    }

    GeometryRenderLaneAvailability ResolveRenderLaneAvailability(
        const GeometryEntityAvailability& availability,
        const GeometryRenderLane lane) noexcept
    {
        GeometryRenderLaneAvailability result{
            .Lane = lane,
            .Requested = false,
            .Supported = false,
            .Status = GeometryAvailabilityStatus::NotRequested,
            .ProvenanceDomain = availability.Sources.ProvenanceDomain,
        };

        const auto has = [&](const GS::SourceCapability capability) noexcept
        {
            return availability.Sources.Has(capability);
        };

        switch (lane)
        {
        case GeometryRenderLane::Surface:
            result.Requested = availability.Surface.has_value();
            if (!result.Requested)
                return result;
            if (!availability.HasGeometry())
            {
                result.Status = GeometryAvailabilityStatus::NoGeometrySource;
                return result;
            }
            if (availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
            {
                result.Status = GeometryAvailabilityStatus::UnsupportedProvenance;
                return result;
            }
            if (!has(GS::SourceCapability::VertexPoints))
            {
                result.Status = GeometryAvailabilityStatus::MissingPointSource;
                return result;
            }
            if (!has(GS::SourceCapability::Halfedges))
            {
                result.Status = GeometryAvailabilityStatus::MissingHalfedgeSource;
                return result;
            }
            if (!has(GS::SourceCapability::Faces))
            {
                result.Status = GeometryAvailabilityStatus::MissingFaceSource;
                return result;
            }
            result.Supported = true;
            result.Status = GeometryAvailabilityStatus::Supported;
            return result;

        case GeometryRenderLane::Edges:
            result.Requested = availability.Edges.has_value();
            if (!result.Requested)
                return result;
            if (!availability.HasGeometry())
            {
                result.Status = GeometryAvailabilityStatus::NoGeometrySource;
                return result;
            }
            if (!availability.Sources.HasPointSource())
            {
                result.Status = GeometryAvailabilityStatus::MissingPointSource;
                return result;
            }
            if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                result.Status = GeometryAvailabilityStatus::UnsupportedProvenance;
                return result;
            }
            if (!has(GS::SourceCapability::Edges))
            {
                if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
                {
                    if (!has(GS::SourceCapability::Halfedges))
                    {
                        result.Status = GeometryAvailabilityStatus::MissingHalfedgeSource;
                        return result;
                    }
                    if (!has(GS::SourceCapability::Faces))
                    {
                        result.Status = GeometryAvailabilityStatus::MissingFaceSource;
                        return result;
                    }
                }
                else
                {
                    result.Status = GeometryAvailabilityStatus::MissingEdgeSource;
                    return result;
                }
            }
            result.Supported = true;
            result.Status = GeometryAvailabilityStatus::Supported;
            return result;

        case GeometryRenderLane::Points:
            result.Requested = availability.Points.has_value();
            if (!result.Requested)
                return result;
            if (!availability.HasGeometry())
            {
                result.Status = GeometryAvailabilityStatus::NoGeometrySource;
                return result;
            }
            if (!availability.Sources.HasPointSource())
            {
                result.Status = GeometryAvailabilityStatus::MissingPointSource;
                return result;
            }
            result.Supported = true;
            result.Status = GeometryAvailabilityStatus::Supported;
            return result;
        }

        return result;
    }

    bool SupportsGeometryElementDomain(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain) noexcept
    {
        return ResolveGeometryPropertySet(availability, domain) != nullptr;
    }

    const Geometry::PropertySet* ResolveGeometryPropertySet(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain) noexcept
    {
        const GS::ConstSourceView& view = availability.SourceView;
        const GS::SourceAvailability& sources = availability.Sources;

        switch (domain)
        {
        case GeometryElementDomain::MeshVertex:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.VertexSource
                ? &view.VertexSource->Properties
                : nullptr;
        case GeometryElementDomain::MeshEdge:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.EdgeSource
                ? &view.EdgeSource->Properties
                : nullptr;
        case GeometryElementDomain::MeshHalfedge:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.HalfedgeSource
                ? &view.HalfedgeSource->Properties
                : nullptr;
        case GeometryElementDomain::MeshFace:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.FaceSource
                ? &view.FaceSource->Properties
                : nullptr;
        case GeometryElementDomain::GraphNode:
            return sources.ProvenanceDomain == GS::Domain::Graph && view.NodeSource
                ? &view.NodeSource->Properties
                : nullptr;
        case GeometryElementDomain::GraphEdge:
            return sources.ProvenanceDomain == GS::Domain::Graph && view.EdgeSource
                ? &view.EdgeSource->Properties
                : nullptr;
        case GeometryElementDomain::PointCloudPoint:
            return sources.ProvenanceDomain == GS::Domain::PointCloud && view.VertexSource
                ? &view.VertexSource->Properties
                : nullptr;
        case GeometryElementDomain::Unknown:
            return nullptr;
        }
        return nullptr;
    }

    std::size_t ResolveGeometryElementCount(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain) noexcept
    {
        const Geometry::PropertySet* properties = ResolveGeometryPropertySet(availability, domain);
        return properties ? properties->Size() : 0u;
    }
}
