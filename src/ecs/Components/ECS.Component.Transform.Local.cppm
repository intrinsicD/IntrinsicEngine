// Defines local TRS state, recompute tags, and checked matrix conversion contracts.
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

    // Stamped by TransformHierarchy or runtime physics writeback on every entity
    // whose world matrix it just rewrote (root or child). Consumers (e.g.,
    // GPU scene sync) clear it once they have processed the update.
    struct WorldUpdatedTag
    {
    };

    [[nodiscard]] glm::mat4 GetMatrix(const Component& transform);

    // Extract finite TRS components, discarding shear/perspective. Reject singular
    // scale or nonfinite input/output; failure leaves outTransform unchanged.
    [[nodiscard]] bool TryDecomposeMatrix(const glm::mat4& matrix, Component& outTransform);

    // Require an invertible finite parent and faithfully reconstructible affine
    // local TRS; shear/perspective failure leaves outLocalTransform unchanged.
    [[nodiscard]] bool TryComputeLocalTransform(const glm::mat4& worldMatrix,
                                                const glm::mat4& parentWorldMatrix,
                                                Component& outLocalTransform);
}
