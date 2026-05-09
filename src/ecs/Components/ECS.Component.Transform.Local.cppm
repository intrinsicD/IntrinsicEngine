module;

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <limits>

export module Extrinsic.ECS.Component.Transform;

export namespace Extrinsic::ECS::Components::Transform
{
    struct Component
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f};
    };

    // CPU recompute marker. Producers stamp this when they mutate a local
    // transform; the promoted TransformHierarchy traversal clears it after
    // recomputing the world matrix. Distinct from the GPU-sync
    // Components::DirtyTags::DirtyTransform tag, which is owned by render-sync.
    struct IsDirtyTag
    {
    };

    // Stamped by the promoted TransformHierarchy traversal on every entity
    // whose world matrix it just rewrote (root or child). Consumers (e.g.,
    // GPU scene sync) clear it once they have processed the update.
    struct WorldUpdatedTag
    {
    };

    [[nodiscard]] glm::mat4 GetMatrix(const Component& transform);

    [[nodiscard]] bool TryDecomposeMatrix(const glm::mat4& matrix, Component& outTransform);

    [[nodiscard]] bool TryComputeLocalTransform(const glm::mat4& worldMatrix,
                                                const glm::mat4& parentWorldMatrix,
                                                Component& outLocalTransform);
}
