// -------------------------------------------------------------------------
// ECS::Components::GeometrySources — populate helpers.
//
// Free functions that construct the authoritative GeometrySources components
// on an ECS entity from a rich geometry object.  After calling one of these
// functions the entity owns all its CPU geometry data through the ECS
// component PropertySets, and the original geometry object is no longer
// required for rendering.
//
// Population contract:
//   PopulateFromMesh   → emplaces Vertices, Edges, Halfedges, Faces
//   PopulateFromGraph  → emplaces Nodes, Edges
//                        (calls GarbageCollection if the graph has garbage)
//   PopulateFromCloud  → emplaces Vertices
//
// Canonical properties written by every populate call:
//   Vertices / Nodes : "v:position"  (glm::vec3)
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

export module ECS:Components.GeometrySourcesPopulate;

import :Components.GeometrySources;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;

export namespace ECS::Components::GeometrySources
{
    // Populate Vertices, Edges, Halfedges, Faces from a Halfedge mesh.
    // Copies each domain's PropertySet and writes the canonical topology keys.
    // Safe to call on a const-semantics mesh (takes non-const ref to access
    // the non-const VertexProperties() overload for PropertySet copy).
    void PopulateFromMesh(entt::registry& registry,
                          entt::entity   entity,
                          Geometry::Halfedge::Mesh& mesh);

    // Populate Nodes and Edges from a Graph.
    // Calls graph.GarbageCollection() if HasGarbage() is true, ensuring the
    // resulting GeometrySources PropertySets are contiguous (no deleted gaps).
    void PopulateFromGraph(entt::registry& registry,
                           entt::entity    entity,
                           Geometry::Graph::Graph& graph);

    // Populate Vertices from a PointCloud.
    // Writes "v:position" (and "v:normal" when HasNormals()) plus copies
    // the full PointProperties() PropertySet.
    void PopulateFromCloud(entt::registry& registry,
                           entt::entity    entity,
                           Geometry::PointCloud::Cloud& cloud);
}

