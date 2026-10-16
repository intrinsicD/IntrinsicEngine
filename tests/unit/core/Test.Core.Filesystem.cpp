#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <optional>
#include <array>
#include <cstddef>

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Hash;

inline std::filesystem::path AsPath(const std::string& p) { return std::filesystem::path(p); }

TEST(CoreFileIOBackend, ReportsBufferedWriteFailure)
{
    if (!std::filesystem::exists("/dev/full"))
        GTEST_SKIP() << "/dev/full is unavailable on this host";
    IO::FileIOBackend backend;
    const std::array data{std::byte{42}};
    const auto result = backend.Write(IO::IORequest{.Path = "/dev/full"}, data);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::FileWriteError);
}

TEST(CoreFilesystemPathResolver, TryResolveShaderPathReturnsErrorWhenLookupMissing)
{
    auto r = Filesystem::TryResolveShaderPath(
        [](Hash::StringID) -> std::optional<std::string>
        {
            return std::nullopt;
        },
        "Missing.Shader"_id);

    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}

TEST(CoreFilesystemPathResolver, ShaderPathFallbackReturnsRelativeWhenMissing)
{
    const std::string rel = AsPath("no") / "such" / "shader.spv";
    const auto resolved = Filesystem::GetShaderPath(rel);
    EXPECT_EQ(resolved, rel);
}

TEST(CoreFilesystemPathResolver, AbsolutePathRoundTrip)
{
    const auto rel = AsPath("tests") / "Core" / "Test.Core.Filesystem.cpp";
    const auto abs = Filesystem::GetAbsolutePath(rel);
    const auto expected = std::filesystem::absolute(rel).string();
    EXPECT_FALSE(abs.empty());
    EXPECT_EQ(abs, expected);
    EXPECT_TRUE(std::filesystem::path(abs).is_absolute());
}

TEST(CoreFilesystemPathResolver, AssetPathContainsAssetsSegment)
{
    const std::string rel = AsPath("textures") / "checker.ktx2";
    const auto path = Filesystem::GetAssetPath(rel);
    EXPECT_NE(path.find("assets"), std::string::npos);
    EXPECT_NE(path.find("textures/checker.ktx2"), std::string::npos);
}
