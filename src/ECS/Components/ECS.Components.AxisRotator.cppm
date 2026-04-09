module;
#include <glm/glm.hpp>

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
}