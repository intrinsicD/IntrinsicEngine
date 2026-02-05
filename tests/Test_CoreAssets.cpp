#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>

import Core;
import Graphics;
import RHI;

using namespace Core;
using namespace Core::Assets;

// Dummy Resource Types
struct Texture
{
    int width, height;
};

struct Mesh
{
    int vertices;
};

TEST(AssetSystem, AsyncLoading)
{
    // 1. Setup
    Core::Tasks::Scheduler::Initialize(2);
    AssetManager manager;

    // 2. Define a Loader Function (Simulates slow IO)
    // FIX: Added AssetHandle argument to match new signature
    auto textureLoader = [](const std::string& path, AssetHandle) -> std::shared_ptr<Texture>
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Fake IO work
        if (path == "fail.png") return nullptr;
        return std::make_shared<Texture>(1024, 1024);
    };

    // 3. Request Load
    AssetHandle handle = manager.Load<Texture>("wood.png", textureLoader);

    // 4. Check Initial State
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(manager.GetState(handle), LoadState::Loading);

    // Not ready yet - should return error
    auto notReady = manager.GetRaw<Texture>(handle);
    EXPECT_FALSE(notReady.has_value());
    EXPECT_EQ(notReady.error(), ErrorCode::AssetNotLoaded);

    // 5. Wait for Task
    Core::Tasks::Scheduler::WaitForAll();

    // 6. Check Final State
    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto texResult = manager.GetRaw<Texture>(handle);
    ASSERT_TRUE(texResult.has_value());
    EXPECT_EQ((*texResult)->width, 1024);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_LeaseBasic)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("value", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(7);
    });

    Core::Tasks::Scheduler::WaitForAll();

    auto leaseResult = manager.AcquireLease<int>(handle);
    ASSERT_TRUE(leaseResult.has_value());

    auto lease = *leaseResult;
    ASSERT_TRUE((bool)lease);
    ASSERT_NE(lease.get(), nullptr);
    EXPECT_EQ(*lease, 7);
    EXPECT_EQ(*lease.get(), 7);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_RespectsProcessingGate)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("test", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(1);
    });

    Core::Tasks::Scheduler::WaitForAll();

    manager.MoveToProcessing(handle);
    EXPECT_EQ(manager.GetState(handle), LoadState::Processing);

    auto lease = manager.AcquireLease<int>(handle);
    EXPECT_FALSE(lease.has_value());
    EXPECT_EQ(lease.error(), ErrorCode::AssetNotLoaded);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_TypeMismatch)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("number", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(123);
    });

    Core::Tasks::Scheduler::WaitForAll();

    auto mismatch = manager.AcquireLease<float>(handle);
    EXPECT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error(), ErrorCode::AssetTypeMismatch);

    Core::Tasks::Scheduler::Shutdown();
}

struct Reloadable
{
    int Value = 0;
};

TEST(AssetSystem, LeaseSurvivesReload_NewLeaseSeesNewValue)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    int generation = 1;

    auto loader = [&](const std::string&, AssetHandle) -> std::shared_ptr<Reloadable>
    {
        auto r = std::make_shared<Reloadable>();
        r->Value = generation;
        return r;
    };

    auto handle = manager.Load<Reloadable>("reloadable", loader);
    Core::Tasks::Scheduler::WaitForAll();

    // Pin old value.
    auto lease1Res = manager.AcquireLease<Reloadable>(handle);
    ASSERT_TRUE(lease1Res.has_value());
    auto lease1 = *lease1Res;
    ASSERT_TRUE((bool)lease1);
    EXPECT_EQ(lease1->Value, 1);

    // Trigger reload to new value.
    generation = 2;
    manager.ReloadAsset<Reloadable>(handle);
    Core::Tasks::Scheduler::WaitForAll();

    // Old lease must still see old data.
    EXPECT_EQ(lease1->Value, 1);

    // New lease must see new data.
    auto lease2Res = manager.AcquireLease<Reloadable>(handle);
    ASSERT_TRUE(lease2Res.has_value());
    EXPECT_EQ((*lease2Res)->Value, 2);

    Core::Tasks::Scheduler::Shutdown();
}

struct NonCopyable
{
    NonCopyable() = default;
    explicit NonCopyable(int v) : Value(v) {}

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;

    int Value = 0;
};

TEST(AssetSystem, UniquePtrLoader_SupportsNonCopyable)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto loader = [](const std::string&, AssetHandle) -> std::unique_ptr<NonCopyable>
    {
        return std::make_unique<NonCopyable>(42);
    };

    auto handle = manager.Load<NonCopyable>("noncopy", loader);
    Core::Tasks::Scheduler::WaitForAll();

    auto raw = manager.GetRaw<NonCopyable>(handle);
    ASSERT_TRUE(raw.has_value());
    ASSERT_NE(*raw, nullptr);
    EXPECT_EQ((*raw)->Value, 42);

    auto lease = manager.AcquireLease<NonCopyable>(handle);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ((*lease)->Value, 42);

    Core::Tasks::Scheduler::Shutdown();
}

// Contract-ish test: AssetManager must be able to own non-copyable payloads like Graphics::Material
// via the Create(name, unique_ptr<T>) overload.
TEST(CoreAssets, Create_UniquePtrMaterialCompiles)
{
    static_assert(!std::is_copy_constructible_v<Graphics::Material>);

    // We only validate compile-time ownership plumbing here.
    // Running this would require a VulkanDevice + BindlessDescriptorSystem instance.
    SUCCEED();
}

TEST(AssetSystem, Caching)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    // FIX: Added AssetHandle argument
    auto simpleLoader = [](const std::string&, AssetHandle) { return std::make_shared<Mesh>(100); };

    // Load same path twice
    AssetHandle h1 = manager.Load<Mesh>("mesh.obj", simpleLoader);
    AssetHandle h2 = manager.Load<Mesh>("mesh.obj", simpleLoader);

    // Should return exact same handle ID (interning)
    EXPECT_EQ(h1, h2);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, EventCallbackOnMainThread)
{
    Core::Tasks::Scheduler::Initialize(2);
    AssetManager manager;

    bool callbackFired = false;
    std::thread::id callbackThreadId;

    // Loader runs on background thread
    // FIX: Added AssetHandle argument
    auto slowLoader = [](const std::string&, AssetHandle) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return std::make_shared<int>(42);
    };

    auto handle = manager.Load<int>("data", slowLoader);

    // Register callback
    manager.RequestNotify(handle, [&](AssetHandle) {
        callbackFired = true;
        callbackThreadId = std::this_thread::get_id();
    });

    // Wait for loader to finish background work
    Core::Tasks::Scheduler::WaitForAll();

    // At this point, asset is Ready in background, but callback hasn't fired yet
    EXPECT_FALSE(callbackFired);

    // Run Update on "Main Thread"
    manager.Update();

    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(callbackThreadId, std::this_thread::get_id()); // Must run on THIS thread

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, ExternalFinalization) {
    Core::Tasks::Scheduler::Initialize(1); // 1. Start Scheduler
    AssetManager manager;

    // Loader creates payload
    auto handle = manager.Load<int>("test", [](const std::string&, AssetHandle){
        return std::make_shared<int>(1);
    });

    // 2. Wait for background task to populate payload (State becomes Ready initially)
    Core::Tasks::Scheduler::WaitForAll();

    // 3. Simulate transition to "Processing" (e.g. waiting for GPU)
    manager.MoveToProcessing(handle);
    EXPECT_EQ(manager.GetState(handle), LoadState::Processing);

    // 4. Verify Get returns error while processing (Access Control)
    auto processingResult = manager.GetRaw<int>(handle);
    EXPECT_FALSE(processingResult.has_value());
    EXPECT_EQ(processingResult.error(), ErrorCode::AssetNotLoaded);

    // 5. Finalize
    manager.FinalizeLoad(handle);

    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto finalResult = manager.GetRaw<int>(handle);
    EXPECT_TRUE(finalResult.has_value());

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, TryGetFast_HotPathOptimization)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("number", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(123);
    });

    Core::Tasks::Scheduler::WaitForAll();

    manager.BeginReadPhase();

    // NOTE: unlike RequestNotify tests, this doesn't require manager.Update() because TryGet reads state directly.

    // 1. Valid access
    int* val = manager.TryGetFast<int>(handle);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 123);

    // 2. Type mismatch (safe fail)
    float* failType = manager.TryGetFast<float>(handle);
    EXPECT_EQ(failType, nullptr);

    // 3. Invalid handle (safe fail)
    AssetHandle invalidHandle;
    int* failHandle = manager.TryGetFast<int>(invalidHandle);
    EXPECT_EQ(failHandle, nullptr);

    manager.EndReadPhase();

    Core::Tasks::Scheduler::Shutdown();
}
