module;
#include <entt/fwd.hpp>

export module ECS:Systems.Transform;
import :Components.Transform;
import :Components.Hierarchy;

export namespace ECS::Systems::Transform
{
    void OnUpdate(entt::registry& registry);
}
