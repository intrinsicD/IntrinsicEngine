// RFC 4648 base64 (standard alphabet, '=' padding) for byte payloads embedded
// in text documents. Decoding is strict: no whitespace, no URL-safe alphabet,
// padding only at the end, non-zero trailing bits rejected, so every byte
// sequence has exactly one accepted encoding.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Core.Base64;

namespace Extrinsic::Core::Base64
{
    namespace Detail
    {
        inline constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        [[nodiscard]] constexpr int DecodeSymbol(const char c) noexcept
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        }
    }

    export [[nodiscard]] inline std::string Encode(const std::span<const std::uint8_t> bytes)
    {
        std::string out;
        out.reserve((bytes.size() + 2u) / 3u * 4u);
        std::size_t i = 0u;
        for (; i + 3u <= bytes.size(); i += 3u)
        {
            const std::uint32_t v = (std::uint32_t(bytes[i]) << 16) |
                                    (std::uint32_t(bytes[i + 1u]) << 8) | bytes[i + 2u];
            out += Detail::kAlphabet[(v >> 18) & 63u];
            out += Detail::kAlphabet[(v >> 12) & 63u];
            out += Detail::kAlphabet[(v >> 6) & 63u];
            out += Detail::kAlphabet[v & 63u];
        }
        if (const std::size_t rest = bytes.size() - i; rest > 0u)
        {
            std::uint32_t v = std::uint32_t(bytes[i]) << 16;
            if (rest == 2u)
                v |= std::uint32_t(bytes[i + 1u]) << 8;
            out += Detail::kAlphabet[(v >> 18) & 63u];
            out += Detail::kAlphabet[(v >> 12) & 63u];
            out += rest == 2u ? Detail::kAlphabet[(v >> 6) & 63u] : '=';
            out += '=';
        }
        return out;
    }

    // Byte count `text` decodes to, from its length and padding alone; nullopt
    // when the length or padding shape is invalid. Lets callers bound a payload
    // against an expected size before decoding or allocating.
    export [[nodiscard]] inline std::optional<std::size_t> DecodedSize(const std::string_view text) noexcept
    {
        if (text.size() % 4u != 0u)
            return std::nullopt;
        if (text.empty())
            return 0u;
        std::size_t padding = 0u;
        if (text.back() == '=')
            padding = text[text.size() - 2u] == '=' ? 2u : 1u;
        return text.size() / 4u * 3u - padding;
    }

    export [[nodiscard]] inline std::optional<std::vector<std::uint8_t>> Decode(const std::string_view text)
    {
        const std::optional<std::size_t> size = DecodedSize(text);
        if (!size.has_value())
            return std::nullopt;
        std::vector<std::uint8_t> out;
        out.reserve(*size);
        for (std::size_t i = 0u; i < text.size(); i += 4u)
        {
            const bool last = i + 4u == text.size();
            const bool pad2 = last && text[i + 2u] == '=';
            const bool pad3 = last && text[i + 3u] == '=';
            if (pad2 && !pad3)
                return std::nullopt;
            const int a = Detail::DecodeSymbol(text[i]);
            const int b = Detail::DecodeSymbol(text[i + 1u]);
            const int c = pad2 ? 0 : Detail::DecodeSymbol(text[i + 2u]);
            const int d = pad3 ? 0 : Detail::DecodeSymbol(text[i + 3u]);
            if (a < 0 || b < 0 || c < 0 || d < 0)
                return std::nullopt;
            const std::uint32_t v = (std::uint32_t(a) << 18) | (std::uint32_t(b) << 12) |
                                    (std::uint32_t(c) << 6) | std::uint32_t(d);
            out.push_back(static_cast<std::uint8_t>(v >> 16));
            if (!pad2)
                out.push_back(static_cast<std::uint8_t>(v >> 8));
            else if ((v & 0xFFFFu) != 0u)
                return std::nullopt;
            if (!pad3)
                out.push_back(static_cast<std::uint8_t>(v));
            else if (!pad2 && (v & 0xFFu) != 0u)
                return std::nullopt;
        }
        return out;
    }
}
