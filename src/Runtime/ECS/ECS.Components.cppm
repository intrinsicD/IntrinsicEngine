module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <memory>

export module ECS:Components;

export namespace ECS::Tag
{
    struct Component
    {
        std::string Name;
    };
}

export namespace ECS::Transform
{
    struct Component
    {
        glm::vec3 Position{0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Euler angles for simplicity now
        glm::vec3 Scale{1.0f};

        [[nodiscard]] glm::mat4 GetTransform() const
        {
            glm::mat4 mat = glm::translate(glm::mat4(1.0f), Position);
            mat = mat * glm::mat4_cast(Rotation); // Multiply on right to apply rotation after translation
            mat = glm::scale(mat, Scale);
            return mat;
        }
    };

    struct Rotator
    {
        static Rotator X() { return {45.0f, {1.0f, 0.0f, 0.0f}}; }
        static Rotator Y() { return {45.0f, {0.0f, 1.0f, 0.0f}}; }
        static Rotator Z() { return {45.0f, {0.0f, 0.0f, 1.0f}}; }

        float Speed = 45.0f;
        glm::vec3 axis{0.0f, 1.0f, 0.0f};
    };

    void OnUpdate(Component& transform, const Rotator& rotator, float dt)
    {
        //rotate around y-axis (Rotation is a quaternion)
        glm::quat deltaRotation = glm::angleAxis(glm::radians(rotator.Speed * dt), rotator.axis);
        transform.Rotation = glm::normalize(deltaRotation * transform.Rotation);
    }
}

