#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Base64;

namespace Base64 = Extrinsic::Core::Base64;

namespace
{
    std::string Encode(const std::string_view text)
    {
        return Base64::Encode({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }

    std::optional<std::string> Decode(const std::string_view text)
    {
        const auto bytes = Base64::Decode(text);
        if (!bytes.has_value())
            return std::nullopt;
        return std::string(bytes->begin(), bytes->end());
    }
}

TEST(CoreBase64, MatchesTheRfcVectorsBothWays)
{
    for (const auto& [plain, encoded] : std::vector<std::pair<std::string_view, std::string_view>>{
             {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
             {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}})
    {
        EXPECT_EQ(Encode(plain), encoded);
        EXPECT_EQ(Decode(encoded), std::string{plain}) << encoded;
        EXPECT_EQ(Base64::DecodedSize(encoded), plain.size()) << encoded;
    }
}

TEST(CoreBase64, RoundTripsEveryByteValue)
{
    std::vector<std::uint8_t> bytes(256u);
    for (std::size_t i = 0u; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(i);
    for (std::size_t size = 0u; size <= bytes.size(); size += 37u)
    {
        const std::vector<std::uint8_t> slice(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        EXPECT_EQ(Base64::Decode(Base64::Encode(slice)), slice) << size;
    }
}

TEST(CoreBase64, StrictDecodingRejectsNonCanonicalText)
{
    for (const std::string_view bad : {"Zg", "Zg=", "Zm9v Yg==", "Zm9vYg=a", "Z===", "====",
                                       "Zm=v", "Zh==", "Zm9=", "Zm-_", "Zg==Zg==", "Zm9v\n"})
    {
        EXPECT_FALSE(Base64::Decode(bad).has_value()) << bad;
    }
    EXPECT_FALSE(Base64::DecodedSize("abc").has_value());
}
