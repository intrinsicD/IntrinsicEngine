module;
#include <entt/fwd.hpp>

export module ECS:Systems.Transform;

export namespace ECS::Systems::Transform
{
    void OnUpdate(entt::registry& registry);
}
