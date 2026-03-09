module;

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <string>
#include <cstddef>

export module ECS:Scene;

export namespace ECS
{
    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        entt::entity CreateEntity(const std::string& name);

        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

        entt::dispatcher& GetDispatcher() { return m_Dispatcher; }
        const entt::dispatcher& GetDispatcher() const { return m_Dispatcher; }

        [[nodiscard]] size_t Size() const;

    private:
        entt::registry m_Registry;
        entt::dispatcher m_Dispatcher;
    };
}
