module;
#include <atomic>
#include <cstdint>
#include <functional>
#include <queue>
#include <mutex>

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

        [[nodiscard]] ListenerToken Subscribe(AssetId id, ListenerCallback cb);
        void Unsubscribe(AssetId id, ListenerToken token);
        void Publish(AssetId id, AssetEvent ev);
        void Flush(); // main-thread fanout
    private:
        struct QueuedEvent
        {
            AssetId id{};
            AssetEvent ev = AssetEvent::Ready;
        };

        mutable std::mutex m_Mutex{};
        std::atomic<uint32_t> m_NextToken{};
        std::unordered_map<AssetId, std::unordered_map<ListenerToken, ListenerCallback>> m_Listeners;
        std::vector<QueuedEvent> m_PendingEvents;
   };
}
