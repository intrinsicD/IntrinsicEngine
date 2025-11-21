module;
#include <entt/entt.hpp>

export module Runtime.ECS.Scene;

import Runtime.ECS.Components;

export namespace Runtime::ECS
{
    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        entt::entity CreateEntity(const std::string& name)
        {
            entt::entity e = m_Registry.create();
            m_Registry.emplace<TagComponent>(e, name);
            m_Registry.emplace<TransformComponent>(e);
            return e;
        }

        entt::registry& GetRegistry() { return m_Registry; }

    private:
        entt::registry m_Registry;
    };
}
