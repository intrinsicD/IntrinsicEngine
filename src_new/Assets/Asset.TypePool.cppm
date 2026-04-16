module;

#include <memory>
#include <unordered_map>
#include <vector>
#include <type_traits>

export module Extrinsic.Asset.TypePool;

export namespace Extrinsic::Assets
{
    template <typename Key>
    class TypePools
    {
    public:
        using TypeId = std::uintptr_t;

        template <class T>
        [[nodiscard]] static TypeId Type() noexcept
        {
            // Per-type unique tag: its address is a stable, RTTI-free type id.
            static char s_TypeTag;
            return reinterpret_cast<TypeId>(&s_TypeTag);
        }

        template <class T>
        [[nodiscard]] auto GetOrCreate() -> std::unordered_map<Key, std::vector<std::remove_cvref_t<T>>>&;

        template <class T>
        [[nodiscard]] auto TryGet() const -> const std::unordered_map<Key, std::vector<std::remove_cvref_t<T>>>*;

        [[nodiscard]] bool Erase(TypeId typeId, Key id);

    private:
        struct ITypePool
        {
            virtual ~ITypePool() = default;
            virtual bool Erase(Key id) = 0;
        };

        template <class T>
        struct TypePool final : ITypePool
        {
            std::unordered_map<Key, std::vector<T>> values{};

            bool Erase(Key id) override
            {
                return values.erase(id) > 0;
            }
        };

        std::unordered_map<TypeId, std::unique_ptr<ITypePool>> m_Pools{};
    };

    template <typename Key>
    template <class T>
    auto TypePools<Key>::GetOrCreate() -> std::unordered_map<Key, std::vector<std::remove_cvref_t<T>>>&
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto typeId = Type<StoredT>();

        auto it = m_Pools.find(typeId);
        if (it == m_Pools.end())
        {
            auto pool = std::make_unique<TypePool<StoredT>>();
            auto* ptr = pool.get();
            m_Pools.emplace(typeId, std::move(pool));
            return ptr->values;
        }

        return static_cast<TypePool<StoredT>*>(it->second.get())->values;
    }

    template <typename Key>
    template <class T>
    auto TypePools<Key>::TryGet() const -> const std::unordered_map<Key, std::vector<std::remove_cvref_t<T>>>*
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto typeId = Type<StoredT>();

        const auto it = m_Pools.find(typeId);
        if (it == m_Pools.end())
        {
            return nullptr;
        }

        return &static_cast<const TypePool<StoredT>*>(it->second.get())->values;
    }

    template <typename Key>
    bool TypePools<Key>::Erase(const TypeId typeId, const Key id)
    {
        const auto it = m_Pools.find(typeId);
        if (it == m_Pools.end())
        {
            return false;
        }

        return it->second->Erase(id);
    }
}
