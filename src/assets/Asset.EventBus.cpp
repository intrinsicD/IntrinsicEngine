module;

#include <mutex>
#include <utility>
#include <vector>
#include <atomic>
#include <unordered_set>
#include <unordered_map>

module Extrinsic.Asset.EventBus;

namespace Extrinsic::Assets
{
    AssetEventBus::AssetEventBus() = default;
    AssetEventBus::~AssetEventBus() = default;

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
        std::vector<ListenerCallback> broadcastCallbacks;
        std::unordered_map<AssetId, std::vector<ListenerCallback>, AssetIdHash> perAssetCallbacks;
        {
            std::scoped_lock lock(m_Mutex);
            if (m_PendingEvents.empty())
            {
                return;
            }
            events.swap(m_PendingEvents);
            broadcastCallbacks.reserve(m_BroadcastListeners.size());
            for (const auto& [_, cb] : m_BroadcastListeners)
            {
                broadcastCallbacks.push_back(cb);
            }

            std::unordered_set<AssetId, AssetIdHash> ids;
            ids.reserve(events.size());
            for (const auto& evt : events)
            {
                ids.insert(evt.id);
            }
            perAssetCallbacks.reserve(ids.size());
            for (const auto id : ids)
            {
                const auto it = m_Listeners.find(id);
                if (it == m_Listeners.end())
                {
                    continue;
                }
                auto& dst = perAssetCallbacks[id];
                dst.reserve(it->second.size());
                for (const auto& [_, cb] : it->second)
                {
                    dst.push_back(cb);
                }
            }
        }

        for (const auto& evt : events)
        {
            for (const auto& cb : broadcastCallbacks)
            {
                cb(evt.id, evt.ev);
            }
            if (const auto it = perAssetCallbacks.find(evt.id); it != perAssetCallbacks.end())
            {
                for (const auto& cb : it->second)
                {
                    cb(evt.id, evt.ev);
                }
            }
        }
    }

    void AssetEventBus::Flush(AssetId id)
    {
        std::vector<QueuedEvent> events;
        std::vector<ListenerCallback> broadcastCallbacks;
        std::vector<ListenerCallback> perAssetCallbacks;
        {
            std::scoped_lock lock(m_Mutex);
            if (m_PendingEvents.empty())
            {
                return;
            }

            std::vector<QueuedEvent> remaining;
            remaining.reserve(m_PendingEvents.size());
            events.reserve(m_PendingEvents.size());
            for (const QueuedEvent& evt : m_PendingEvents)
            {
                if (evt.id == id)
                {
                    events.push_back(evt);
                }
                else
                {
                    remaining.push_back(evt);
                }
            }
            if (events.empty())
            {
                return;
            }
            m_PendingEvents.swap(remaining);

            broadcastCallbacks.reserve(m_BroadcastListeners.size());
            for (const auto& [_, cb] : m_BroadcastListeners)
            {
                broadcastCallbacks.push_back(cb);
            }

            if (const auto it = m_Listeners.find(id); it != m_Listeners.end())
            {
                perAssetCallbacks.reserve(it->second.size());
                for (const auto& [_, cb] : it->second)
                {
                    perAssetCallbacks.push_back(cb);
                }
            }
        }

        for (const QueuedEvent& evt : events)
        {
            for (const auto& cb : broadcastCallbacks)
            {
                cb(evt.id, evt.ev);
            }
            for (const auto& cb : perAssetCallbacks)
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
