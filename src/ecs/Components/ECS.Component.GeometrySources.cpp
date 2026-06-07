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
                        bool hasNodes) noexcept
    {
        if (hasVertices && hasEdges && hasHalfedges && hasFaces && !hasNodes)
            return Domain::Mesh;

        if (!hasVertices && hasEdges && hasHalfedges && !hasFaces && hasNodes)
            return Domain::Graph;

        if (hasVertices && !hasEdges && !hasHalfedges && !hasFaces && !hasNodes)
            return Domain::PointCloud;

        if (!hasVertices && !hasEdges && !hasHalfedges && !hasFaces && !hasNodes)
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
        view.NodeSource = registry.try_get<Nodes>(entity);

        const bool hasMeshTopology = registry.all_of<HasMeshTopology>(entity);
        const bool hasGraphTopology = registry.all_of<HasGraphTopology>(entity);

        view.ActiveDomain = DetectDomain(
            view.VertexSource != nullptr,
            view.EdgeSource != nullptr || hasGraphTopology,
            view.HalfedgeSource != nullptr || hasGraphTopology,
            view.FaceSource != nullptr || hasMeshTopology,
            view.NodeSource != nullptr);

        assert(view.ActiveDomain != Domain::Mesh || view.FaceSource != nullptr || hasMeshTopology);
        assert(view.ActiveDomain != Domain::Graph || view.NodeSource != nullptr || hasGraphTopology);

        return view;
    }

    MutableSourceView BuildMutableView(entt::registry& registry, entt::entity entity)
    {
        MutableSourceView view{};
        view.VertexSource = registry.try_get<Vertices>(entity);
        view.EdgeSource = registry.try_get<Edges>(entity);
        view.HalfedgeSource = registry.try_get<Halfedges>(entity);
        view.FaceSource = registry.try_get<Faces>(entity);
        view.NodeSource = registry.try_get<Nodes>(entity);

        const bool hasMeshTopology = registry.all_of<HasMeshTopology>(entity);
        const bool hasGraphTopology = registry.all_of<HasGraphTopology>(entity);

        view.ActiveDomain = DetectDomain(
            view.VertexSource != nullptr,
            view.EdgeSource != nullptr || hasGraphTopology,
            view.HalfedgeSource != nullptr || hasGraphTopology,
            view.FaceSource != nullptr || hasMeshTopology,
            view.NodeSource != nullptr);

        assert(view.ActiveDomain != Domain::Mesh || view.FaceSource != nullptr || hasMeshTopology);
        assert(view.ActiveDomain != Domain::Graph || view.NodeSource != nullptr || hasGraphTopology);

        return view;
    }
}
