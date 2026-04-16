module;
#include <cstdint>

module Extrinsic.Asset.EventBus;

import Extrinsic.Asset.Registry;

namespace Extrinsic::Assets
{
    [[nodiscard]] uint32_t AssetEventBus::Subscribe(AssetId id, Listener cb)
    {
    }

    void AssetEventBus::Unsubscribe(AssetId id, uint32_t token)
    {
    }

    void AssetEventBus::Publish(AssetId id, AssetEvent ev)
    {
    }

    void AssetEventBus::Flush();
    {
    }
}
