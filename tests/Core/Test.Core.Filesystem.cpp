#include <gtest/gtest.h>
#include <string>
#include <optional>

import Extrinsic.Core.Filesystem;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Error;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Hash;

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

TEST(CoreFilesystemWatcher, StatsCanBeResetAndRead)
{
    Filesystem::FileWatcher::ResetStatsForTests();
    const auto stats = Filesystem::FileWatcher::GetStats();
    EXPECT_EQ(stats.DeferredEventCount, 0u);
    EXPECT_EQ(stats.DroppedEventCount, 0u);
    EXPECT_EQ(stats.InlineDispatchCount, 0u);
    EXPECT_EQ(stats.SchedulerDispatchCount, 0u);
}
