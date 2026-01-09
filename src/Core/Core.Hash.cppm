module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include  <string_view>

export module Core:Hash;

export namespace Core::Hash
{
    // FNV-1a Hash
    constexpr uint32_t HashString(std::string_view str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    struct StringID
    {
        uint32_t Value;

        constexpr StringID() : Value(0)
        {
        }

        constexpr StringID(uint32_t v) : Value(v)
        {
        }

        constexpr StringID(const char* str) : Value(HashString(str))
        {
        }

        constexpr StringID(std::string_view str) : Value(HashString(str))
        {
        }

        auto operator<=>(const StringID&) const = default;
    };

    // User-defined literal for convenient IDs, e.g. "Backbuffer"_id.
    constexpr StringID operator""_id(const char* str, size_t len)
    {
        return {std::string_view(str, len)};
    }

    struct U64Hash {
        size_t operator()(uint64_t v) const { return std::hash<uint64_t>{}(v); }
    };

}


// Allow Core::Hash::StringID to be used in unordered containers.
// (We keep the type in Core::Hash, but the hash specialization must live in std.)
template <>
struct std::hash<Core::Hash::StringID>
{
    std::size_t operator()(const Core::Hash::StringID& id) const noexcept
    {
        return std::hash<uint32_t>{}(id.Value);
    }
};
