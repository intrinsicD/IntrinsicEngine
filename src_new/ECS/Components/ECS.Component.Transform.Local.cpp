module;

#define GLM_ENABLE_EXPERIMENTAL
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

module Extrinsic.ECS.Component.Transform;

namespace Extrinsic::ECS::Components::Transform
{
    [[nodiscard]] glm::mat4 GetMatrix(const Component& transform)
    {
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), transform.Position);
        mat = mat * glm::mat4_cast(transform.Rotation); // Apply rotation
        mat = glm::scale(mat, transform.Scale);
        return mat;
    }

    [[nodiscard]] bool TryDecomposeMatrix(const glm::mat4& matrix, Component& outTransform)
    {
        constexpr float kScaleEpsilon = 1e-8f;

        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 position{0.0f};

        if (!glm::decompose(matrix, scale, rotation, position, skew, perspective))
            return false;

        if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z))
            return false;
        if (std::abs(scale.x) < kScaleEpsilon ||
            std::abs(scale.y) < kScaleEpsilon ||
            std::abs(scale.z) < kScaleEpsilon)
            return false;

        if (!std::isfinite(rotation.w) || !std::isfinite(rotation.x) ||
            !std::isfinite(rotation.y) || !std::isfinite(rotation.z))
            return false;

        outTransform.Position = position;
        outTransform.Rotation = glm::normalize(rotation);
        outTransform.Scale = scale;
        return true;
    }


    [[nodiscard]] bool TryComputeLocalTransform(const glm::mat4& worldMatrix,
                                                const glm::mat4& parentWorldMatrix,
                                                Component& outLocalTransform)
    {
        const float parentDeterminant = glm::determinant(parentWorldMatrix);
        if (!std::isfinite(parentDeterminant) || std::abs(parentDeterminant) < 1e-8f)
            return false;

        return TryDecomposeMatrix(glm::inverse(parentWorldMatrix) * worldMatrix, outLocalTransform);
    }
}
