module;

#include <entt/fwd.hpp>

export module ECS:HierarchyTraversal;

export namespace ECS::Systems::HierarchyTraversal
{
    void UpdateWorldTransforms(entt::registry& registry);
}
