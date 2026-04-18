module;

#include <mutex>

module Extrinsic.Asset.PayloadStore;

namespace Extrinsic::Assets
{
    std::size_t AssetPayloadStore::ShardIndex(const AssetId id) noexcept
    {
        return (static_cast<std::size_t>(id.Index) ^ (static_cast<std::size_t>(id.Generation) << 1)) % kShardCount;
    }

    Core::Result AssetPayloadStore::Retire(AssetId id)
    {
        auto& shard = m_Shards[ShardIndex(id)];
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
        const auto& shard = m_Shards[ShardIndex(id)];
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
        for (const auto& shard : m_Shards)
        {
            std::scoped_lock lock(shard.mutex);
            total += shard.entries.size();
        }
        return total;
    }
}
