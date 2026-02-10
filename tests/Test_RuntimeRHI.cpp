#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <thread>
#include <vector>
#include <atomic>

#include "RHI.Vulkan.hpp"

// Ensure VMA memory usage enums are visible to this TU.
#include <vk_mem_alloc.h>

import RHI;
import Core;

TEST(RHIBindless, ConstructorTakesDeviceByRef)
{
    // Compile-time API contract: bindless system must be device-owned, not shared-owned.
    static_assert(std::is_constructible_v<RHI::BindlessDescriptorSystem, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::BindlessDescriptorSystem, std::shared_ptr<RHI::VulkanDevice>>);

    SUCCEED();
}

TEST(RHIBuffer, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::VulkanBuffer, RHI::VulkanDevice&, size_t, VkBufferUsageFlags, VmaMemoryUsage>);
    static_assert(!std::is_constructible_v<RHI::VulkanBuffer, std::shared_ptr<RHI::VulkanDevice>, size_t, VkBufferUsageFlags, VmaMemoryUsage>);
    SUCCEED();
}

TEST(RHIImage, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::VulkanImage,
        RHI::VulkanDevice&,
        uint32_t, uint32_t, uint32_t,
        VkFormat, VkImageUsageFlags, VkImageAspectFlags, VkSharingMode>);

    static_assert(!std::is_constructible_v<RHI::VulkanImage,
        std::shared_ptr<RHI::VulkanDevice>,
        uint32_t, uint32_t, uint32_t,
        VkFormat, VkImageUsageFlags, VkImageAspectFlags, VkSharingMode>);

    SUCCEED();
}

TEST(RHIShader, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::ShaderModule, RHI::VulkanDevice&, const std::string&, RHI::ShaderStage>);
    static_assert(!std::is_constructible_v<RHI::ShaderModule, std::shared_ptr<RHI::VulkanDevice>, const std::string&, RHI::ShaderStage>);
    SUCCEED();
}

TEST(RHITexture, ConstructorTakesDeviceByRef)
{
    // Handle-body idiom: Texture is a lightweight RAII handle that requires a TextureSystem.
    static_assert(std::is_constructible_v<RHI::Texture, RHI::TextureSystem&, RHI::VulkanDevice&, uint32_t, uint32_t, VkFormat>);

    // Must not accept shared_ptr device anymore.
    static_assert(!std::is_constructible_v<RHI::Texture, std::shared_ptr<RHI::VulkanDevice>, uint32_t, uint32_t, VkFormat>);

    SUCCEED();
}

TEST(RHIDescriptors, LayoutAndAllocatorTakeDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::DescriptorLayout, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::DescriptorLayout, std::shared_ptr<RHI::VulkanDevice>>);

    static_assert(std::is_constructible_v<RHI::DescriptorAllocator, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::DescriptorAllocator, std::shared_ptr<RHI::VulkanDevice>>);

    SUCCEED();
}

TEST(RHITransfer, ManagerTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::TransferManager, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::TransferManager, std::shared_ptr<RHI::VulkanDevice>>);
    SUCCEED();
}

TEST(RHIStagingBelt, TakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::StagingBelt, RHI::VulkanDevice&, size_t>);
    static_assert(!std::is_constructible_v<RHI::StagingBelt, std::shared_ptr<RHI::VulkanDevice>, size_t>);
    SUCCEED();
}

class DescriptorAllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RHI::ContextConfig config{
            .AppName = "DescriptorAllocatorTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(config);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        m_Layout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        ASSERT_TRUE(m_Layout->IsValid());

        m_Allocator = std::make_unique<RHI::DescriptorAllocator>(*m_Device);
        ASSERT_TRUE(m_Allocator->IsValid());
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;

    std::unique_ptr<RHI::DescriptorLayout> m_Layout;
    std::unique_ptr<RHI::DescriptorAllocator> m_Allocator;
};

TEST_F(DescriptorAllocatorTest, GrowsPoolsAndAllocatesManySets)
{
    // Force pool growth by allocating more sets than a single pool's maxSets.
    // The allocator should transparently create additional pools when exhausted.
    constexpr uint32_t kAllocCount = 10'000;

    for (uint32_t i = 0; i < kAllocCount; ++i)
    {
        VkDescriptorSet set = m_Allocator->Allocate(m_Layout->GetHandle());
        ASSERT_NE(set, VK_NULL_HANDLE) << "Allocation failed at i=" << i;
    }
}

TEST_F(DescriptorAllocatorTest, ResetRecyclesPoolsAndAllocationsStillSucceed)
{
    constexpr uint32_t kAllocCount = 6'000;

    for (uint32_t i = 0; i < kAllocCount; ++i)
    {
        VkDescriptorSet set = m_Allocator->Allocate(m_Layout->GetHandle());
        ASSERT_NE(set, VK_NULL_HANDLE) << "Pre-reset allocation failed at i=" << i;
    }

    // Reset at frame start: pools must be reset and reused.
    m_Allocator->Reset();

    for (uint32_t i = 0; i < kAllocCount; ++i)
    {
        VkDescriptorSet set = m_Allocator->Allocate(m_Layout->GetHandle());
        ASSERT_NE(set, VK_NULL_HANDLE) << "Post-reset allocation failed at i=" << i;
    }
}

class TransferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Minimal Vulkan setup for testing
        RHI::ContextConfig config{
            .AppName = "TransferTest",
            .EnableValidation = true,
            .Headless = true
        };
        m_Context = std::make_unique<RHI::VulkanContext>(config);

        // Note: In a real test, you'd need a surface.
        // For headless RHI tests, we assume a mock or a dummy window.
        // Here we use the Device's ability to pick a GPU without a surface for pure transfer.
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
        m_TransferMgr = std::make_unique<RHI::TransferManager>(*m_Device);
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::TransferManager> m_TransferMgr;
};

// Phase 1.1: Verify that SignalGraphicsTimeline / SafeDestroy is safe under
// concurrent access from multiple threads.
TEST_F(TransferTest, TimelineValue_ConcurrentSafeDestroy)
{
    using namespace RHI;

    // Signal the timeline a few times to establish a non-zero baseline.
    for (int i = 0; i < 5; ++i)
        m_Device->SignalGraphicsTimeline();

    const uint64_t baseline = m_Device->GetGraphicsTimelineValue();
    EXPECT_GE(baseline, 5u);

    // Spawn threads that call SafeDestroy concurrently while the main thread signals.
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 200;
    std::atomic<int> destroyCallCount{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&]()
        {
            for (int i = 0; i < kOpsPerThread; ++i)
            {
                m_Device->SafeDestroy([&destroyCallCount]()
                {
                    destroyCallCount.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    // Main thread keeps signaling while background threads enqueue deletions.
    for (int i = 0; i < 50; ++i)
        m_Device->SignalGraphicsTimeline();

    for (auto& th : threads)
        th.join();

    // The timeline value should be monotonically above the baseline + our signals.
    EXPECT_GE(m_Device->GetGraphicsTimelineValue(), baseline + 50);

    // Wait for GPU and collect garbage — all deferred deletions should execute.
    vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    m_Device->CollectGarbage();

    // All deletions should have executed.
    EXPECT_EQ(destroyCallCount.load(), kThreads * kOpsPerThread);
}

TEST_F(TransferTest, AsyncBufferUpload) {
    using namespace RHI;

    const size_t bufferSize = 1024 * 1024; // 1MB

    // 1. Create a Destination Buffer (GPU Only)
    auto dstBuffer = std::make_unique<VulkanBuffer>(
        *m_Device, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    // 2. Create Staging Buffer (CPU Visible)
    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        *m_Device, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    // Fill staging data
    uint32_t* data = static_cast<uint32_t*>(stagingBuffer->Map());
    for(size_t i = 0; i < bufferSize/4; ++i) data[i] = 0xDEADBEEF;
    stagingBuffer->Unmap();

    // 3. Record and Submit
    VkCommandBuffer cmd = m_TransferMgr->Begin();

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer->GetHandle(), dstBuffer->GetHandle(), 1, &copyRegion);

    // Hand over staging buffer ownership to the manager
    std::vector<std::unique_ptr<VulkanBuffer>> stagingList;
    stagingList.push_back(std::move(stagingBuffer));

    TransferToken token = m_TransferMgr->Submit(cmd, std::move(stagingList));

    // 4. Verification
    EXPECT_TRUE(token.IsValid());

    // The GPU might be fast, but usually, immediately after submission, it's not "done".
    // We check that we can poll it.
    bool completedImmediately = m_TransferMgr->IsCompleted(token);
    Core::Log::Info("Transfer completed immediately? {}", completedImmediately);

    // 5. Cleanup / Wait
    // In a real engine, we'd loop. Here we block to finish the test.
    while(!m_TransferMgr->IsCompleted(token)) {
        std::this_thread::yield();
    }

    EXPECT_TRUE(m_TransferMgr->IsCompleted(token));

    // GC should now clear the staging buffer
    m_TransferMgr->GarbageCollect();
}

TEST_F(TransferTest, StagingBeltManySmallUploads)
{
    using namespace RHI;

    constexpr size_t uploadSize = 4 * 1024; // 4 KiB
    constexpr int uploadCount = 1024;

    std::vector<std::unique_ptr<VulkanBuffer>> gpuBuffers;
    gpuBuffers.reserve(uploadCount);

    std::vector<TransferToken> tokens;
    tokens.reserve(uploadCount);

    // Use Vulkan copy offset alignment (queried like production).
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);
    const size_t copyAlign = std::max<size_t>(16, static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment));

    for (int i = 0; i < uploadCount; ++i)
    {
        auto dst = std::make_unique<VulkanBuffer>(
            *m_Device, uploadSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

        const uint32_t pattern = static_cast<uint32_t>(0xA5A50000u | static_cast<uint32_t>(i & 0xFFFF));

        VkCommandBuffer cmd = m_TransferMgr->Begin();

        auto alloc = m_TransferMgr->AllocateStaging(uploadSize, copyAlign);
        ASSERT_NE(alloc.Buffer, VK_NULL_HANDLE) << "Staging belt ran out of space in test (may need larger default belt)";

        uint32_t* dstWords = static_cast<uint32_t*>(alloc.MappedPtr);
        for (size_t w = 0; w < uploadSize / sizeof(uint32_t); ++w)
            dstWords[w] = pattern;

        VkBufferCopy region{};
        region.srcOffset = alloc.Offset;
        region.size = uploadSize;
        vkCmdCopyBuffer(cmd, alloc.Buffer, dst->GetHandle(), 1, &region);

        tokens.push_back(m_TransferMgr->Submit(cmd));
        gpuBuffers.push_back(std::move(dst));
    }

    // Wait for last token and GC.
    const TransferToken last = tokens.back();
    while (!m_TransferMgr->IsCompleted(last))
        std::this_thread::yield();

    m_TransferMgr->GarbageCollect();
}

TEST_F(TransferTest, UploadBufferHelper)
{
    using namespace RHI;

    constexpr size_t bufferSize = 64 * 1024;

    auto dst = std::make_unique<VulkanBuffer>(
        *m_Device, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::vector<std::byte> payload(bufferSize);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::byte>(static_cast<uint8_t>(i));

    TransferToken token = m_TransferMgr->UploadBuffer(dst->GetHandle(), payload);
    ASSERT_TRUE(token.IsValid());

    while (!m_TransferMgr->IsCompleted(token))
        m_TransferMgr->GarbageCollect();

    // Basic sanity: token completed without device loss. Readback correctness is covered by other tests.
    SUCCEED();
}

TEST_F(TransferTest, UploadBufferBatchHelper)
{
    using namespace RHI;

    constexpr size_t uploadSize = 4096;
    constexpr int uploadCount = 256;

    std::vector<std::unique_ptr<VulkanBuffer>> dst;
    dst.reserve(uploadCount);

    TransferManager::UploadBatchConfig cfg{};
    VkCommandBuffer cmd = m_TransferMgr->BeginUploadBatch(cfg);

    std::vector<std::byte> payload(uploadSize);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::byte>(0x5A);

    for (int i = 0; i < uploadCount; ++i)
    {
        dst.push_back(std::make_unique<VulkanBuffer>(
            *m_Device, uploadSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY));

        const bool ok = m_TransferMgr->EnqueueUploadBuffer(cmd, dst.back()->GetHandle(), payload);
        ASSERT_TRUE(ok) << "Failed at i=" << i;
    }

    TransferToken token = m_TransferMgr->EndUploadBatch(cmd);
    ASSERT_TRUE(token.IsValid());

    while (!m_TransferMgr->IsCompleted(token))
        std::this_thread::yield();

    m_TransferMgr->GarbageCollect();
    SUCCEED();
}
