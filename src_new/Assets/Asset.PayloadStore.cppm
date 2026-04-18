module;

#include <span>
#include <unordered_map>
#include <mutex>
#include <array>
#include <atomic>
#include <memory>

export module Extrinsic.Asset.PayloadStore;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.TypePool;

namespace Extrinsic::Assets
{
    export struct PayloadTicket
    {
        uint64_t slot = 0;
        uint64_t generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return slot != 0; }
        auto operator<=>(const PayloadTicket&) const = default;
    };

    export class AssetPayloadStore
    {
    public:
        template <class T>
        [[nodiscard]] Core::Expected<PayloadTicket> Publish(AssetId id, T&& value);

        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> ReadSpan(AssetId id) const;

        [[nodiscard]] Core::Expected<PayloadTicket> GetTicket(AssetId id) const;

        Core::Result Retire(AssetId id);

        [[nodiscard]] std::size_t Size() const;

    private:
        using TypeId = TypePools<AssetId>::TypeId;
        static constexpr std::size_t kShardCount = 32;

        struct Entry
        {
            PayloadTicket ticket{};
            TypeId typeId = 0;
            std::shared_ptr<const void> payload{};
            std::size_t count = 0;
            const void* (*dataFn)(const std::shared_ptr<const void>&) = nullptr;
        };

        struct Shard
        {
            mutable std::mutex mutex{};
            std::unordered_map<AssetId, Entry> entries{};
        };

        [[nodiscard]] static std::size_t ShardIndex(AssetId id) noexcept;
        std::array<Shard, kShardCount> m_Shards{};
        std::atomic<uint64_t> m_NextSlot{1};
    };

    template <class T>
    Core::Expected<PayloadTicket> AssetPayloadStore::Publish(AssetId id, T&& value)
    {
        using StoredT = std::remove_cvref_t<T>;
        if (!id.IsValid())
        {
            return Core::Err<PayloadTicket>(Core::ErrorCode::InvalidArgument);
        }

        const auto newTypeId = TypePools<AssetId>::Type<StoredT>();
        auto payload = std::make_shared<StoredT>(std::forward<T>(value));
        auto dataFn = +[](const std::shared_ptr<const void>& p) -> const void*
        {
            return static_cast<const StoredT*>(p.get());
        };
        auto& shard = m_Shards[ShardIndex(id)];
        std::scoped_lock lock(shard.mutex);
        auto& entry = shard.entries[id];
        if (entry.ticket.slot == 0)
        {
            entry.ticket.slot = m_NextSlot.fetch_add(1, std::memory_order_relaxed);
            entry.ticket.generation = 1;
        }
        else
        {
            ++entry.ticket.generation;
        }

        entry.typeId = newTypeId;
        entry.payload = std::move(payload);
        entry.count = 1;
        entry.dataFn = dataFn;

        return entry.ticket;
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetPayloadStore::ReadSpan(AssetId id) const
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto& shard = m_Shards[ShardIndex(id)];
        std::scoped_lock lock(shard.mutex);
        const auto entryIt = shard.entries.find(id);
        if (entryIt == shard.entries.end())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::AssetNotLoaded);
        }

        if (entryIt->second.typeId != TypePools<AssetId>::Type<StoredT>())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::TypeMismatch);
        }

        if (!entryIt->second.payload || entryIt->second.dataFn == nullptr)
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }

        const auto* typed = static_cast<const StoredT*>(entryIt->second.dataFn(entryIt->second.payload));
        return std::span<const T>(typed, entryIt->second.count);
    }
}
