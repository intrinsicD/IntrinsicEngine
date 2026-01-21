module;
#include <entt/fwd.hpp>

export module ECS:Systems.AxisRotator;

export namespace ECS::Systems::AxisRotator
{
    void OnUpdate(entt::registry& registry, float dt);
}
