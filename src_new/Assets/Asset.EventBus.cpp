module;

#include <mutex>
#include <utility>
#include <vector>
#include <atomic>

module Extrinsic.Asset.EventBus;

namespace Extrinsic::Assets
{
    AssetEventBus::ListenerToken AssetEventBus::Subscribe(AssetId id, ListenerCallback cb)
    {
        if (!cb)
        {
            return InvalidToken;
        }

        const ListenerToken token = m_NextToken.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_Mutex);
        m_Listeners[id].emplace(token, std::move(cb));
        return token;
    }

    AssetEventBus::ListenerToken AssetEventBus::SubscribeAll(ListenerCallback cb)
    {
        if (!cb)
        {
            return InvalidToken;
        }

        const ListenerToken token = m_NextToken.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_Mutex);
        m_BroadcastListeners.emplace(token, std::move(cb));
        return token;
    }

    void AssetEventBus::Unsubscribe(AssetId id, ListenerToken token)
    {
        if (token == InvalidToken)
        {
            return;
        }
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Listeners.find(id);
        if (it == m_Listeners.end())
        {
            return;
        }

        it->second.erase(token);
        if (it->second.empty())
        {
            m_Listeners.erase(it);
        }
    }

    void AssetEventBus::UnsubscribeAll(ListenerToken token)
    {
        if (token == InvalidToken)
        {
            return;
        }
        std::scoped_lock lock(m_Mutex);
        m_BroadcastListeners.erase(token);
    }

    void AssetEventBus::Publish(AssetId id, AssetEvent ev)
    {
        std::scoped_lock lock(m_Mutex);
        m_PendingEvents.push_back({id, ev});
    }

    void AssetEventBus::Flush()
    {
        std::vector<QueuedEvent> events;
        {
            std::scoped_lock lock(m_Mutex);
            if (m_PendingEvents.empty())
            {
                return;
            }
            events.swap(m_PendingEvents);
        }


        for (const auto& evt : events)
        {
            std::vector<ListenerCallback> callbacks;
            {
                std::scoped_lock lock(m_Mutex);

                callbacks.reserve(m_BroadcastListeners.size());
                for (const auto& [_, cb] : m_BroadcastListeners)
                {
                    callbacks.push_back(cb);
                }

                const auto it = m_Listeners.find(evt.id);
                if (it != m_Listeners.end())
                {
                    callbacks.reserve(it->second.size() + callbacks.size());
                    for (const auto& [_, cb] : it->second)
                    {
                        callbacks.push_back(cb);
                    }
                }
            }

            for (const auto& cb : callbacks)
            {
                cb(evt.id, evt.ev);
            }
        }
    }

    [[nodiscard]] std::size_t AssetEventBus::PendingCount() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_PendingEvents.size();
    }
}
