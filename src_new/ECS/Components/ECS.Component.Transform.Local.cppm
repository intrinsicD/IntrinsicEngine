module;

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

export module Extrinsic.ECS.Components.Transform;

export namespace Extrinsic::ECS::Components::Transform
{
    struct Component
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f};
    };

    struct WorldUpdatedTag
    {
    };

    [[nodiscard]] glm::mat4 GetMatrix(const Transform& transform);

    [[nodiscard]] bool TryDecomposeMatrix(const glm::mat4& matrix, Transform& outTransform);

    [[nodiscard]] bool TryComputeLocalTransform(const glm::mat4& worldMatrix,
                                                const glm::mat4& parentWorldMatrix,
                                                Transform& outLocalTransform);
}