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
        m_Registry.emplace<Tag::Component>(e, name);
        m_Registry.emplace<Transform::Component>(e);
        return e;
    }
}