module;

#include <span>
#include <unordered_map>
#include <mutex>
#include <vector>
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

        struct Entry
        {
            PayloadTicket ticket{};
            TypeId typeId = 0;
        };

        mutable std::mutex m_Mutex{};
        std::unordered_map<AssetId, Entry> m_Entries{};
        TypePools<AssetId> m_TypePools{};
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

        std::scoped_lock lock(m_Mutex);

        auto& entry = m_Entries[id];
        const auto newTypeId = TypePools<AssetId>::Type<StoredT>();

        // If an entry with a different type exists, clear the old pool bucket
        // so we do not leak a stale vector in the previous TypePool.
        if (entry.typeId != 0 && entry.typeId != newTypeId)
        {
            (void)m_TypePools.Erase(entry.typeId, id);
        }

        auto& pool = m_TypePools.GetOrCreate<StoredT>();
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
        pool[id] = std::vector<StoredT>{std::forward<T>(value)};

        return entry.ticket;
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetPayloadStore::ReadSpan(AssetId id) const
    {
        using StoredT = std::remove_cvref_t<T>;
        std::scoped_lock lock(m_Mutex);
        const auto entryIt = m_Entries.find(id);
        if (entryIt == m_Entries.end())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::AssetNotLoaded);
        }

        if (entryIt->second.typeId != TypePools<AssetId>::Type<StoredT>())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::TypeMismatch);
        }

        const auto* poolValues = m_TypePools.TryGet<StoredT>();
        if (poolValues == nullptr)
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }

        const auto valueIt = poolValues->find(id);
        if (valueIt == poolValues->end())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }

        return std::span<const T>(valueIt->second.data(), valueIt->second.size());
    }
}
