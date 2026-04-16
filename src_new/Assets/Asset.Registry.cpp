module;

#include <cstdint>

module Extrinsic.Asset.Registry;

namespace Extrinsic::Assets
{
    [[nodiscard]] Core::Expected<AssetId> AssetRegistry::Create(uint32_t pathHash, uint32_t typeId)
    {
    }

    [[nodiscard]] bool AssetRegistry::IsAlive(AssetId id) const noexcept
    {
    }

    [[nodiscard]] Core::Expected<AssetMeta> AssetRegistry::GetMeta(AssetId id) const
    {
    }

    [[nodiscard]] Core::Result AssetRegistry::SetState(AssetId id, AssetState expected, AssetState next)
    {
    }

    [[nodiscard]] Core::Result AssetRegistry::Destroy(AssetId id)
    {
    }
}
