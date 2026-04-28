module;
#define GLM_ENABLE_EXPERIMENTAL
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

export module ECS:Components.Transform;

export namespace ECS::Components::Transform
{
    // Core transform data - kept minimal for cache efficiency
    // Use IsDirtyTag component for dirty tracking instead of bool member
    struct Component
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Quaternion for rotation
        glm::vec3 Scale{1.0f};
        // NOTE: Dirty state tracked via IsDirtyTag component, not bool member
        //       This improves cache efficiency in transform iteration
    };

    // Tag component for dirty tracking - zero size, just marks entity
    // Usage: registry.emplace_or_replace<IsDirtyTag>(entity) when transform changes
    //        registry.view<Component, IsDirtyTag>() to iterate dirty transforms
    struct IsDirtyTag
    {
    };

    // Tag component for downstream systems that need to know which entities had their
    // WorldMatrix recomputed this tick.
    // Contract:
    //  - Added by TransformSystem when it writes WorldMatrix.
    //  - Cleared by the consumer system once processed (e.g., GPU scene sync).
    struct WorldUpdatedTag
    {
    };

    struct WorldMatrix
    {
        glm::mat4 Matrix{1.0f};
    };

    [[nodiscard]] glm::mat4 GetMatrix(const Component& transform)
    {
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), transform.Position);
        mat = mat * glm::mat4_cast(transform.Rotation); // Apply rotation
        mat = glm::scale(mat, transform.Scale);
        return mat;
    }

    [[nodiscard]] bool TryDecomposeMatrix(const glm::mat4& matrix, Component& outTransform)
    {
        glm::vec3 skew{0.0f};
        glm::vec4 perspective{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 position{0.0f};

        if (!glm::decompose(matrix, scale, rotation, position, skew, perspective))
            return false;

        const glm::vec4 rotationVec{rotation.x, rotation.y, rotation.z, rotation.w};
        const auto isFiniteVec3 = [](const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        const auto isFiniteVec4 = [](const glm::vec4& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
        };
        if (glm::any(glm::isnan(position)) || glm::any(glm::isnan(scale)) || glm::any(glm::isnan(rotationVec)) ||
            !isFiniteVec3(position) || !isFiniteVec3(scale) || !isFiniteVec4(rotationVec))
        {
            return false;
        }

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
