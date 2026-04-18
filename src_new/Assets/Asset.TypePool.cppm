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
            static char s_TypeTag;
            return reinterpret_cast<TypeId>(&s_TypeTag);
        }
    };
}
