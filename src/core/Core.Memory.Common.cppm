module;

#include <concepts>
#include <cstddef>
#include <expected>
#include <span>

export module Extrinsic.Core.Memory:Common;
import Extrinsic.Core.Error;

export namespace Extrinsic::Core::Memory
{
    constexpr size_t kDefaultAlignment = 16;
    constexpr size_t kCacheLineSize = 64;

    template <typename T>
    concept ArenaLike = requires(T a, const size_t size, const size_t align)
    {
        { a.AllocBytes(size, align) } -> std::convertible_to<std::expected<std::span<std::byte>, ErrorCode>>;
        { a.Reset() } -> std::same_as<void>;
    };
}
