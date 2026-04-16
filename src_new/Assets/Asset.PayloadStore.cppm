module;

#include <cstdint>
#include <span>

export module Extrinsic.Asset.PayloadStore;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;

namespace Extrinsic::Assets
{
  struct PayloadTicket { uint32_t slot = 0; uint32_t generation = 0; };

  class AssetPayloadStore {
  public:
    template<class T>
    [[nodiscard]] Core::Expected<PayloadTicket> Publish(AssetId id, T&& value);

    template<class T>
    [[nodiscard]] Core::Expected<std::span<const T>> ReadSpan(AssetId id) const;

    [[nodiscard]] Core::Result Retire(AssetId id);
  };

  template <class T>
  Core::Expected<PayloadTicket> AssetPayloadStore::Publish(AssetId id, T&& value)
  {

  }

  template <class T>
  Core::Expected<std::span<const T>> AssetPayloadStore::ReadSpan(AssetId id) const
  {

  }
}
