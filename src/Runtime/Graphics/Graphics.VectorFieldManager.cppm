module;

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

export module Graphics.VectorFieldManager;

import Graphics.VisualizationConfig;
import Geometry;

// =============================================================================
// VectorFieldManager — creates/updates/destroys child Graph entities for
// vector field visualization overlays.
//
// Each VectorFieldEntry in a VisualizationConfig spawns a child Graph entity
// whose nodes are (base positions + target positions) and whose edges connect
// each base[i] to target[i]. The child Graph entity reuses the full graph
// rendering pipeline (edge colors, node rendering, width, overlay toggle).
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
    /// Synchronize vector field child Graph entities for a source entity.
    ///
    /// @param registry     ECS registry
    /// @param sourceEntity Entity that owns the VisualizationConfig
    /// @param positions    Base point positions (vertex/node/point positions)
    /// @param normals      Optional normals (may be empty)
    /// @param vertexProps  PropertySet to read vec3 properties from
    /// @param config       VisualizationConfig with VectorFields entries
    /// @param sourceName   Name of the source entity (for child naming)
    void SyncVectorFields(
        entt::registry& registry,
        entt::entity sourceEntity,
        std::span<const glm::vec3> positions,
        const Geometry::PropertySet& vertexProps,
        Graphics::VisualizationConfig& config,
        const std::string& sourceName);

    /// Destroy all vector field child entities owned by this config.
    void DestroyAllVectorFields(
        entt::registry& registry,
        Graphics::VisualizationConfig& config);
}
