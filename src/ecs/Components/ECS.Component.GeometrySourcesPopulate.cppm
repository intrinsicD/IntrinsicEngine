// Prepares owned geometry source components and publishes them into ECS entities.
// Preparation can run off-thread; publication replaces the previous element domains.
module;

#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>

export module Extrinsic.ECS.Components.GeometrySourcesPopulate;

import Extrinsic.ECS.Components.GeometrySources;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;

export namespace Extrinsic::ECS::Components::GeometrySources
{
    struct PreparedMeshSources
    {
        Vertices VertexSource{};
        Edges EdgeSource{};
        Halfedges HalfedgeSource{};
        Faces FaceSource{};
    };

    // Preparation touches only the supplied mesh; publication moves the completed
    // components into the registry on its owning thread.
    [[nodiscard]] PreparedMeshSources PrepareFromMesh(
        Geometry::HalfedgeMesh::Mesh& mesh);
    void PublishPreparedMesh(entt::registry& registry, entt::entity entity,
                             PreparedMeshSources sources);

    // Populate Vertices, Edges, Halfedges, Faces from a halfedge mesh.
    // Copies each domain's PropertySet and writes the canonical topology
    // keys. The source mesh can be discarded after population.
    void PopulateFromMesh(entt::registry& registry,
                          entt::entity entity,
                          Geometry::HalfedgeMesh::Mesh& mesh);

    // Populate Vertices, Halfedges, and Edges from a graph; also stamp
    // `HasGraphTopology` so provenance remains independent of the shared
    // physical component types. Calls graph.GarbageCollection() if HasGarbage()
    // so the resulting PropertySets are contiguous.
    void PopulateFromGraph(entt::registry& registry,
                           entt::entity entity,
                           Geometry::Graph::Graph& graph);

    // Populate Vertices from a point cloud. Writes "v:position" (and
    // "v:normal" when HasNormals()) plus copies the full PointProperties()
    // PropertySet to preserve user attributes.
    void PopulateFromCloud(entt::registry& registry,
                           entt::entity entity,
                           Geometry::PointCloud::Cloud& cloud);
}
