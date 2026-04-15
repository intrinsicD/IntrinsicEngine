#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>
#include <atomic>
#include <vector>

import Extrinsic.Core.Tasks;
import Extrinsic.Core.Error;
import Extrinsic.Asset.Manager;
import Extrinsic.Asset.Handle;

using namespace Extrinsic::Core;
using namespace Extrinsic::Assets;

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
    Tasks::Scheduler::Initialize(2);
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
    EXPECT_TRUE(manager.IsValid(handle));
    EXPECT_EQ(manager.GetState(handle), LoadState::Loading);

    // Not ready yet - should return error
    auto notReady = manager.TryGet<Texture>(handle);
    EXPECT_FALSE(notReady.has_value());
    EXPECT_EQ(notReady.error(), ErrorCode::AssetNotLoaded);

    // 5. Wait for Task
    Tasks::Scheduler::WaitForAll();

    // 6. Check Final State
    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto texResult = manager.TryGet<Texture>(handle);
    ASSERT_TRUE(texResult.has_value());
    EXPECT_EQ((*texResult)->width, 1024);

    Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_LeaseBasic)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("value", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(7);
    });

    Tasks::Scheduler::WaitForAll();

    auto leaseResult = manager.AcquireLease<int>(handle);
    ASSERT_TRUE(leaseResult.has_value());

    auto lease = *leaseResult;
    ASSERT_TRUE((bool)lease);
    ASSERT_NE(lease.Get(), nullptr);
    EXPECT_EQ(*lease, 7);
    EXPECT_EQ(*lease.Get(), 7);

    Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_RespectsProcessingGate)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("test", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(1);
    });

    Tasks::Scheduler::WaitForAll();

    manager.MoveToProcessing(handle);
    EXPECT_EQ(manager.GetState(handle), LoadState::Processing);

    auto lease = manager.AcquireLease<int>(handle);
    EXPECT_FALSE(lease.has_value());
    EXPECT_EQ(lease.error(), ErrorCode::AssetNotLoaded);

    Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_TypeMismatch)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("number", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(123);
    });

    Tasks::Scheduler::WaitForAll();

    auto mismatch = manager.AcquireLease<float>(handle);
    EXPECT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error(), ErrorCode::AssetTypeMismatch);

    Tasks::Scheduler::Shutdown();
}

struct Reloadable
{
    int Value = 0;
};

TEST(AssetSystem, LeaseSurvivesReload_NewLeaseSeesNewValue)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    int generation = 1;

    auto loader = [&](const std::string&, AssetHandle) -> std::shared_ptr<Reloadable>
    {
        auto r = std::make_shared<Reloadable>();
        r->Value = generation;
        return r;
    };

    auto handle = manager.Load<Reloadable>("reloadable", loader);
    Tasks::Scheduler::WaitForAll();

    // Pin old value.
    auto lease1Res = manager.AcquireLease<Reloadable>(handle);
    ASSERT_TRUE(lease1Res.has_value());
    auto lease1 = *lease1Res;
    ASSERT_TRUE((bool)lease1);
    EXPECT_EQ(lease1->Value, 1);

    // Trigger reload to new value.
    generation = 2;
    manager.ReloadAsset<Reloadable>(handle);
    Tasks::Scheduler::WaitForAll();

    // Old lease must still see old data.
    EXPECT_EQ(lease1->Value, 1);

    // New lease must see new data.
    auto lease2Res = manager.AcquireLease<Reloadable>(handle);
    ASSERT_TRUE(lease2Res.has_value());
    EXPECT_EQ((*lease2Res)->Value, 2);

    Tasks::Scheduler::Shutdown();
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
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto loader = [](const std::string&, AssetHandle) -> std::unique_ptr<NonCopyable>
    {
        return std::make_unique<NonCopyable>(42);
    };

    auto handle = manager.Load<NonCopyable>("noncopy", loader);
    Tasks::Scheduler::WaitForAll();

    auto raw = manager.TryGet<NonCopyable>(handle);
    ASSERT_TRUE(raw.has_value());
    ASSERT_NE(*raw, nullptr);
    EXPECT_EQ((*raw)->Value, 42);

    auto lease = manager.AcquireLease<NonCopyable>(handle);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ((*lease)->Value, 42);

    Tasks::Scheduler::Shutdown();
}

// Contract-ish test: AssetManager must be able to own non-copyable payloads like Graphics::Material
// via the Create(name, unique_ptr<T>) overload.
TEST(CoreAssets, Create_UniquePtrMaterialCompiles)
{
    //Create a noncopyable payload
    struct Material
    {
        bool test;
    };
    auto material = std::make_unique<Material>(true);
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    manager.Create("test", std::move(material));
    Tasks::Scheduler::WaitForAll();

    //TODO: Check this test?
}

TEST(AssetSystem, Caching)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    // FIX: Added AssetHandle argument
    auto simpleLoader = [](const std::string&, AssetHandle) { return std::make_shared<Mesh>(100); };

    // Load same path twice
    AssetHandle h1 = manager.Load<Mesh>("mesh.obj", simpleLoader);
    AssetHandle h2 = manager.Load<Mesh>("mesh.obj", simpleLoader);

    // Should return exact same handle ID (interning)
    EXPECT_EQ(h1, h2);

    Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, EventCallbackOnMainThread)
{
    Tasks::Scheduler::Initialize(2);
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
    manager.ListenOnce(handle, [&](AssetHandle) {
        callbackFired = true;
        callbackThreadId = std::this_thread::get_id();
    });

    // Wait for loader to finish background work
    Tasks::Scheduler::WaitForAll();

    // At this point, asset is Ready in background, but callback hasn't fired yet
    EXPECT_FALSE(callbackFired);

    // Run Update on "Main Thread"
    manager.Update();

    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(callbackThreadId, std::this_thread::get_id()); // Must run on THIS thread

    Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, ExternalFinalization) {
    Tasks::Scheduler::Initialize(1); // 1. Start Scheduler
    AssetManager manager;

    // Loader creates payload
    auto handle = manager.Load<int>("test", [](const std::string&, AssetHandle){
        return std::make_shared<int>(1);
    });

    // 2. Wait for background task to populate payload (State becomes Ready initially)
    Tasks::Scheduler::WaitForAll();

    // 3. Simulate transition to "Processing" (e.g. waiting for GPU)
    manager.MoveToProcessing(handle);
    EXPECT_EQ(manager.GetState(handle), LoadState::Processing);

    // 4. Verify Get returns error while processing (Access Control)
    auto processingResult = manager.TryGet<int>(handle);
    EXPECT_FALSE(processingResult.has_value());
    EXPECT_EQ(processingResult.error(), ErrorCode::AssetNotLoaded);

    // 5. Finalize
    manager.FinalizeLoad(handle);

    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto finalResult = manager.TryGet<int>(handle);
    EXPECT_TRUE(finalResult.has_value());

    Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, TryGetFast_HotPathOptimization)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("number", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(123);
    });

    Tasks::Scheduler::WaitForAll();

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

    Tasks::Scheduler::Shutdown();
}

// ---------------------------------------------------------------------------
// Tests for quick-win fixes
// ---------------------------------------------------------------------------

// Phase 6: Verify AssetLease PinCount is correct under concurrent copy/destroy.
TEST(AssetSystem, Lease_ConcurrentPinUnpin)
{
    Tasks::Scheduler::Initialize(4);
    AssetManager manager;

    auto handle = manager.Load<int>("shared_val", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(999);
    });

    Tasks::Scheduler::WaitForAll();

    // Acquire a base lease
    auto baseResult = manager.AcquireLease<int>(handle);
    ASSERT_TRUE(baseResult.has_value());
    auto baseLease = *baseResult;

    // Spawn many threads that copy and destroy leases concurrently
    constexpr int kThreads = 8;
    constexpr int kIterations = 1000;
    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&baseLease, &successCount]()
        {
            for (int i = 0; i < kIterations; ++i)
            {
                // Copy (pins)
                AssetLease<int> copy = baseLease;
                if (copy.Get() && *copy == 999)
                    successCount.fetch_add(1, std::memory_order_relaxed);
                // Destructor (unpins)
            }
        });
    }

    for (auto& th : threads)
        th.join();

    // All iterations should have seen the value (no torn reads)
    EXPECT_EQ(successCount.load(), kThreads * kIterations);

    // Base lease should still be valid
    ASSERT_NE(baseLease.Get(), nullptr);
    EXPECT_EQ(*baseLease, 999);

    Tasks::Scheduler::Shutdown();
}

// Phase 7: Verify AssetManager::Clear() doesn't crash and properly resets state.
TEST(AssetSystem, Clear_SafeWithMultipleAssets)
{
    Tasks::Scheduler::Initialize(2);
    AssetManager manager;

    // Load several assets
    auto loader = [](const std::string& path, AssetHandle) -> std::shared_ptr<int>
    {
        (void)path;
        return std::make_shared<int>(42);
    };

    [[maybe_unused]] auto h1 = manager.Load<int>("asset_a", loader);
    [[maybe_unused]] auto h2 = manager.Load<int>("asset_b", loader);
    [[maybe_unused]] auto h3 = manager.Load<int>("asset_c", loader);

    Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(manager.GetState(h1), LoadState::Ready);
    EXPECT_EQ(manager.GetState(h2), LoadState::Ready);
    EXPECT_EQ(manager.GetState(h3), LoadState::Ready);

    // Clear should not crash even with multiple loaded assets
    manager.Clear();

    // All handles should now be invalid/unloaded
    EXPECT_EQ(manager.GetState(h1), LoadState::Unloaded);
    EXPECT_EQ(manager.GetState(h2), LoadState::Unloaded);
    EXPECT_EQ(manager.GetState(h3), LoadState::Unloaded);

    Tasks::Scheduler::Shutdown();
}

// Phase 7: Verify Clear() is safe when leases are still held.
TEST(AssetSystem, Clear_WhileLeaseHeld)
{
    Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("leased", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(77);
    });

    Tasks::Scheduler::WaitForAll();

    // Acquire lease before clearing
    auto leaseResult = manager.AcquireLease<int>(handle);
    ASSERT_TRUE(leaseResult.has_value());
    auto lease = *leaseResult;
    EXPECT_EQ(*lease, 77);

    // Clear the manager — should not crash even though lease is held
    manager.Clear();

    // Lease should still hold the old value (backed by shared_ptr to slot)
    ASSERT_NE(lease.Get(), nullptr);
    EXPECT_EQ(*lease, 77);

    // Handle is now invalid
    EXPECT_EQ(manager.GetState(handle), LoadState::Unloaded);

    Tasks::Scheduler::Shutdown();
}
