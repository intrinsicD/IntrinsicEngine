module;
#include <cstdint>
#include <functional>

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
   };
}
