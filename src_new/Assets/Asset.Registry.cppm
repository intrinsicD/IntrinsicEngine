module;

#include <cstdint>
#include <mutex>
#include <vector>

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

    struct AssetRegistrySnapshotEntry
    {
        AssetId id{};
        AssetMeta meta{};
    };

    class AssetRegistry
    {
    public:
        AssetRegistry() = default;
        AssetRegistry(const AssetRegistry&) = delete;
        AssetRegistry& operator=(const AssetRegistry&) = delete;

        [[nodiscard]] Core::Expected<AssetId> Create(uint32_t pathHash, uint32_t typeId);
        [[nodiscard]] bool IsAlive(AssetId id) const noexcept;
        [[nodiscard]] Core::Expected<AssetMeta> GetMeta(AssetId id) const;
        [[nodiscard]] Core::Expected<AssetState> GetState(AssetId id) const;
        [[nodiscard]] Core::Result SetState(AssetId id, AssetState expected, AssetState next);
        [[nodiscard]] Core::Result SetPayloadSlot(AssetId id, uint32_t slot);
        [[nodiscard]] Core::Result Destroy(AssetId id);
        [[nodiscard]] std::size_t LiveCount() const noexcept;
        [[nodiscard]] std::size_t Capacity() const noexcept;
        [[nodiscard]] std::vector<AssetRegistrySnapshotEntry> Snapshot() const;

    private:
        [[nodiscard]] bool IsAliveLocked(AssetId id) const noexcept;
        mutable std::mutex m_Mutex{};
        std::vector<uint32_t> m_PathHashes{};
        std::vector<AssetState> m_States{};
        std::vector<uint32_t> m_TypeIds{};
        std::vector<uint32_t> m_PayloadSlots{};
        std::vector<uint32_t> m_Generations{};
        std::vector<uint8_t> m_Allocated{};
        std::vector<uint32_t> m_FreeList{};
        std::size_t m_LiveCount{0};
    };
}
