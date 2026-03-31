module;

#include <entt/entity/registry.hpp>
#include <string>

module ECS:SceneBootstrap.Impl;
import :SceneBootstrap;
import :Components.NameTag;
import :Components.Transform;
import :Components.Hierarchy;

namespace ECS::SceneBootstrap
{
    void EmplaceDefaults(entt::registry& registry, entt::entity entity, const std::string& name)
    {
        registry.emplace<Components::NameTag::Component>(entity, name);
        registry.emplace<Components::Transform::Component>(entity);
        registry.emplace<Components::Transform::WorldMatrix>(entity);
        registry.emplace<Components::Transform::IsDirtyTag>(entity);
        registry.emplace<Components::Hierarchy::Component>(entity);
    }
}
