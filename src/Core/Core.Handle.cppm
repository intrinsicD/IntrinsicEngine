module;
#include <cstdint>
#include <limits>
#include <functional>

export module Core:Handle;

export namespace Core
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
    template <typename Tag>
    struct StrongHandle
    {
        static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

        uint32_t Index = INVALID_INDEX;
        uint32_t Generation = 0;

        constexpr StrongHandle() = default;

        constexpr StrongHandle(uint32_t index, uint32_t gen) : Index(index), Generation(gen)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Index != INVALID_INDEX;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }

        auto operator<=>(const StrongHandle&) const = default;
    };
}

// Allow StrongHandle to be used in unordered containers
namespace std
{
    template <typename Tag>
    struct hash<Core::StrongHandle<Tag>>
    {
        std::size_t operator()(const Core::StrongHandle<Tag>& h) const noexcept
        {
            // Pack into 64-bit integer (assuming 32-bit index/gen)
            uint64_t val = (static_cast<uint64_t>(h.Generation) << 32) | h.Index;

            // MurmurHash3 Mix / WyHash Mix (Very fast, high avalanche)
            val ^= val >> 33;
            val *= 0xff51afd7ed558ccd;
            val ^= val >> 33;
            val *= 0xc4ceb9fe1a85ec53;
            val ^= val >> 33;

            return static_cast<std::size_t>(val);
        }
    };
}
