// -------------------------------------------------------------------------
// Extrinsic::ECS::Components::GeometrySources — populate helpers.
//
// Free functions that construct the authoritative `GeometrySources`
// components on an ECS entity from a rich geometry object. After calling
// one of these functions the entity owns all its CPU geometry data through
// the ECS component PropertySets, and the original geometry object is no
// longer required.
//
// Population contract:
//   PopulateFromMesh   → emplaces Vertices, Edges, Halfedges, Faces
//   PopulateFromGraph  → emplaces Vertices, Halfedges, Edges, plus the
//                        HasGraphTopology provenance marker.
//                        Calls graph.GarbageCollection() if HasGarbage().
//   PopulateFromCloud  → emplaces Vertices
//
// Every populate call first drops the entity's existing GeometrySources
// components (Vertices/Edges/Halfedges/Faces) and topology markers
// (HasMeshTopology/HasGraphTopology) so a re-population from a different
// domain (e.g. mesh→cloud, graph→cloud, mesh→graph) does not leak stale
// topology into BuildConstView/BuildMutableView. The reset is silent on
// first-population entities.
//
// Canonical properties written by every populate call (see
// `Extrinsic.ECS.Components.GeometrySources::PropertyNames`):
//   Vertices         : "v:position"  (glm::vec3)
//                      "v:normal"    (glm::vec3, when available)
//   Edges            : "e:v0", "e:v1" (uint32_t endpoint indices)
//   Halfedges        : "h:to_vertex", "h:next", "h:face" (uint32_t)
//   Faces            : "f:halfedge" (uint32_t)
//
// In addition to the canonical keys, the ENTIRE PropertySet from the source
// geometry is copied, preserving user-defined per-element properties
// (colors, labels, radii, vector fields, …).
// -------------------------------------------------------------------------

module;

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Components.GeometrySourcesPopulate;

import Extrinsic.ECS.Components.GeometrySources;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;

export namespace Extrinsic::ECS::Components::GeometrySources
{
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
