module;

#include <concepts>
#include <cstddef>
#include <span>

export module Extrinsic.Core.Memory:Common;
import Extrinsic.Core.Error;

export namespace Extrinsic::Core::Memory
{
    export constexpr size_t kDefaultAlignment = 16;
    export constexpr size_t kCacheLineSize = 64;

    export using MemoryError = Extrinsic::Core::ErrorCode;

    template <typename T>
    using MemoryExpected = Extrinsic::Core::Expected<T>;

    export template <typename T>
    concept ArenaLike = requires(T a, const size_t size, const size_t align)
    {
        { a.AllocBytes(size, align) } -> std::convertible_to<MemoryExpected<std::span<std::byte>>>;
        { a.Reset() } -> std::same_as<void>;
    };
}
