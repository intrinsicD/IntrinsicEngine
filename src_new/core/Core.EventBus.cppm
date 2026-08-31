module;

#include <entt/signal/dispatcher.hpp>

export module Core.EventBus;


export namespace Extrinsic::Core
{
    class EventBus
    {
    public:
        void Drain() noexcept
        {
            m_Dispatcher.update();
        }

        void Clear() noexcept
        {
            m_Dispatcher.clear();
        }

        entt::dispatcher& Raw() noexcept { return m_Dispatcher; }

    private:
        entt::dispatcher m_Dispatcher;
    };
}
