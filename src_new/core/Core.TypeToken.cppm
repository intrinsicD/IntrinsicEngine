module;

export module Core.TypeToken;

#include <string_view>
#include <cstdint>
#include <limits>

namespace Extrinsic::Core
{
    namespace Detail
    {
        [[nodiscard]] constexpr uint64_t HashTypeSig(std::string_view s) noexcept
        {
            uint64_t h = 14695981039346656037ULL;
            for (unsigned char c : s)
            {
                h ^= c;
                h *= 1099511628211ULL;
            }
            return h;
        }
    }

    export template <typename T>
    [[nodiscard]] consteval std::string_view TypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "TypeNameOf<unknown>";
#endif
    }

    template <typename T>
    [[nodiscard]] std::size_t TypeToken() noexcept
    {
        constexpr auto kMask = std::numeric_limits<std::size_t>::max() >> 1;
        static constexpr std::size_t s_Token = static_cast<std::size_t>(
            Detail::HashTypeSig(TypeNameOf<T>())) & kMask;
        return s_Token;
    }
}
