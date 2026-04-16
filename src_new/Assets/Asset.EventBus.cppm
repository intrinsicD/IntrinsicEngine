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
        using Listener = std::function<void(AssetId, AssetEvent)>;
        [[nodiscard]] uint32_t Subscribe(AssetId id, Listener cb);
        void Unsubscribe(AssetId id, uint32_t token);
        void Publish(AssetId id, AssetEvent ev);
        void Flush(); // main-thread fanout
    };
}
