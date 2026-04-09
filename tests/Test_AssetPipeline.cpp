#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <atomic>
#include <thread>

#include <entt/entity/fwd.hpp>
#include "RHI.Vulkan.hpp"

import Asset.Pipeline;
import Asset.Manager;
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

namespace
{
    void WaitForTransferCompletion(RHI::TransferManager& transferManager, RHI::TransferToken token)
    {
        ASSERT_TRUE(token.IsValid());

        int iterations = 0;
        while (!transferManager.IsCompleted(token) && iterations < 10000)
        {
            transferManager.GarbageCollect();
            ++iterations;
        }

        ASSERT_TRUE(transferManager.IsCompleted(token));
    }
}

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
    m_Pipeline->GetAssetManager().MoveToProcessing(handle);
    m_Pipeline->RegisterAssetLoad(handle, token);

    // Upload completion alone must not finalize the asset until ProcessUploads runs.
    WaitForTransferCompletion(*m_TransferManager, token);
    EXPECT_EQ(m_Pipeline->GetAssetManager().GetState(handle), Core::Assets::LoadState::Processing);

    // Process the completion and finalize the asset.
    m_Pipeline->ProcessUploads();
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
    m_Pipeline->GetAssetManager().MoveToProcessing(handle);
    m_Pipeline->RegisterAssetLoad(handle, token, [&callbackFired]() { callbackFired = true; });

    WaitForTransferCompletion(*m_TransferManager, token);
    EXPECT_FALSE(callbackFired);
    EXPECT_EQ(m_Pipeline->GetAssetManager().GetState(handle), Core::Assets::LoadState::Processing);

    m_Pipeline->ProcessUploads();
    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(m_Pipeline->GetAssetManager().GetState(handle), Core::Assets::LoadState::Ready);
}

TEST_F(AssetPipelineHeadlessTest, AssetStreamingCompletionSeparatesQueuedUploadedAndFinalizedStages)
{
    constexpr size_t bufSize = 192;
    auto dst = std::make_unique<RHI::VulkanBuffer>(
        *m_Device, bufSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::vector<std::byte> payload(bufSize, std::byte{0x5A});
    RHI::TransferToken token = m_TransferManager->UploadBuffer(dst->GetHandle(), payload);
    ASSERT_TRUE(token.IsValid());

    auto handle = m_Pipeline->GetAssetManager().Create("streaming-semantics", std::make_unique<int>(123));
    auto& assetManager = m_Pipeline->GetAssetManager();
    assetManager.MoveToProcessing(handle);

    int completionCallbacks = 0;
    int readyNotifications = 0;

    assetManager.RequestNotify(handle, [&](Core::Assets::AssetHandle readyHandle)
    {
        EXPECT_EQ(readyHandle.ID, handle.ID);
        ++readyNotifications;
    });

    m_Pipeline->RegisterAssetLoad(handle, token, [&]()
    {
        ++completionCallbacks;
    });

    // Stage 1: queued for GPU completion, but not yet uploaded/finalized.
    EXPECT_EQ(assetManager.GetState(handle), Core::Assets::LoadState::Processing);
    EXPECT_EQ(completionCallbacks, 0);
    EXPECT_EQ(readyNotifications, 0);

    // Stage 2: transfer is uploaded on the GPU timeline, but asset finalization
    // remains deferred until the streaming lane polls ProcessUploads().
    WaitForTransferCompletion(*m_TransferManager, token);
    EXPECT_EQ(assetManager.GetState(handle), Core::Assets::LoadState::Processing);
    EXPECT_EQ(completionCallbacks, 0);
    EXPECT_EQ(readyNotifications, 0);

    // Stage 3: ProcessUploads finalizes the asset exactly once.
    m_Pipeline->ProcessUploads();
    EXPECT_EQ(completionCallbacks, 1);
    EXPECT_EQ(assetManager.GetState(handle), Core::Assets::LoadState::Ready);
    EXPECT_EQ(readyNotifications, 0);

    // Ready notifications are still main-thread AssetManager work and therefore
    // do not fire until Update() drains the queue.
    assetManager.Update();
    EXPECT_EQ(completionCallbacks, 1);
    EXPECT_EQ(readyNotifications, 1);

    m_Pipeline->ProcessUploads();
    assetManager.Update();
    EXPECT_EQ(completionCallbacks, 1);
    EXPECT_EQ(readyNotifications, 1);
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
