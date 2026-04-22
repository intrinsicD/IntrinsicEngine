module;

#include <glm/glm.hpp>

export module Extrinsic.ECS.Component.Transform.WorldMatrix;

export namespace Extrinsic::ECS::Components::Transform
{
    struct WorldMatrix
    {
        glm::mat4 Matrix{1.0f};
    };
}