module;

#include <cstdint>
#include <span>
#include <expected>

export module Extrinsic.Asset.Registry;

import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Assets
{
    struct AssetTag;
    using AssetId = Core::StrongHandle<AssetTag>;

    enum class AssetState : uint8_t
    {
        Unloaded, QueuedIO, LoadedCPU, QueuedGPU, Ready, Failed
    };

    struct AssetMeta
    {
        uint32_t pathHash = 0;
        AssetState state = AssetState::Unloaded;
        uint32_t typeId = 0;
        uint32_t payloadSlot = 0;
    };

    class AssetRegistry
    {
    public:
        [[nodiscard]] Core::Expected<AssetId> Create(uint32_t pathHash, uint32_t typeId);
        [[nodiscard]] bool IsAlive(AssetId id) const noexcept;
        [[nodiscard]] Core::Expected<AssetMeta> GetMeta(AssetId id) const;
        [[nodiscard]] Core::Result SetState(AssetId id, AssetState expected, AssetState next);
        [[nodiscard]] Core::Result Destroy(AssetId id);
    };
}
