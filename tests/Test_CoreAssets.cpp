#include <gtest/gtest.h>
#include <thread>
#include <chrono>

import Core;

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
    auto notReady = manager.Get<Texture>(handle);
    EXPECT_FALSE(notReady.has_value());
    EXPECT_EQ(notReady.error(), ErrorCode::AssetNotLoaded);

    // 5. Wait for Task
    Core::Tasks::Scheduler::WaitForAll();

    // 6. Check Final State
    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto texResult = manager.Get<Texture>(handle);
    ASSERT_TRUE(texResult.has_value());
    EXPECT_EQ((*texResult)->width, 1024);

    Core::Tasks::Scheduler::Shutdown();
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
    auto processingResult = manager.Get<int>(handle);
    EXPECT_FALSE(processingResult.has_value());
    EXPECT_EQ(processingResult.error(), ErrorCode::AssetNotLoaded);

    // 5. Finalize
    manager.FinalizeLoad(handle);

    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto finalResult = manager.Get<int>(handle);
    EXPECT_TRUE(finalResult.has_value());

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, TryGet_HotPathOptimization)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("number", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(123);
    });

    Core::Tasks::Scheduler::WaitForAll();

    // NOTE: unlike RequestNotify tests, this doesn't require manager.Update() because TryGet reads state directly.

    // 1. Valid access
    int* val = manager.TryGet<int>(handle);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 123);

    // 2. Type mismatch (safe fail)
    float* failType = manager.TryGet<float>(handle);
    EXPECT_EQ(failType, nullptr);

    // 3. Invalid handle (safe fail)
    AssetHandle invalidHandle;
    int* failHandle = manager.TryGet<int>(invalidHandle);
    EXPECT_EQ(failHandle, nullptr);

    Core::Tasks::Scheduler::Shutdown();
}
