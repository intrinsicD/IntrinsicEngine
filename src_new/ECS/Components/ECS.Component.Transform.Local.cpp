module;

#define GLM_ENABLE_EXPERIMENTAL
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

module Extrinsic.ECS.Components.Transform;

namespace Extrinsic::ECS::Components
{
    [[nodiscard]] glm::mat4 GetMatrix(const Transform& transform)
    {
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), transform.Position);
        mat = mat * glm::mat4_cast(transform.Rotation); // Apply rotation
        mat = glm::scale(mat, transform.Scale);
        return mat;
    }

    [[nodiscard]] bool TryDecomposeMatrix(const glm::mat4& matrix, Transform& outTransform)
    {
        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 position{0.0f};

        if (!glm::decompose(matrix, scale, rotation, position, skew, perspective))
            return false;

        outTransform.Position = position;
        outTransform.Rotation = rotation;
        outTransform.Scale = scale;
        return true;
    }


    [[nodiscard]] bool TryComputeLocalTransform(const glm::mat4& worldMatrix,
                                                const glm::mat4& parentWorldMatrix,
                                                Transform& outLocalTransform)
    {
        const float parentDeterminant = glm::determinant(parentWorldMatrix);
        if (!std::isfinite(parentDeterminant) || std::abs(parentDeterminant) < 1e-8f)
            return false;

        return TryDecomposeMatrix(glm::inverse(parentWorldMatrix) * worldMatrix, outLocalTransform);
    }
}