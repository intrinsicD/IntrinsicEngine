module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <memory>

export module Runtime.ECS.Components;

import Runtime.RHI.Buffer;
import Runtime.Graphics.Material;
import Runtime.Graphics.Geometry;
import Runtime.Geometry.OBB;

export namespace Runtime::ECS::Tag
{
    struct Component
    {
        std::string Name;
    };
}

export namespace Runtime::ECS::Transform
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

export namespace Runtime::ECS::MeshRenderer
{
    struct Component
    {
        Graphics::GeometryHandle Geometry;
        std::shared_ptr<Graphics::Material> MaterialRef;
    };
}

export namespace Runtime::ECS::MeshCollider
{
    struct Component
    {
        std::shared_ptr<Graphics::GeometryCollisionData> CollisionRef;
        Geometry::OBB WorldOBB;
    };
}
