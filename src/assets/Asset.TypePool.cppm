// Provides process-local type tokens for type-erased asset payload checks;
// token values are runtime identities and are never serialization keys.
module;

#include <cstdint>
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
            return reinterpret_cast<TypeId>(&s_TypeTag<T>);
        }

        // Normalized, folded token used by 32-bit asset metadata.
        template <class T>
        [[nodiscard]] static std::uint32_t Type32() noexcept
        {
            const auto value = Type<std::remove_cvref_t<T>>();
            if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
            {
                return static_cast<std::uint32_t>(value ^ (value >> 32));
            }
            return static_cast<std::uint32_t>(value);
        }

    private:
        template <class T>
        static inline char s_TypeTag{};
    };
}
