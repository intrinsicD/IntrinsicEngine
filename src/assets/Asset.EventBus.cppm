module;
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

export module Extrinsic.Asset.EventBus;

import Extrinsic.Asset.Registry;

export namespace Extrinsic::Assets
{
    enum class AssetEvent : uint8_t { Ready, Failed, Reloaded, Destroyed };

    class AssetEventBus
    {
    public:
        using ListenerCallback = std::function<void(AssetId, AssetEvent)>;
        using ListenerToken = uint32_t;
        // 0 is reserved for "invalid / failed subscribe".
        static constexpr ListenerToken InvalidToken = 0u;

        AssetEventBus();
        ~AssetEventBus();

        [[nodiscard]] ListenerToken Subscribe(AssetId id, ListenerCallback cb);
        [[nodiscard]] ListenerToken SubscribeAll(ListenerCallback cb);
        void Unsubscribe(AssetId id, ListenerToken token);
        void UnsubscribeAll(ListenerToken token);
        void Publish(AssetId id, AssetEvent ev);
        void Flush(); // main-thread fanout
        [[nodiscard]] std::size_t PendingCount() const;

    private:
        struct QueuedEvent
        {
            AssetId id{};
            AssetEvent ev = AssetEvent::Ready;
        };

        mutable std::mutex m_Mutex{};
        std::atomic<uint32_t> m_NextToken{1};
        std::unordered_map<AssetId, std::unordered_map<ListenerToken, ListenerCallback>, AssetIdHash> m_Listeners;
        std::unordered_map<ListenerToken, ListenerCallback> m_BroadcastListeners;
        std::vector<QueuedEvent> m_PendingEvents;
    };
}
