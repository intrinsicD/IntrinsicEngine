module;
#include <entt/entity/registry.hpp>

module ECS:Scene.Impl;
import :Scene;
import :SceneBootstrap;

namespace ECS
{
    entt::entity Scene::CreateEntity(const std::string& name)
    {
        entt::entity e = m_Registry.create();
        SceneBootstrap::EmplaceDefaults(m_Registry, e, name);
        return e;
    }

    [[nodiscard]] size_t Scene::Size() const { return m_Registry.storage<entt::entity>()->free_list(); }
}
