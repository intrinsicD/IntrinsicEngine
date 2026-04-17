#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Filesystem;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Tasks;

using Extrinsic::Core::Filesystem::FileWatcher;
using Extrinsic::Core::Filesystem::GetAssetPath;
using Extrinsic::Core::Filesystem::GetShaderPath;
using Extrinsic::Core::Filesystem::GetAbsolutePath;
using Extrinsic::Core::Filesystem::ResolveShaderPathOrExit;
using Extrinsic::Core::Hash::StringID;
using Extrinsic::Core::Tasks::Scheduler;

namespace
{
    // A dedicated temp dir per test run; cleaned up in the fixture teardown.
    class FilesystemTempDir
    {
    public:
        FilesystemTempDir()
        {
            const auto base = std::filesystem::temp_directory_path();
            for (int i = 0; i < 1024; ++i)
            {
                auto candidate = base / ("extrinsic_fs_test_" + std::to_string(::getpid()) + "_" + std::to_string(i));
                std::error_code ec;
                if (std::filesystem::create_directory(candidate, ec))
                {
                    m_Path = std::move(candidate);
                    return;
                }
            }
            // Leave m_Path empty if all attempts failed; callers will fail their
            // own write assertions. This lets the constructor remain noexcept-y
            // and avoids GoogleTest fatal-failure-in-constructor issues.
        }

        ~FilesystemTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_Path, ec);
        }

        [[nodiscard]] const std::filesystem::path& Path() const { return m_Path; }

    private:
        std::filesystem::path m_Path;
    };

    void WriteFile(const std::filesystem::path& p, std::string_view contents)
    {
        std::ofstream ofs(p, std::ios::binary);
        ASSERT_TRUE(ofs.is_open());
        ofs.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    void AppendFile(const std::filesystem::path& p, std::string_view contents)
    {
        std::ofstream ofs(p, std::ios::binary | std::ios::app);
        ASSERT_TRUE(ofs.is_open());
        ofs.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
}

// -----------------------------------------------------------------------------
// PathResolver: Asset / Shader / Absolute helpers
// -----------------------------------------------------------------------------

TEST(CoreFilesystem, GetAssetPathEndsWithInput)
{
    const auto p = GetAssetPath("shaders/foo.spv");
    EXPECT_TRUE(std::filesystem::path(p).filename() == "foo.spv")
        << "GetAssetPath returned: " << p;
}

TEST(CoreFilesystem, GetAssetPathContainsAssetsSegment)
{
    const auto p = GetAssetPath("sample.txt");
    const auto asPath = std::filesystem::path(p);
    bool seen = false;
    for (const auto& seg : asPath)
    {
        if (seg == "assets")
        {
            seen = true;
            break;
        }
    }
    EXPECT_TRUE(seen) << "GetAssetPath must include an 'assets' segment: " << p;
}

TEST(CoreFilesystem, GetShaderPathReturnsSomething)
{
    // For a non-existent shader, the resolver falls back to returning the input
    // string unchanged so that callers can print a clear error.
    const auto p = GetShaderPath("nonexistent_shader.spv");
    EXPECT_FALSE(p.empty());
}

TEST(CoreFilesystem, GetShaderPathPassesThroughAbsolutePath)
{
    FilesystemTempDir dir;
    auto target = dir.Path() / "blob.spv";
    WriteFile(target, "spv");

    auto resolved = GetShaderPath(target.string());
    EXPECT_EQ(resolved, target.string());
}

TEST(CoreFilesystem, GetAbsolutePathResolvesRelativeInput)
{
    const auto cwd = std::filesystem::current_path();
    const auto abs = GetAbsolutePath(".");
    // On success the result should be the canonical CWD (or equivalent).
    std::error_code ec;
    EXPECT_TRUE(std::filesystem::equivalent(cwd, abs, ec)) << "abs: " << abs;
}

TEST(CoreFilesystem, GetAbsolutePathRoundtripsExistingFile)
{
    FilesystemTempDir dir;
    auto target = dir.Path() / "file.txt";
    WriteFile(target, "hello");

    auto abs = GetAbsolutePath(target.string());
    // Result is canonical/absolute; equivalent() compares them regardless of
    // whether one has extra `..` or `.` segments.
    std::error_code ec;
    EXPECT_TRUE(std::filesystem::equivalent(target, abs, ec));
}

TEST(CoreFilesystem, ResolveShaderPathOrExitUsesLookupForKnownId)
{
    // The lookup returns a path string; ResolveShaderPathOrExit forwards to
    // GetShaderPath. We cannot exercise the std::exit branch without killing
    // the test process, so that's intentionally not tested.
    FilesystemTempDir dir;
    auto shader = dir.Path() / "known.spv";
    WriteFile(shader, "spv");

    const auto id = StringID("known.spv");
    auto result = ResolveShaderPathOrExit(
        [&](StringID qid) -> std::optional<std::string>
        {
            if (qid == id) return shader.string();  // absolute path
            return std::nullopt;
        },
        id);
    EXPECT_EQ(result, shader.string());
}

// -----------------------------------------------------------------------------
// FileWatcher: Watch on a missing file is a silent no-op.
// -----------------------------------------------------------------------------

TEST(CoreFilesystem, FileWatcherWatchMissingFileIsSilentNoop)
{
    FilesystemTempDir dir;
    FileWatcher::Initialize();

    // Register a watch on a file that does not exist. This must not throw and
    // must not dispatch any callback.
    std::atomic<int> callbacks{0};
    FileWatcher::Watch((dir.Path() / "does_not_exist").string(),
                       [&](const std::string&) { callbacks.fetch_add(1); });

    // Briefly wait to confirm no callback fires.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    FileWatcher::Shutdown();

    EXPECT_EQ(callbacks.load(), 0);
}

// -----------------------------------------------------------------------------
// FileWatcher: Detects modification and dispatches callback via Scheduler.
// -----------------------------------------------------------------------------

TEST(CoreFilesystem, FileWatcherDetectsModificationAndDispatches)
{
    FilesystemTempDir dir;
    auto target = dir.Path() / "watched.txt";
    WriteFile(target, "initial");

    Scheduler::Initialize(2);
    FileWatcher::Initialize();

    std::atomic<int> callbacks{0};
    std::mutex pathMutex;
    std::string capturedPath;

    FileWatcher::Watch(target.string(), [&](const std::string& p)
    {
        {
            std::lock_guard lock(pathMutex);
            capturedPath = p;
        }
        callbacks.fetch_add(1, std::memory_order_release);
    });

    // The scanner runs on a 500 ms tick with a 150 ms debounce. Wait at least
    // one scan interval before mutating, then wait enough for the next two
    // ticks after the mutation.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Ensure the mtime advances visibly by explicitly updating it.
    std::error_code ec;
    const auto newTime = std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2);
    std::filesystem::last_write_time(target, newTime, ec);
    ASSERT_FALSE(ec) << "Failed to bump mtime: " << ec.message();
    AppendFile(target, " update");

    // Poll up to 5 seconds for the callback to fire.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (callbacks.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Allow any in-flight scheduled task to actually run.
    Scheduler::WaitForAll();

    // Shut down BEFORE we verify so nothing races with cleanup.
    FileWatcher::Shutdown();
    Scheduler::Shutdown();

    EXPECT_GE(callbacks.load(), 1);
    {
        std::lock_guard lock(pathMutex);
        EXPECT_EQ(capturedPath, target.string());
    }
}

// -----------------------------------------------------------------------------
// FileWatcher: Initialize is idempotent, Shutdown without Initialize is safe.
// -----------------------------------------------------------------------------

TEST(CoreFilesystem, FileWatcherDoubleInitializeIsSafe)
{
    FileWatcher::Initialize();
    FileWatcher::Initialize();  // Should be a no-op second call.
    FileWatcher::Shutdown();
    // Shutdown when not running must also be safe.
    FileWatcher::Shutdown();
    SUCCEED();
}
