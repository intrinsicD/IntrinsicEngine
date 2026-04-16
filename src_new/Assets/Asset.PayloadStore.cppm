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

namespace Extrinsic::Assets
{
    struct PayloadTicket
    {
        uint32_t slot = 0;
        uint32_t generation = 0;
    };

    export class AssetPayloadStore
    {
    public:
        template <class T>
        [[nodiscard]] Core::Expected<PayloadTicket> Publish(AssetId id, T&& value);

        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> ReadSpan(AssetId id) const;

        [[nodiscard]] Core::Result Retire(AssetId id);

    private:
        using TypeId = std::uintptr_t;

        template <typename T>
        struct TypeInfo
        {
            static TypeId Id() noexcept
            {
                static char s_TypeTag;
                return reinterpret_cast<TypeId>(&s_TypeTag);
            }
        };

        struct Entry
        {
            PayloadTicket ticket{};
            TypeId typeId = 0;
        };

        struct ITypePool
        {
            virtual ~ITypePool() = default;
            [[nodiscard]] virtual TypeId PoolType() const noexcept = 0;
            virtual bool Erase(AssetId id) = 0;
        };

        template <class T>
        struct TypePool final : ITypePool
        {
            std::unordered_map<AssetId, std::vector<T>> values{};

            [[nodiscard]] TypeId PoolType() const noexcept override
            {
                return TypeInfo<T>::Id();
            }

            bool Erase(AssetId id) override
            {
                return values.erase(id) > 0;
            }
        };

        template <class T>
        [[nodiscard]] TypePool<std::remove_cvref_t<T>>& GetOrCreatePool();

        template <class T>
        [[nodiscard]] const TypePool<std::remove_cvref_t<T>>* TryGetPool() const;

        mutable std::mutex m_Mutex{};
        std::unordered_map<AssetId, Entry> m_Entries{};
        std::unordered_map<TypeId, std::unique_ptr<ITypePool>> m_Pools{};
        std::atomic<uint32_t> m_NextSlot{1};
    };

    template <class T>
    auto AssetPayloadStore::GetOrCreatePool() -> TypePool<std::remove_cvref_t<T>>&
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto typeId = TypeInfo<StoredT>::Id();
        auto it = m_Pools.find(typeId);
        if (it == m_Pools.end())
        {
            auto pool = std::make_unique<TypePool<StoredT>>();
            auto* ptr = pool.get();
            m_Pools.emplace(typeId, std::move(pool));
            return *ptr;
        }

        return *static_cast<TypePool<StoredT>*>(it->second.get());
    }

    template <class T>
    auto AssetPayloadStore::TryGetPool() const -> const TypePool<std::remove_cvref_t<T>>*
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto typeId = TypeInfo<StoredT>::Id();
        const auto it = m_Pools.find(typeId);
        if (it == m_Pools.end())
        {
            return nullptr;
        }
        return static_cast<const TypePool<StoredT>*>(it->second.get());
    }

    template <class T>
    Core::Expected<PayloadTicket> AssetPayloadStore::Publish(AssetId id, T&& value)
    {
        using StoredT = std::remove_cvref_t<T>;
        std::scoped_lock lock(m_Mutex);

        auto& pool = GetOrCreatePool<StoredT>();
        auto& entry = m_Entries[id];

        if (entry.ticket.slot == 0)
        {
            entry.ticket.slot = m_NextSlot.fetch_add(1, std::memory_order_relaxed);
            entry.ticket.generation = 1;
        }
        else
        {
            ++entry.ticket.generation;
        }

        entry.typeId = TypeInfo<StoredT>::Id();
        pool.values[id] = std::vector<StoredT>{std::forward<T>(value)};
        return entry.ticket;
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetPayloadStore::ReadSpan(AssetId id) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto entryIt = m_Entries.find(id);
        if (entryIt == m_Entries.end())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::AssetNotLoaded);
        }

        if (entryIt->second.typeId != TypeInfo<T>::Id())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::TypeMismatch);
        }

        const auto* pool = TryGetPool<T>();
        if (pool == nullptr)
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }

        const auto valueIt = pool->values.find(id);
        if (valueIt == pool->values.end())
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }

        return std::span<const T>(valueIt->second.data(), valueIt->second.size());
    }
}
