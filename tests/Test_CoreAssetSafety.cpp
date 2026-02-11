#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>
#include <atomic>
#include <vector>
#include <string>

import Core;

using namespace Core;
using namespace Core::Assets;

// ---------------------------------------------------------------------------
// Issue 1.3: Asset Loader Capture Safety — Concept & Shared Ownership
// ---------------------------------------------------------------------------

// Compile-time test: AssetLoaderFunc concept rejects non-copyable loaders.
struct NonCopyableLoader
{
    NonCopyableLoader() = default;
    NonCopyableLoader(const NonCopyableLoader&) = delete;
    NonCopyableLoader(NonCopyableLoader&&) = default;
    NonCopyableLoader& operator=(const NonCopyableLoader&) = delete;
    NonCopyableLoader& operator=(NonCopyableLoader&&) = default;

    std::shared_ptr<int> operator()(const std::string&, AssetHandle) const
    {
        return std::make_shared<int>(1);
    }
};

TEST(AssetLoaderSafety, Concept_RejectsNonCopyableLoader)
{
    // A non-copyable loader must NOT satisfy AssetLoaderFunc because
    // loaders are stored persistently for hot-reload callbacks.
    static_assert(!Core::Assets::AssetLoaderFunc<NonCopyableLoader, int>,
                  "Non-copyable loaders must be rejected by the concept");
    SUCCEED();
}

TEST(AssetLoaderSafety, Concept_AcceptsValidLoader)
{
    // A normal lambda with value captures is a valid loader.
    auto validLoader = [](const std::string&, AssetHandle) -> std::shared_ptr<int>
    {
        return std::make_shared<int>(42);
    };

    static_assert(Core::Assets::AssetLoaderFunc<decltype(validLoader), int>,
                  "Valid copyable + invocable loaders must satisfy the concept");
    SUCCEED();
}

// Runtime test: Shared loader ownership works correctly across reloads.
// Verifies that the loader's captures remain alive across multiple reloads
// (shared_ptr wrapper prevents dangling).
TEST(AssetLoaderSafety, SharedLoaderOwnership_SurvivesReload)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    // The loader captures a shared_ptr (safe long-lived capture).
    auto sharedState = std::make_shared<std::atomic<int>>(0);

    auto loader = [sharedState](const std::string&, AssetHandle) -> std::shared_ptr<int>
    {
        int gen = sharedState->fetch_add(1, std::memory_order_relaxed) + 1;
        return std::make_shared<int>(gen);
    };

    auto handle = manager.Load<int>("reload_test", loader);
    Core::Tasks::Scheduler::WaitForAll();

    // First load should produce value 1
    auto r1 = manager.GetRaw<int>(handle);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(**r1, 1);

    // Trigger reload — loader must still be alive and functional
    manager.ReloadAsset<int>(handle);
    Core::Tasks::Scheduler::WaitForAll();

    auto r2 = manager.GetRaw<int>(handle);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(**r2, 2);

    // Third reload for good measure
    manager.ReloadAsset<int>(handle);
    Core::Tasks::Scheduler::WaitForAll();

    auto r3 = manager.GetRaw<int>(handle);
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(**r3, 3);

    // sharedState should have been incremented 3 times
    EXPECT_EQ(sharedState->load(), 3);

    Core::Tasks::Scheduler::Shutdown();
}

// ---------------------------------------------------------------------------
// Issue 1.2: Negative error-handling tests for asset error paths
// ---------------------------------------------------------------------------

// Verify GetRaw on a completely invalid (default) handle returns ResourceNotFound.
TEST(AssetErrorPaths, GetRaw_InvalidHandle_ReturnsResourceNotFound)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    AssetHandle invalid; // default-constructed, ID == entt::null
    auto result = manager.GetRaw<int>(invalid);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::ResourceNotFound);

    Core::Tasks::Scheduler::Shutdown();
}

// Verify AcquireLease on an invalid handle returns ResourceNotFound.
TEST(AssetErrorPaths, AcquireLease_InvalidHandle_ReturnsResourceNotFound)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    AssetHandle invalid;
    auto result = manager.AcquireLease<int>(invalid);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::ResourceNotFound);

    Core::Tasks::Scheduler::Shutdown();
}

// Verify GetRaw returns AssetLoadFailed when the loader returns nullptr.
TEST(AssetErrorPaths, GetRaw_FailedLoad_ReturnsAssetLoadFailed)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto failLoader = [](const std::string&, AssetHandle) -> std::shared_ptr<int>
    {
        return nullptr; // Simulate load failure
    };

    auto handle = manager.Load<int>("fail.dat", failLoader);
    Core::Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(manager.GetState(handle), LoadState::Failed);

    auto result = manager.GetRaw<int>(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::AssetLoadFailed);

    Core::Tasks::Scheduler::Shutdown();
}

// Verify AcquireLease returns AssetLoadFailed when the asset failed to load.
TEST(AssetErrorPaths, AcquireLease_FailedLoad_ReturnsAssetLoadFailed)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto failLoader = [](const std::string&, AssetHandle) -> std::shared_ptr<int>
    {
        return nullptr;
    };

    auto handle = manager.Load<int>("fail2.dat", failLoader);
    Core::Tasks::Scheduler::WaitForAll();

    auto result = manager.AcquireLease<int>(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::AssetLoadFailed);

    Core::Tasks::Scheduler::Shutdown();
}

// Verify Create with nullptr returns a handle in Failed state.
TEST(AssetErrorPaths, Create_NullUnique_ResultsInFailedState)
{
    AssetManager manager;

    auto handle = manager.Create<int>("null_asset", std::unique_ptr<int>(nullptr));
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(manager.GetState(handle), LoadState::Failed);

    auto result = manager.GetRaw<int>(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::AssetLoadFailed);
}

// Verify Create with nullptr shared_ptr returns a handle in Failed state.
TEST(AssetErrorPaths, Create_NullShared_ResultsInFailedState)
{
    AssetManager manager;

    auto handle = manager.Create<int>("null_shared", std::shared_ptr<int>(nullptr));
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(manager.GetState(handle), LoadState::Failed);

    auto result = manager.GetRaw<int>(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::AssetLoadFailed);
}
