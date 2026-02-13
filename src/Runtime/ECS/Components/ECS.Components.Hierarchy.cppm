module;
#include <entt/entity/entity.hpp>
#include <cassert>

export module ECS:Components.Hierarchy;

export namespace ECS::Components::Hierarchy
{
    struct Component
    {
        entt::entity Parent = entt::null;
        entt::entity FirstChild = entt::null;
        entt::entity NextSibling = entt::null;
        entt::entity PrevSibling = entt::null;
        uint32_t ChildCount = 0;
    };

    void Attach(entt::registry& registry, entt::entity child, entt::entity newParent);

    void Detach(entt::registry& registry, entt::entity child);
}
