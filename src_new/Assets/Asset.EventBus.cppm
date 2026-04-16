module;

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

export module Extrinsic.Asset.EventBus;

import Extrinsic.Asset.Registry;

export namespace Extrinsic::Assets
{
    enum class AssetEvent : uint8_t
    {
        Ready,      // Payload published and asset transitioned to Ready.
        Failed,     // Load/Reload/GpuUpload failed. Payload may be stale or absent.
        Reloaded,   // Reload succeeded and payload was replaced.
        // The id attached to a Destroyed event is already dead in the registry.
        // Subscribers MUST NOT call Read/GetMeta/Reload with it - use the id
        // only for bookkeeping (e.g. pruning local caches).
        Destroyed
    };

    class AssetEventBus
    {
    public:
        using ListenerCallback = std::function<void(AssetId, AssetEvent)>;
        using ListenerToken = uint32_t;

        // 0 is reserved for "invalid / failed subscribe".
        static constexpr ListenerToken InvalidToken = 0u;

        // Subscribe to a specific asset id. Returns InvalidToken when cb is empty.
        [[nodiscard]] ListenerToken Subscribe(AssetId id, ListenerCallback cb);

        // Subscribe to every event published through the bus. Returns InvalidToken
        // when cb is empty.
        [[nodiscard]] ListenerToken SubscribeAll(ListenerCallback cb);

        // Unsubscribe by token. Safe to call with an unknown token.
        void Unsubscribe(AssetId id, ListenerToken token);
        void UnsubscribeAll(ListenerToken token);

        // Enqueue an event. Does not invoke callbacks.
        void Publish(AssetId id, AssetEvent ev);

        // Drain the pending event queue and invoke subscribers.
        // Must be called on the main thread.
        void Flush();

        [[nodiscard]] std::size_t PendingCount() const;

    private:
        struct QueuedEvent
        {
            AssetId id{};
            AssetEvent ev = AssetEvent::Ready;
        };

        mutable std::mutex m_Mutex{};
        // Tokens start at 1 - 0 is reserved as InvalidToken.
        std::atomic<uint32_t> m_NextToken{1};
        std::unordered_map<AssetId, std::unordered_map<ListenerToken, ListenerCallback>> m_Listeners;
        std::unordered_map<ListenerToken, ListenerCallback> m_BroadcastListeners;
        std::vector<QueuedEvent> m_PendingEvents;
    };
}
