module;

#include <functional>
#include <string>
#include <mutex>
#include <unordered_map>
#include <array>
#include <memory>

module Extrinsic.Asset.PathIndex;

namespace Extrinsic::Assets
{
    struct AssetPathIndex::Impl
    {
        struct StringHash
        {
            using is_transparent = void;

            [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }

            [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept
            {
                return std::hash<std::string_view>{}(s);
            }
        };

        struct Shard
        {
            mutable std::mutex mutex;
            std::unordered_map<std::string, AssetId, StringHash, std::equal_to<>> index{};
        };

        std::array<Shard, kShardCount> shards{};
    };

    AssetPathIndex::AssetPathIndex()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    AssetPathIndex::~AssetPathIndex() = default;

    std::size_t AssetPathIndex::ShardIndex(const std::string_view absolutePath) noexcept
    {
        return std::hash<std::string_view>{}(absolutePath) % kShardCount;
    }

    Core::Expected<AssetId> AssetPathIndex::Find(std::string_view absolutePath) const
    {
        auto& shard = m_Impl->shards[ShardIndex(absolutePath)];
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

        auto& shard = m_Impl->shards[ShardIndex(absolutePath)];
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
        auto& shard = m_Impl->shards[ShardIndex(absolutePath)];
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
        auto& shard = m_Impl->shards[ShardIndex(absolutePath)];
        std::scoped_lock lock(shard.mutex);
        return shard.index.find(absolutePath) != shard.index.end();
    }

    std::size_t AssetPathIndex::Size() const
    {
        std::size_t total = 0;
        for (const auto& shard : m_Impl->shards)
        {
            std::scoped_lock lock(shard.mutex);
            total += shard.index.size();
        }
        return total;
    }
}
