module;

module Extrinsic.Asset.EventBus;

import Extrinsic.Asset.Registry;

namespace Extrinsic::Assets
{
    AssetEventBus::ListenerToken AssetEventBus::Subscribe(AssetId id, ListenerCallback cb)
    {
    }

    void AssetEventBus::Unsubscribe(AssetId id, ListenerToken token)
    {
    }

    void AssetEventBus::Publish(AssetId id, AssetEvent ev)
    {
    }

    void AssetEventBus::Flush()
    {
    }
}
