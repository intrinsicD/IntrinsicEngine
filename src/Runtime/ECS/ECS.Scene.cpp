module;
#include <filesystem>
#include <chrono>
#include <entt/entity/registry.hpp>

module ECS:Scene.Impl;
import :Scene;
import :Components;

namespace ECS
{
    entt::entity Scene::CreateEntity(const std::string& name)
    {
        entt::entity e = m_Registry.create();
        m_Registry.emplace<Components::NameTag::Component>(e, name);
        m_Registry.emplace<Components::Transform::Component>(e);
        m_Registry.emplace<Components::Transform::WorldMatrix>(e);
        m_Registry.emplace<Components::Transform::IsDirtyTag>(e);
        m_Registry.emplace<Components::Hierarchy::Component>(e);
        return e;
    }

    [[nodiscard]] size_t Scene::Size() const { return m_Registry.storage<entt::entity>()->size(); }
}
