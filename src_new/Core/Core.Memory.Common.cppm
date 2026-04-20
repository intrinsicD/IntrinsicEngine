module;

#include <concepts>
#include <cstddef>
#include <expected>
#include <span>

export module Extrinsic.Core.Memory:Common;

export namespace Extrinsic::Core::Memory
{
    export constexpr size_t kDefaultAlignment = 16;
    export constexpr size_t kCacheLineSize = 64;

    export enum class AllocError
    {
        OutOfMemory,
        InvalidAlignment,
        Overflow,
        ThreadViolation,
        InvalidMarker
    };

    export template <typename T>
    concept ArenaLike = requires(T a, const size_t size, const size_t align)
    {
        { a.AllocBytes(size, align) } -> std::convertible_to<std::expected<std::span<std::byte>, AllocError>>;
        { a.Reset() } -> std::same_as<void>;
    };
}
