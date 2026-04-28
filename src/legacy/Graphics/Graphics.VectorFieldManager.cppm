module;

#include <span>
#include <string>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

export module Graphics.VectorFieldManager;

import Graphics.VisualizationConfig;
import Geometry.Properties;

// =============================================================================
// VectorFieldManager — creates/updates/destroys child Graph entities for
// vector field visualization overlays.
//
// Each VectorFieldEntry in a VisualizationConfig spawns a child Graph overlay
// entity that inherits the parent's transform. Its nodes are explicit
// base/end points baked on the CPU, and the line pipeline consumes the shared
// vertex buffer directly.
//
// Multiple vector fields can be active simultaneously on the same source
// entity, each producing an independent child Graph entity.
//
// Usage:
//   Called per-frame (or on dirty) from the UI or sync systems.
//   SyncVectorFields() creates/updates/destroys child entities as needed.
// =============================================================================

export namespace Graphics::VectorFieldManager
{
    /// Domain used by a vector-field source entry.
    using Domain = Graphics::VectorFieldDomain;

    /// Synchronize vector field child Graph entities for a source entity.
    ///
    /// @param registry     ECS registry
    /// @param sourceEntity Entity that owns the VisualizationConfig
    /// @param positions    Canonical base positions for the selected domain
    /// @param domain       Base-point domain (vertex, edge, face)
    /// @param domainProps  PropertySet for the selected domain
    /// @param config       VisualizationConfig with VectorFields entries
    /// @param sourceName   Name of the source entity (for child naming)
    void SyncVectorFields(
        entt::registry& registry,
        entt::entity sourceEntity,
        std::span<const glm::vec3> positions,
        Domain domain,
        const Geometry::PropertySet& domainProps,
        Graphics::VisualizationConfig& config,
        const std::string& sourceName);

    /// Destroy all vector field child entities owned by this config.
    void DestroyAllVectorFields(
        entt::registry& registry,
        Graphics::VisualizationConfig& config);
}
