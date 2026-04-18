module;

#include <string>
#include <mutex>

module Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    std::size_t AssetPathIndex::ShardIndex(const std::string_view absolutePath) noexcept
    {
        return StringHash{}(absolutePath) % kShardCount;
    }

    Core::Expected<AssetId> AssetPathIndex::Find(std::string_view absolutePath) const
    {
        auto& shard = m_Shards[ShardIndex(absolutePath)];
        std::scoped_lock lock(shard.mutex);
        const auto it = shard.index.find(absolutePath);
        if (it == shard.index.end())
        {
            return Core::Err<AssetId>(Core::ErrorCode::ResourceNotFound);
        }
        return it->second;
    }

    Core::Result AssetPathIndex::Insert(std::string_view absolutePath, AssetId id)
    {
        if (absolutePath.empty() || !id.IsValid())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        auto& shard = m_Shards[ShardIndex(absolutePath)];
        std::scoped_lock lock(shard.mutex);
        const auto [_, inserted] = shard.index.emplace(std::string(absolutePath), id);
        if (!inserted)
        {
            return Core::Err(Core::ErrorCode::ResourceBusy);
        }
        return Core::Ok();
    }

    Core::Result AssetPathIndex::Erase(std::string_view absolutePath, AssetId id)
    {
        auto& shard = m_Shards[ShardIndex(absolutePath)];
        std::scoped_lock lock(shard.mutex);
        const auto it = shard.index.find(absolutePath);
        if (it == shard.index.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        if (it->second != id)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        shard.index.erase(it);
        return Core::Ok();
    }

    bool AssetPathIndex::Contains(std::string_view absolutePath) const
    {
        auto& shard = m_Shards[ShardIndex(absolutePath)];
        std::scoped_lock lock(shard.mutex);
        return shard.index.find(absolutePath) != shard.index.end();
    }

    std::size_t AssetPathIndex::Size() const
    {
        std::size_t total = 0;
        for (const auto& shard : m_Shards)
        {
            std::scoped_lock lock(shard.mutex);
            total += shard.index.size();
        }
        return total;
    }
}
