module;

#include <glm/glm.hpp>

export module Extrinsic.ECS.Component.Light;

import Geometry.Sphere;

export namespace Extrinsic::ECS::Components::Lights
{
    struct LightTag
    {
    };

    struct DirectionalLight
    {
        //Direction from WorldMatrix (rotation row)
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
    };

    struct PointLight
    {
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
    };

    struct SpotLight
    {
        glm::vec3 Direction{0.0f};
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
    };

    struct AmbientLight
    {
        glm::vec3 Color{1.0f};
        Geometry::Sphere SphereBounds;
    };
}
