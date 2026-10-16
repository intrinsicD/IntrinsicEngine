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

export namespace Extrinsic::Core::Base64
{
    [[nodiscard]] std::string Encode(std::span<const std::uint8_t> bytes);

    // Byte count `text` decodes to, from its length and padding alone; nullopt
    // when the length or padding shape is invalid. Lets callers bound a payload
    // against an expected size before decoding or allocating.
    [[nodiscard]] std::optional<std::size_t> DecodedSize(std::string_view text) noexcept;

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> Decode(std::string_view text);
}
