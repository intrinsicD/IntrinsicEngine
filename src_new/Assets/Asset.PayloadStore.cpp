module;

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

module Extrinsic.Asset.PayloadStore;

namespace Extrinsic::Assets
{
    struct AssetPayloadStore::Impl
    {
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

        std::array<Shard, kShardCount> shards{};
        std::atomic<uint64_t> nextSlot{1};
    };

    AssetPayloadStore::AssetPayloadStore()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    AssetPayloadStore::~AssetPayloadStore() = default;

    std::size_t AssetPayloadStore::ShardIndex(const AssetId id) noexcept
    {
        return (static_cast<std::size_t>(id.Index) ^ (static_cast<std::size_t>(id.Generation) << 1)) % kShardCount;
    }

    Core::Expected<PayloadTicket> AssetPayloadStore::PublishUntyped(
        AssetId id,
        TypeId typeId,
        std::shared_ptr<const void> payload,
        std::size_t count,
        const void* (*dataFn)(const std::shared_ptr<const void>&))
    {
        if (!id.IsValid())
        {
            return Core::Err<PayloadTicket>(Core::ErrorCode::InvalidArgument);
        }

        auto& shard = m_Impl->shards[ShardIndex(id)];
        std::scoped_lock lock(shard.mutex);
        auto& entry = shard.entries[id];
        if (entry.ticket.slot == 0)
        {
            entry.ticket.slot = m_Impl->nextSlot.fetch_add(1, std::memory_order_relaxed);
            entry.ticket.generation = 1;
        }
        else
        {
            ++entry.ticket.generation;
        }

        entry.typeId = typeId;
        entry.payload = std::move(payload);
        entry.count = count;
        entry.dataFn = dataFn;
        return entry.ticket;
    }

    Core::Expected<AssetPayloadStore::ReadView> AssetPayloadStore::ReadUntyped(AssetId id, TypeId typeId) const
    {
        const auto& shard = m_Impl->shards[ShardIndex(id)];
        std::scoped_lock lock(shard.mutex);
        const auto entryIt = shard.entries.find(id);
        if (entryIt == shard.entries.end())
        {
            return Core::Err<ReadView>(Core::ErrorCode::AssetNotLoaded);
        }

        const auto& entry = entryIt->second;
        if (entry.typeId != typeId)
        {
            return Core::Err<ReadView>(Core::ErrorCode::TypeMismatch);
        }

        if (!entry.payload || entry.dataFn == nullptr)
        {
            return Core::Err<ReadView>(Core::ErrorCode::ResourceNotFound);
        }

        return ReadView{.ptr = entry.dataFn(entry.payload), .count = entry.count};
    }

    Core::Result AssetPayloadStore::Retire(AssetId id)
    {
        auto& shard = m_Impl->shards[ShardIndex(id)];
        std::scoped_lock lock(shard.mutex);
        const auto it = shard.entries.find(id);
        if (it == shard.entries.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        shard.entries.erase(it);
        return Core::Ok();
    }

    Core::Expected<PayloadTicket> AssetPayloadStore::GetTicket(AssetId id) const
    {
        const auto& shard = m_Impl->shards[ShardIndex(id)];
        std::scoped_lock lock(shard.mutex);
        const auto it = shard.entries.find(id);
        if (it == shard.entries.end())
        {
            return Core::Err<PayloadTicket>(Core::ErrorCode::AssetNotLoaded);
        }
        return it->second.ticket;
    }

    std::size_t AssetPayloadStore::Size() const
    {
        std::size_t total = 0;
        for (const auto& shard : m_Impl->shards)
        {
            std::scoped_lock lock(shard.mutex);
            total += shard.entries.size();
        }
        return total;
    }
}
