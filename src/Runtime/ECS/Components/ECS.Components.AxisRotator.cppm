module;
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

export module ECS:Components.AxisRotator;
import :Components.Transform;

export namespace ECS::Components::AxisRotator
{
    struct Component
    {
        static Component X() { return {45.0f, {1.0f, 0.0f, 0.0f}}; }
        static Component Y() { return {45.0f, {0.0f, 1.0f, 0.0f}}; }
        static Component Z() { return {45.0f, {0.0f, 0.0f, 1.0f}}; }

        float Speed = 45.0f;
        glm::vec3 axis{0.0f, 1.0f, 0.0f};
    };

    void OnUpdate(Transform::Component& transform, const Component& rotator, float dt)
    {
        //rotate around y-axis (Rotation is a quaternion)
        glm::quat deltaRotation = glm::angleAxis(glm::radians(rotator.Speed * dt), rotator.axis);
        transform.Rotation = glm::normalize(deltaRotation * transform.Rotation);
    }
}