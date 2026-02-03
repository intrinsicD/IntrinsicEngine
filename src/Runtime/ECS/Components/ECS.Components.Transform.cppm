module;
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
}
