module;

#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

module Extrinsic.ECS.Component.Transform;

namespace Extrinsic::ECS::Components::Transform
{
    namespace
    {
        bool IsFiniteMatrix(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row]))
                        return false;
            return true;
        }

        // Translation magnitude must not mask basis shear or perspective.
        bool MatchesTrs(const glm::mat4& reconstructed, const glm::mat4& expected)
        {
            constexpr float kAffineTolerance = 1.0e-6f;
            constexpr float kAbsTolerance = 1.0e-5f;
            constexpr float kRelTolerance = 1.0e-4f;
            for (int column = 0; column < 4; ++column)
            {
                if (!(std::abs(expected[column][3] - (column == 3 ? 1.0f : 0.0f)) <= kAffineTolerance))
                    return false;
                float magnitude = 0.0f;
                for (int row = 0; row < 3; ++row)
                    magnitude = std::max(magnitude, std::abs(expected[column][row]));
                const float tolerance = kAbsTolerance + kRelTolerance * magnitude;
                for (int row = 0; row < 3; ++row)
                    if (!(std::abs(reconstructed[column][row] - expected[column][row]) <= tolerance))
                        return false;
            }
            return true;
        }
    }

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
        if (!IsFiniteMatrix(matrix))
            return false;

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

        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return false;
        const float rotationLength = glm::length(rotation);
        if (!std::isfinite(rotationLength) || rotationLength <= 0.0f)
            return false;
        rotation /= rotationLength;
        if (!std::isfinite(rotation.w) || !std::isfinite(rotation.x) ||
            !std::isfinite(rotation.y) || !std::isfinite(rotation.z))
            return false;

        outTransform.Position = position;
        outTransform.Rotation = rotation;
        outTransform.Scale = scale;
        return true;
    }


    [[nodiscard]] bool TryComputeLocalTransform(const glm::mat4& worldMatrix,
                                                const glm::mat4& parentWorldMatrix,
                                                Component& outLocalTransform)
    {
        if (!IsFiniteMatrix(parentWorldMatrix))
            return false;
        const float parentDeterminant = glm::determinant(parentWorldMatrix);
        if (!std::isfinite(parentDeterminant) || std::abs(parentDeterminant) < 1e-8f)
            return false;

        const glm::mat4 localMatrix = glm::inverse(parentWorldMatrix) * worldMatrix;
        Component candidate;
        if (!TryDecomposeMatrix(localMatrix, candidate) ||
            !MatchesTrs(GetMatrix(candidate), localMatrix))
            return false;
        outLocalTransform = candidate;
        return true;
    }
}
