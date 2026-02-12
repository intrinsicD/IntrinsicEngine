#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <atomic>
#include <thread>

#include <entt/entity/fwd.hpp>
#include "RHI.Vulkan.hpp"

import Runtime.AssetPipeline;
import RHI;
import Core;

// ---------------------------------------------------------------------------
// Compile-time API contract tests
// ---------------------------------------------------------------------------

TEST(AssetPipeline, NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::AssetPipeline>);
    static_assert(!std::is_copy_assignable_v<Runtime::AssetPipeline>);
    SUCCEED();
}

TEST(AssetPipeline, NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::AssetPipeline>);
    static_assert(!std::is_move_assignable_v<Runtime::AssetPipeline>);
    SUCCEED();
}

TEST(AssetPipeline, NotDefaultConstructible)
{
    static_assert(!std::is_default_constructible_v<Runtime::AssetPipeline>);
    SUCCEED();
}

TEST(AssetPipeline, RequiresTransferManager)
{
    static_assert(std::is_constructible_v<Runtime::AssetPipeline,
                                          RHI::TransferManager&>);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Headless integration tests (real Vulkan, no window surface)
// ---------------------------------------------------------------------------

class AssetPipelineHeadlessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RHI::ContextConfig ctxConfig{
            .AppName = "AssetPipelineTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
        m_TransferManager = std::make_unique<RHI::TransferManager>(*m_Device);
        m_Pipeline = std::make_unique<Runtime::AssetPipeline>(*m_TransferManager);
    }

    void TearDown() override
    {
        m_Pipeline.reset();
        m_TransferManager.reset();
        if (m_Device)
            m_Device->FlushAllDeletionQueues();
        m_Device.reset();
        m_Context.reset();
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::TransferManager> m_TransferManager;
    std::unique_ptr<Runtime::AssetPipeline> m_Pipeline;
};

TEST_F(AssetPipelineHeadlessTest, AssetManagerAccessible)
{
    // AssetPipeline exposes a functional AssetManager.
    auto& mgr = m_Pipeline->GetAssetManager();
    auto handle = mgr.Create("test-asset", std::make_unique<int>(42));
    ASSERT_TRUE(handle.IsValid());

    auto* val = mgr.TryGet<int>(handle);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);
}

TEST_F(AssetPipelineHeadlessTest, ProcessMainThreadQueueExecutesTasks)
{
    int counter = 0;

    m_Pipeline->RunOnMainThread([&counter]() { counter += 1; });
    m_Pipeline->RunOnMainThread([&counter]() { counter += 10; });
    m_Pipeline->RunOnMainThread([&counter]() { counter += 100; });

    // Tasks should not execute until ProcessMainThreadQueue is called.
    EXPECT_EQ(counter, 0);

    m_Pipeline->ProcessMainThreadQueue();
    EXPECT_EQ(counter, 111);

    // Calling again should be a no-op (queue was drained).
    m_Pipeline->ProcessMainThreadQueue();
    EXPECT_EQ(counter, 111);
}

TEST_F(AssetPipelineHeadlessTest, RunOnMainThreadIsThreadSafe)
{
    std::atomic<int> counter{0};
    constexpr int kTasksPerThread = 100;
    constexpr int kThreadCount = 4;

    // Spawn multiple threads that all queue tasks concurrently.
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadCount; ++t)
    {
        threads.emplace_back([this, &counter]()
        {
            for (int i = 0; i < kTasksPerThread; ++i)
            {
                m_Pipeline->RunOnMainThread([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }
    for (auto& th : threads) th.join();

    // Process all queued tasks on "main thread".
    m_Pipeline->ProcessMainThreadQueue();
    EXPECT_EQ(counter.load(), kTasksPerThread * kThreadCount);
}

TEST_F(AssetPipelineHeadlessTest, TrackMaterialAddsToList)
{
    EXPECT_TRUE(m_Pipeline->GetLoadedMaterials().empty());

    Core::Assets::AssetHandle h1{};
    h1.ID = static_cast<entt::entity>(1);
    Core::Assets::AssetHandle h2{};
    h2.ID = static_cast<entt::entity>(2);

    m_Pipeline->TrackMaterial(h1);
    m_Pipeline->TrackMaterial(h2);

    EXPECT_EQ(m_Pipeline->GetLoadedMaterials().size(), 2u);
    EXPECT_EQ(m_Pipeline->GetLoadedMaterials()[0].ID, h1.ID);
    EXPECT_EQ(m_Pipeline->GetLoadedMaterials()[1].ID, h2.ID);

    m_Pipeline->ClearLoadedMaterials();
    EXPECT_TRUE(m_Pipeline->GetLoadedMaterials().empty());
}

TEST_F(AssetPipelineHeadlessTest, RegisterAssetLoadAndProcessUploads)
{
    // Create a GPU buffer to transfer into.
    constexpr size_t bufSize = 256;
    auto dst = std::make_unique<RHI::VulkanBuffer>(
        *m_Device, bufSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    // Initiate a transfer.
    std::vector<std::byte> payload(bufSize, std::byte{0xCD});
    RHI::TransferToken token = m_TransferManager->UploadBuffer(dst->GetHandle(), payload);
    ASSERT_TRUE(token.IsValid());

    // Register the pending load.
    auto handle = m_Pipeline->GetAssetManager().Create("buf-asset", std::make_unique<int>(7));
    m_Pipeline->RegisterAssetLoad(handle, token);

    // Poll until the transfer completes.
    int iterations = 0;
    while (!m_TransferManager->IsCompleted(token) && iterations < 10000)
    {
        m_Pipeline->ProcessUploads();
        ++iterations;
    }

    // One more call to process the completion.
    m_Pipeline->ProcessUploads();

    // The asset should have been finalized.
    EXPECT_EQ(m_Pipeline->GetAssetManager().GetState(handle), Core::Assets::LoadState::Ready);
}

TEST_F(AssetPipelineHeadlessTest, RegisterAssetLoadWithCompletionCallback)
{
    // Create a GPU buffer to transfer into.
    constexpr size_t bufSize = 128;
    auto dst = std::make_unique<RHI::VulkanBuffer>(
        *m_Device, bufSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::vector<std::byte> payload(bufSize, std::byte{0xAB});
    RHI::TransferToken token = m_TransferManager->UploadBuffer(dst->GetHandle(), payload);
    ASSERT_TRUE(token.IsValid());

    bool callbackFired = false;
    auto handle = m_Pipeline->GetAssetManager().Create("cb-asset", std::make_unique<int>(99));
    m_Pipeline->RegisterAssetLoad(handle, token, [&callbackFired]() { callbackFired = true; });

    // Poll until completion.
    int iterations = 0;
    while (!callbackFired && iterations < 10000)
    {
        m_Pipeline->ProcessUploads();
        ++iterations;
    }

    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(m_Pipeline->GetAssetManager().GetState(handle), Core::Assets::LoadState::Ready);
}

TEST_F(AssetPipelineHeadlessTest, ProcessUploadsNoOpWhenEmpty)
{
    // Should not crash or hang when there are no pending loads.
    m_Pipeline->ProcessUploads();
    m_Pipeline->ProcessUploads();
    SUCCEED();
}

TEST_F(AssetPipelineHeadlessTest, ProcessMainThreadQueueNoOpWhenEmpty)
{
    // Should not crash or hang when the queue is empty.
    m_Pipeline->ProcessMainThreadQueue();
    m_Pipeline->ProcessMainThreadQueue();
    SUCCEED();
}
