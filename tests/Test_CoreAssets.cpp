#include <gtest/gtest.h>
#include <thread>
#include <chrono>

import Core.Assets;
import Core.Tasks;

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
    auto textureLoader = [](const std::string& path) -> std::shared_ptr<Texture>
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
    EXPECT_EQ(manager.Get<Texture>(handle), nullptr); // Not ready yet

    // 5. Wait for Task
    Core::Tasks::Scheduler::WaitForAll();

    // 6. Check Final State
    EXPECT_EQ(manager.GetState(handle), LoadState::Ready);
    auto tex = manager.Get<Texture>(handle);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->width, 1024);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Caching)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto simpleLoader = [](const std::string&) { return std::make_shared<Mesh>(100); };

    // Load same path twice
    AssetHandle h1 = manager.Load<Mesh>("mesh.obj", simpleLoader);
    AssetHandle h2 = manager.Load<Mesh>("mesh.obj", simpleLoader);

    // Should return exact same handle ID (interning)
    EXPECT_EQ(h1, h2);

    Core::Tasks::Scheduler::Shutdown();
}

// Test_CoreAssets.cpp (Update)
TEST(AssetSystem, EventCallbackOnMainThread)
{
    Core::Tasks::Scheduler::Initialize(2);
    AssetManager manager;

    bool callbackFired = false;
    std::thread::id callbackThreadId;

    // Loader runs on background thread
    auto slowLoader = [](const std::string&) {
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
