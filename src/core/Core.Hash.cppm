// Canonical string and type hashing: 32-bit `StringID` names, the 64-bit FNV-1a
// byte hash, and the RTTI-free compile-time type identity built on it.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>

export module Extrinsic.Core.Hash;

namespace Extrinsic::Core::Hash
{
    export constexpr uint32_t HashString(std::string_view str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    // 64-bit FNV-1a over the bytes of `str`, each byte treated as unsigned and
    // embedded NULs included. Compile-time type identity and the task graph's
    // own type tokens share this one implementation; their signature sources
    // stay distinct.
    export [[nodiscard]] constexpr uint64_t HashString64(std::string_view str) noexcept
    {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : str)
        {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    export struct StringID
    {
        uint32_t Value;
#ifndef NDEBUG
        const char* DebugString = nullptr; // For debugging hash collisions
#endif

        constexpr StringID() : Value(0)
        {
        }

        constexpr StringID(uint32_t v) : Value(v)
        {
        }

        // Note: DebugString points to the literal, safe only for string literals
        constexpr StringID(const char* str) : Value(HashString(str))
#ifndef NDEBUG
                                              , DebugString(str)
#endif
        {
        }

        // WARNING: DebugString stores str.data() which is only valid if str outlives this StringID.
        // Prefer the const char* constructor with string literals for safety.
        // This constructor is mainly for compile-time string_view literals.
        constexpr StringID(std::string_view str) : Value(HashString(str))
#ifndef NDEBUG
                                                   , DebugString(str.data())
        // UNSAFE: Only valid if str outlives StringID!
#endif
        {
        }

        auto operator<=>(const StringID&) const = default;

        // Equality only compares Value, not DebugString
        bool operator==(const StringID& other) const { return Value == other.Value; }
    };

    // User-defined literal for convenient IDs, e.g. "Backbuffer"_id.
    export constexpr StringID operator""_id(const char* str, size_t len)
    {
        return {std::string_view(str, len)};
    }

    export struct U64Hash
    {
        size_t operator()(uint64_t v) const { return std::hash<uint64_t>{}(v); }
    };
}

// -----------------------------------------------------------------------
// Compile-time type ID — deterministic FNV-1a hash of the compiler type
// signature. Stable across TUs and named-module boundaries (no RTTI).
// The high bit is masked off so a token never collides with reserved
// sentinel values in erased key maps.
// -----------------------------------------------------------------------
export namespace Extrinsic::Core
{
    namespace Detail
    {
        template <typename T>
        [[nodiscard]] constexpr std::string_view TypeSig() noexcept
        {
#if defined(__clang__) || defined(__GNUC__)
            return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
            return __FUNCSIG__;
#else
            return "TypeSig<unknown>";
#endif
        }
    }

    template <typename T>
    [[nodiscard]] std::size_t TypeToken() noexcept
    {
        constexpr auto kMask = std::numeric_limits<std::size_t>::max() >> 1;
        static constexpr std::size_t s_Token = static_cast<std::size_t>(
            Hash::HashString64(Detail::TypeSig<T>())) & kMask;
        return s_Token;
    }
}

// Allow Core::Hash::StringID to be used in unordered containers.
// (We keep the type in Core::Hash, but the hash specialization must live in std.)
namespace std
{
    template <>
    struct hash<Extrinsic::Core::Hash::StringID>
    {
        size_t operator()(const Extrinsic::Core::Hash::StringID& id) const noexcept
        {
            return hash<uint32_t>{}(id.Value);
        }
    };
}