module;

#include <entt/entity/registry.hpp>
#include <string>

export module ECS:SceneBootstrap;

export namespace ECS::SceneBootstrap
{
    /// Emplace the default set of components on a freshly created entity.
    /// This is the single source of truth for the entity default-component contract:
    /// NameTag, Transform (local + world + dirty), Hierarchy.
    void EmplaceDefaults(entt::registry& registry, entt::entity entity, const std::string& name);
}
