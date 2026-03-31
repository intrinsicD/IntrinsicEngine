// -------------------------------------------------------------------------
// OverlayEntityFactory — Creates child entities for cross-domain overlays.
// -------------------------------------------------------------------------
//
// When an entity needs geometry from a different domain than its own
// data authority (e.g., a mesh entity wants an independent point cloud
// overlay), this factory creates a properly composed child entity with:
//   - Hierarchy attachment to the parent (inherits transform)
//   - The appropriate Data component and DataAuthority tag
//   - SelectableTag and PickID for independent selection
//
// This centralizes the child-entity creation pattern already used by
// VectorFieldManager and makes it available to editor UI and algorithms.

module;
#include <cstdint>
#include <memory>
#include <string>
#include <entt/entity/registry.hpp>

export module Graphics.OverlayEntityFactory;

import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloudUtils;

export namespace Graphics::OverlayEntityFactory
{
    /// Create a child Mesh overlay on a parent entity.
    /// Returns the child entity with Mesh::Data + Surface::Component +
    /// DataAuthority::MeshTag.  The child is selectable and follows the
    /// parent's transform.
    entt::entity CreateMeshOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::Halfedge::Mesh> mesh,
        const std::string& name);

    /// Create a child PointCloud overlay on a parent entity.
    /// Returns the child entity with PointCloud::Data + DataAuthority::PointCloudTag.
    /// The child is selectable and follows the parent's transform.
    entt::entity CreatePointCloudOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::PointCloud::Cloud> cloud,
        const std::string& name);

    /// Create a child Graph overlay on a parent entity.
    /// Returns the child entity with Graph::Data + DataAuthority::GraphTag.
    /// The child is selectable and follows the parent's transform.
    entt::entity CreateGraphOverlay(
        entt::registry& registry,
        entt::entity parent,
        std::shared_ptr<Geometry::Graph::Graph> graph,
        const std::string& name);

    /// Detach and destroy an overlay child entity.
    /// Safe to call with entt::null or an already-destroyed entity.
    void DestroyOverlay(entt::registry& registry, entt::entity overlayEntity);
}
