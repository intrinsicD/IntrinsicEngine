module;

#include <cassert>
#include <entt/entity/registry.hpp>

module Extrinsic.ECS.Components.GeometrySources;

namespace Extrinsic::ECS::Components::GeometrySources
{
    Domain DetectDomain(bool hasVertices,
                        bool hasEdges,
                        bool hasHalfedges,
                        bool hasFaces,
                        bool hasMeshTopologyMarker,
                        bool hasGraphTopologyMarker) noexcept
    {
        if (hasMeshTopologyMarker && hasGraphTopologyMarker)
            return Domain::Unknown;

        if (hasVertices && hasEdges && hasHalfedges && hasFaces &&
            !hasGraphTopologyMarker)
            return Domain::Mesh;

        if (hasVertices && hasEdges && hasHalfedges && !hasFaces &&
            hasGraphTopologyMarker && !hasMeshTopologyMarker)
            return Domain::Graph;

        if (hasVertices && !hasEdges && !hasHalfedges && !hasFaces &&
            !hasMeshTopologyMarker && !hasGraphTopologyMarker)
            return Domain::PointCloud;

        if (!hasVertices && !hasEdges && !hasHalfedges && !hasFaces &&
            !hasMeshTopologyMarker && !hasGraphTopologyMarker)
            return Domain::None;

        return Domain::Unknown;
    }

    ConstSourceView BuildConstView(const entt::registry& registry, entt::entity entity)
    {
        ConstSourceView view{};
        view.VertexSource = registry.try_get<Vertices>(entity);
        view.EdgeSource = registry.try_get<Edges>(entity);
        view.HalfedgeSource = registry.try_get<Halfedges>(entity);
        view.FaceSource = registry.try_get<Faces>(entity);

        view.HasMeshTopologyMarker = registry.all_of<HasMeshTopology>(entity);
        view.HasGraphTopologyMarker = registry.all_of<HasGraphTopology>(entity);

        view.ActiveDomain = DetectDomain(
            view.VertexSource != nullptr,
            view.EdgeSource != nullptr,
            view.HalfedgeSource != nullptr,
            view.FaceSource != nullptr,
            view.HasMeshTopologyMarker,
            view.HasGraphTopologyMarker);

        assert(view.ActiveDomain != Domain::Mesh ||
               (view.VertexSource != nullptr && view.EdgeSource != nullptr &&
                view.HalfedgeSource != nullptr && view.FaceSource != nullptr &&
                !view.HasGraphTopologyMarker));
        assert(view.ActiveDomain != Domain::Graph ||
               (view.VertexSource != nullptr && view.HalfedgeSource != nullptr &&
                view.EdgeSource != nullptr && view.HasGraphTopologyMarker));

        return view;
    }

    MutableSourceView BuildMutableView(entt::registry& registry, entt::entity entity)
    {
        MutableSourceView view{};
        view.VertexSource = registry.try_get<Vertices>(entity);
        view.EdgeSource = registry.try_get<Edges>(entity);
        view.HalfedgeSource = registry.try_get<Halfedges>(entity);
        view.FaceSource = registry.try_get<Faces>(entity);

        view.HasMeshTopologyMarker = registry.all_of<HasMeshTopology>(entity);
        view.HasGraphTopologyMarker = registry.all_of<HasGraphTopology>(entity);

        view.ActiveDomain = DetectDomain(
            view.VertexSource != nullptr,
            view.EdgeSource != nullptr,
            view.HalfedgeSource != nullptr,
            view.FaceSource != nullptr,
            view.HasMeshTopologyMarker,
            view.HasGraphTopologyMarker);

        assert(view.ActiveDomain != Domain::Mesh ||
               (view.VertexSource != nullptr && view.EdgeSource != nullptr &&
                view.HalfedgeSource != nullptr && view.FaceSource != nullptr &&
                !view.HasGraphTopologyMarker));
        assert(view.ActiveDomain != Domain::Graph ||
               (view.VertexSource != nullptr && view.HalfedgeSource != nullptr &&
                view.EdgeSource != nullptr && view.HasGraphTopologyMarker));

        return view;
    }

    SourceAvailability BuildSourceAvailability(const ConstSourceView& view) noexcept
    {
        SourceAvailability availability{};
        availability.ExactDomain = view.ActiveDomain;

        if (view.HasMeshTopologyMarker && view.HasGraphTopologyMarker)
            availability.ProvenanceDomain = Domain::Unknown;
        else if (view.HasMeshTopologyMarker)
            availability.ProvenanceDomain = Domain::Mesh;
        else if (view.HasGraphTopologyMarker)
            availability.ProvenanceDomain = Domain::Graph;
        else if (view.FaceSource != nullptr)
            availability.ProvenanceDomain = Domain::Mesh;
        else if (view.VertexSource != nullptr &&
                 view.EdgeSource == nullptr &&
                 view.HalfedgeSource == nullptr)
            availability.ProvenanceDomain = Domain::PointCloud;
        else if (view.VertexSource == nullptr &&
                 view.EdgeSource == nullptr &&
                 view.HalfedgeSource == nullptr &&
                 view.FaceSource == nullptr)
            availability.ProvenanceDomain = Domain::None;
        else
            availability.ProvenanceDomain = Domain::Unknown;

        if (view.VertexSource != nullptr)
        {
            availability.Capabilities |= SourceCapability::Vertices;
            availability.VertexCount = view.VerticesAlive();
        }
        if (view.EdgeSource != nullptr)
        {
            availability.Capabilities |= SourceCapability::Edges;
            availability.EdgeCount = view.EdgesAlive();
        }
        if (view.HalfedgeSource != nullptr)
        {
            availability.Capabilities |= SourceCapability::Halfedges;
            availability.HalfedgeCount = view.HalfedgesTotal();
        }
        if (view.FaceSource != nullptr)
        {
            availability.Capabilities |= SourceCapability::Faces;
            availability.FaceCount = view.FacesAlive();
        }

        return availability;
    }

    SourceAvailability BuildSourceAvailability(const MutableSourceView& view) noexcept
    {
        ConstSourceView constView{};
        constView.ActiveDomain = view.ActiveDomain;
        constView.HasMeshTopologyMarker = view.HasMeshTopologyMarker;
        constView.HasGraphTopologyMarker = view.HasGraphTopologyMarker;
        constView.VertexSource = view.VertexSource;
        constView.EdgeSource = view.EdgeSource;
        constView.HalfedgeSource = view.HalfedgeSource;
        constView.FaceSource = view.FaceSource;
        return BuildSourceAvailability(constView);
    }
}
