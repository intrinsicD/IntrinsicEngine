module;
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

export module ECS:Components.Transform;

export namespace ECS::Components::Transform
{
    struct Component
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Quaternion for rotation
        glm::vec3 Scale{1.0f};
    };

    [[nodiscard]] glm::mat4 GetMatrix(const Component& transform)
    {
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), transform.Position);
        mat = mat * glm::mat4_cast(transform.Rotation); // Apply rotation
        mat = glm::scale(mat, transform.Scale);
        return mat;
    }
}
