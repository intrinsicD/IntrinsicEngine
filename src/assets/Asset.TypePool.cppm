module;

#include <cstdint>

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

    private:
        template <class T>
        static inline char s_TypeTag{};
    };
}
