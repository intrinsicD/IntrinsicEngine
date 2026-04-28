module;

#include <cstdint>
#include <functional>
#include <limits>

export module Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Core
{
    // -------------------------------------------------------------------------
    // StrongHandle - Type-safe generational handle template
    // -------------------------------------------------------------------------
    // Use this for resource references that need to be validated.
    // The Tag type parameter ensures handles of different resource types
    // cannot be accidentally mixed up at compile time.
    //
    // Example Usage:
    //   struct GeometryTag {};
    //   using GeometryHandle = Core::StrongHandle<GeometryTag>;
    //
    //   struct TextureTag {};
    //   using TextureHandle = Core::StrongHandle<TextureTag>;
    //
    //   GeometryHandle geo = storage.Add(...);
    //   TextureHandle tex = textures.Add(...);
    //   // geo = tex; // Compile error - different types!
    // -------------------------------------------------------------------------

    constexpr uint32_t INVALID_HANDLE_INDEX = std::numeric_limits<uint32_t>::max();

    template <typename Tag>
    struct StrongHandle
    {
        uint32_t Index = INVALID_HANDLE_INDEX;
        uint32_t Generation = 0;

        constexpr StrongHandle() noexcept = default;

        constexpr StrongHandle(uint32_t index, uint32_t generation) noexcept : Index(index), Generation(generation)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Index != INVALID_HANDLE_INDEX;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }

        auto operator<=>(const StrongHandle&) const = default;
    };

    // Exported hasher for use in unordered containers across module boundaries.
    // std::hash specializations in module purviews are not reliably visible to
    // consumers that instantiate std::unordered_map in their own GMF. This
    // explicit functor sidesteps the issue entirely.
    template <typename Tag>
    struct StrongHandleHash
    {
        std::size_t operator()(StrongHandle<Tag> const& h) const noexcept
        {
            uint64_t val = (static_cast<uint64_t>(h.Generation) << 32) | h.Index;
            val ^= val >> 33;
            val *= 0xff51afd7ed558ccd;
            val ^= val >> 33;
            val *= 0xc4ceb9fe1a85ec53;
            val ^= val >> 33;

            return static_cast<std::size_t>(val);
        }
    };
}
