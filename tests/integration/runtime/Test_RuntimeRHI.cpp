#include <gtest/gtest.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>
#include <span>

#include "RHI.Vulkan.hpp"
// VMA types/enums are already pulled in transitively via RHI.Vulkan.hpp → RHI.VmaConfig.hpp.

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
    // Handle-body idiom: Texture is a lightweight RAII handle that requires a TextureManager.
    static_assert(std::is_constructible_v<RHI::Texture, RHI::TextureManager&, RHI::VulkanDevice&, uint32_t, uint32_t, VkFormat>);
    static_assert(std::is_nothrow_move_constructible_v<RHI::Texture>);
    static_assert(std::is_nothrow_move_assignable_v<RHI::Texture>);

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

TEST_F(TransferTest, HostVisibleBufferUnmapIsSafeForPersistentVmaMapping)
{
    RHI::VulkanBuffer buffer(
        *m_Device,
        256,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    ASSERT_TRUE(buffer.IsValid());
    ASSERT_NE(buffer.Map(), nullptr);

    // Regression for HARDEN-003: buffers created with VMA's create-mapped
    // pointer must not call vmaUnmapMemory unless this wrapper acquired an
    // explicit map with vmaMapMemory. The old behavior aborted in VMA here.
    buffer.Unmap();
    buffer.Unmap();
}

// Phase 1.1: Verify that SignalGraphicsTimeline / SafeDestroy is safe under
// concurrent access from multiple threads.
TEST_F(TransferTest, TimelineValue_ConcurrentSafeDestroy)
{
    using namespace RHI;

    // Signal the timeline a few times to establish a non-zero baseline.
    for (int i = 0; i < 5; ++i)
        (void)m_Device->SignalGraphicsTimeline();

    const uint64_t baseline = m_Device->GetGraphicsTimelineValue();
    EXPECT_GE(baseline, 5u);

    // Spawn threads that call SafeDestroy concurrently while the main thread signals.
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 200;
    auto destroyCallCount = std::make_shared<std::atomic<int>>(0);
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, destroyCallCount]()
        {
            for (int i = 0; i < kOpsPerThread; ++i)
            {
                m_Device->SafeDestroy([destroyCallCount]()
                {
                    destroyCallCount->fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    // Main thread keeps signaling while background threads enqueue deletions.
    for (int i = 0; i < 50; ++i)
        (void)m_Device->SignalGraphicsTimeline();

    for (auto& th : threads)
        th.join();

    // The timeline value should be monotonically above the baseline + our signals.
    EXPECT_GE(m_Device->GetGraphicsTimelineValue(), baseline + 50);

    // Wait for GPU and force-drain timeline deletes while captures are alive.
    vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    m_Device->FlushTimelineDeletionQueueNow();

    // All deletions should have executed.
    EXPECT_EQ(destroyCallCount->load(), kThreads * kOpsPerThread);
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
    auto* data = static_cast<uint32_t*>(stagingBuffer->Map());
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

        const auto pattern = static_cast<uint32_t>(0xA5A50000u | static_cast<uint32_t>(i & 0xFFFF));

        VkCommandBuffer cmd = m_TransferMgr->Begin();

        auto alloc = m_TransferMgr->AllocateStaging(uploadSize, copyAlign);
        ASSERT_NE(alloc.Buffer, VK_NULL_HANDLE) << "Staging belt ran out of space in test (may need larger default belt)";

        auto* dstWords = static_cast<uint32_t*>(alloc.MappedPtr);
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

// ---------------------------------------------------------------------------
// TransferManager error-path resource hygiene (E3c)
// ---------------------------------------------------------------------------

TEST_F(TransferTest, UploadBuffer_NullDst_ReturnsInvalidToken)
{
    // UploadBuffer with VK_NULL_HANDLE dst must return an invalid token
    // without leaking a command buffer (early return before Begin()).
    std::vector<std::byte> payload(256);
    auto token = m_TransferMgr->UploadBuffer(VK_NULL_HANDLE, payload);
    EXPECT_FALSE(token.IsValid());
}

TEST_F(TransferTest, UploadBuffer_EmptySrc_ReturnsInvalidToken)
{
    // Empty source should early-return without allocating a command buffer.
    auto dst = std::make_unique<RHI::VulkanBuffer>(
        *m_Device, 256,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::span<const std::byte> empty;
    auto token = m_TransferMgr->UploadBuffer(dst->GetHandle(), empty);
    EXPECT_FALSE(token.IsValid());
}

TEST_F(TransferTest, FreeCommandBuffer_ReleasesWithoutSubmit)
{
    // BeginUploadBatch + FreeCommandBuffer must not leak the command buffer.
    // Repeat many times to ensure no pool exhaustion.
    for (int i = 0; i < 100; ++i)
    {
        VkCommandBuffer cmd = m_TransferMgr->BeginUploadBatch();
        ASSERT_NE(cmd, VK_NULL_HANDLE);
        m_TransferMgr->FreeCommandBuffer(cmd);
    }
    SUCCEED();
}

TEST_F(TransferTest, FreeCommandBuffer_NullIsNoOp)
{
    // FreeCommandBuffer(VK_NULL_HANDLE) must not crash.
    m_TransferMgr->FreeCommandBuffer(VK_NULL_HANDLE);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Push Constant Runtime Validation (TODO §1.10)
// ---------------------------------------------------------------------------

TEST(RHIPipeline, BuilderTakesDeviceBySharedPtr)
{
    static_assert(std::is_constructible_v<RHI::PipelineBuilder, std::shared_ptr<RHI::VulkanDevice>>);
    SUCCEED();
}

TEST(RHIPipeline, ComputeBuilderTakesDeviceBySharedPtr)
{
    static_assert(std::is_constructible_v<RHI::ComputePipelineBuilder, std::shared_ptr<RHI::VulkanDevice>>);
    SUCCEED();
}

class PipelineBuilderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RHI::ContextConfig config{
            .AppName = "PipelineBuilderTest",
            .EnableValidation = true,
            .Headless = true,
        };
        m_Context = std::make_unique<RHI::VulkanContext>(config);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);
        m_MaxPushConstantsSize = props.limits.maxPushConstantsSize;
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    uint32_t m_MaxPushConstantsSize = 0;
};

TEST_F(PipelineBuilderTest, PushConstantValidation_ExceedsLimit_ReturnsError)
{
    // A push constant range that exceeds the device limit must fail early.
    RHI::PipelineBuilder pb(m_Device);

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = 0;
    range.size = m_MaxPushConstantsSize + 4; // Exceed by 4 bytes
    pb.AddPushConstantRange(range);

    auto result = pb.Build();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), VK_ERROR_UNKNOWN);
}

TEST_F(PipelineBuilderTest, PushConstantValidation_OffsetPlusSizeExceedsLimit_ReturnsError)
{
    // offset + size exceeding the limit must also be caught.
    RHI::PipelineBuilder pb(m_Device);

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = m_MaxPushConstantsSize - 4;
    range.size = 8; // offset + size = maxPushConstantsSize + 4
    pb.AddPushConstantRange(range);

    auto result = pb.Build();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), VK_ERROR_UNKNOWN);
}

TEST_F(PipelineBuilderTest, PushConstantValidation_WithinLimit_PassesValidation)
{
    // A push constant range within the device limit must pass validation.
    // Note: Build() will still fail because we have no shaders, but it must
    // not fail with VK_ERROR_UNKNOWN (our validation sentinel).
    RHI::PipelineBuilder pb(m_Device);

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = 0;
    range.size = 128; // Well within any device's limit (min guaranteed: 128)
    pb.AddPushConstantRange(range);

    auto result = pb.Build();
    // Build may fail for other reasons (no shaders, no color format, etc.)
    // but if it fails, it must NOT be due to push constant validation.
    if (!result.has_value())
    {
        EXPECT_NE(result.error(), VK_ERROR_UNKNOWN)
            << "Build failed with VK_ERROR_UNKNOWN, which indicates "
               "push constant validation rejected a valid range";
    }
}

TEST_F(PipelineBuilderTest, ComputePushConstantValidation_ExceedsLimit_ReturnsError)
{
    RHI::ComputePipelineBuilder cpb(m_Device);

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = m_MaxPushConstantsSize + 4;
    cpb.AddPushConstantRange(range);

    auto result = cpb.Build();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), VK_ERROR_UNKNOWN);
}

// =============================================================================
// PresentPolicy / SelectPresentMode
// =============================================================================

TEST(PresentPolicy, VSyncAlwaysReturnsFIFO)
{
    // FIFO is always available per Vulkan spec — even with an empty mode list,
    // the function returns FIFO as the safe fallback.
    const std::vector<VkPresentModeKHR> modes = {
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_FIFO_RELAXED_KHR,
    };
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::VSync, modes), VK_PRESENT_MODE_FIFO_KHR);
}

TEST(PresentPolicy, LowLatencyPrefersMailbox)
{
    const std::vector<VkPresentModeKHR> modes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::LowLatency, modes), VK_PRESENT_MODE_MAILBOX_KHR);
}

TEST(PresentPolicy, LowLatencyFallsBackToFIFO)
{
    const std::vector<VkPresentModeKHR> modes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::LowLatency, modes), VK_PRESENT_MODE_FIFO_KHR);
}

TEST(PresentPolicy, UncappedPrefersImmediate)
{
    const std::vector<VkPresentModeKHR> modes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::Uncapped, modes), VK_PRESENT_MODE_IMMEDIATE_KHR);
}

TEST(PresentPolicy, UncappedFallsToMailboxThenFIFO)
{
    // No Immediate → prefer Mailbox.
    {
        const std::vector<VkPresentModeKHR> modes = {
            VK_PRESENT_MODE_FIFO_KHR,
            VK_PRESENT_MODE_MAILBOX_KHR,
        };
        EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::Uncapped, modes), VK_PRESENT_MODE_MAILBOX_KHR);
    }

    // No Immediate, no Mailbox → FIFO.
    {
        const std::vector<VkPresentModeKHR> modes = { VK_PRESENT_MODE_FIFO_KHR };
        EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::Uncapped, modes), VK_PRESENT_MODE_FIFO_KHR);
    }
}

TEST(PresentPolicy, EditorThrottledPrefersFIFORelaxed)
{
    const std::vector<VkPresentModeKHR> modes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_FIFO_RELAXED_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::EditorThrottled, modes),
              VK_PRESENT_MODE_FIFO_RELAXED_KHR);
}

TEST(PresentPolicy, EditorThrottledFallsBackToFIFO)
{
    const std::vector<VkPresentModeKHR> modes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::EditorThrottled, modes), VK_PRESENT_MODE_FIFO_KHR);
}

TEST(PresentPolicy, EmptyModesAlwaysReturnsFIFO)
{
    const std::vector<VkPresentModeKHR> empty{};
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::VSync, empty), VK_PRESENT_MODE_FIFO_KHR);
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::LowLatency, empty), VK_PRESENT_MODE_FIFO_KHR);
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::Uncapped, empty), VK_PRESENT_MODE_FIFO_KHR);
    EXPECT_EQ(RHI::SelectPresentMode(RHI::PresentPolicy::EditorThrottled, empty), VK_PRESENT_MODE_FIFO_KHR);
}

TEST(PresentPolicy, ToStringCoversAllPolicies)
{
    EXPECT_EQ(RHI::ToString(RHI::PresentPolicy::VSync), "VSync");
    EXPECT_EQ(RHI::ToString(RHI::PresentPolicy::LowLatency), "LowLatency");
    EXPECT_EQ(RHI::ToString(RHI::PresentPolicy::Uncapped), "Uncapped");
    EXPECT_EQ(RHI::ToString(RHI::PresentPolicy::EditorThrottled), "EditorThrottled");
}

// =============================================================================
// VK_CHECK_* Error Semantics Policy (E3a)
// =============================================================================

namespace
{
    // Helper functions that exercise VK_CHECK_RETURN and VK_CHECK_BOOL with a
    // known VkResult, allowing us to verify deterministic control flow without
    // needing a live Vulkan device.

    int ReturnHelperWithVkResult(VkResult injected)
    {
        VK_CHECK_RETURN(injected, -1);
        return 42;
    }

    bool BoolHelperWithVkResult(VkResult injected)
    {
        VK_CHECK_BOOL(injected);
        return true;
    }

    int WarnHelperWithVkResult(VkResult injected)
    {
        VK_CHECK_WARN(injected);
        return 99;
    }
} // anonymous namespace

TEST(VkCheckPolicy, CheckReturn_Success_ContinuesExecution)
{
    // VK_SUCCESS must not trigger an early return — function returns 42.
    EXPECT_EQ(ReturnHelperWithVkResult(VK_SUCCESS), 42);
}

TEST(VkCheckPolicy, CheckReturn_Failure_ReturnsSentinel)
{
    // Any non-SUCCESS result must trigger early return with the sentinel value (-1).
    EXPECT_EQ(ReturnHelperWithVkResult(VK_ERROR_DEVICE_LOST), -1);
    EXPECT_EQ(ReturnHelperWithVkResult(VK_ERROR_OUT_OF_HOST_MEMORY), -1);
    EXPECT_EQ(ReturnHelperWithVkResult(VK_ERROR_INITIALIZATION_FAILED), -1);
}

TEST(VkCheckPolicy, CheckBool_Success_ReturnsTrue)
{
    EXPECT_TRUE(BoolHelperWithVkResult(VK_SUCCESS));
}

TEST(VkCheckPolicy, CheckBool_Failure_ReturnsFalse)
{
    EXPECT_FALSE(BoolHelperWithVkResult(VK_ERROR_DEVICE_LOST));
    EXPECT_FALSE(BoolHelperWithVkResult(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST(VkCheckPolicy, CheckWarn_Success_ContinuesExecution)
{
    EXPECT_EQ(WarnHelperWithVkResult(VK_SUCCESS), 99);
}

TEST(VkCheckPolicy, CheckWarn_Failure_ContinuesExecution)
{
    // VK_CHECK_WARN must log but continue — function still returns 99.
    EXPECT_EQ(WarnHelperWithVkResult(VK_ERROR_DEVICE_LOST), 99);
    EXPECT_EQ(WarnHelperWithVkResult(VK_NOT_READY), 99);
}

TEST(VkCheckPolicy, CheckFatal_Success_DoesNotAbort)
{
    // VK_CHECK_FATAL with VK_SUCCESS must not abort.
    VK_CHECK_FATAL(VK_SUCCESS);
    SUCCEED();
}

TEST(VkCheckPolicyDeathTest, CheckFatal_Failure_Aborts)
{
    // VK_CHECK_FATAL with a non-SUCCESS result must abort the process.
    // GTest death tests fork a subprocess, so this does not terminate the suite.
    EXPECT_DEATH(VK_CHECK_FATAL(VK_ERROR_DEVICE_LOST), "");
}

// =============================================================================
// Queue-Family Safety Contract Tests (E3b)
// =============================================================================

TEST(QueueFamilyIndices, SafeAccessors_AllFamiliesPresent)
{
    RHI::QueueFamilyIndices indices;
    indices.GraphicsFamily = 0u;
    indices.PresentFamily = 1u;
    indices.TransferFamily = 2u;

    EXPECT_EQ(indices.Graphics(), 0u);
    EXPECT_EQ(indices.Present(), 1u);
    EXPECT_EQ(indices.Transfer(), 2u);
    EXPECT_TRUE(indices.IsComplete());
    EXPECT_TRUE(indices.HasDistinctTransfer());
}

TEST(QueueFamilyIndices, HasDistinctTransfer_SameAsGraphics)
{
    RHI::QueueFamilyIndices indices;
    indices.GraphicsFamily = 0u;
    indices.TransferFamily = 0u;

    EXPECT_FALSE(indices.HasDistinctTransfer());
}

TEST(QueueFamilyIndices, HasDistinctTransfer_DifferentFromGraphics)
{
    RHI::QueueFamilyIndices indices;
    indices.GraphicsFamily = 0u;
    indices.TransferFamily = 2u;

    EXPECT_TRUE(indices.HasDistinctTransfer());
}

TEST(QueueFamilyIndices, HasDistinctTransfer_NoTransfer)
{
    RHI::QueueFamilyIndices indices;
    indices.GraphicsFamily = 0u;
    // TransferFamily not set

    EXPECT_FALSE(indices.HasDistinctTransfer());
}

TEST(QueueFamilyIndices, IsComplete_MissingPresent)
{
    RHI::QueueFamilyIndices indices;
    indices.GraphicsFamily = 0u;
    indices.TransferFamily = 0u;

    EXPECT_FALSE(indices.IsComplete());
}

// Integration test: headless device passes the queue-family contract.
// Requires a Vulkan-capable GPU; skips gracefully in headless CI environments.
TEST(QueueFamilyContract, HeadlessDevice_GraphicsAndTransferResolved)
{
    RHI::ContextConfig config{
        .AppName = "QueueFamilyContractTest",
        .EnableValidation = false,
        .Headless = true,
    };

    auto context = std::make_unique<RHI::VulkanContext>(config);
    if (!context->GetInstance())
    {
        GTEST_SKIP() << "No Vulkan instance available (headless environment)";
    }

    auto device = std::make_shared<RHI::VulkanDevice>(*context, VK_NULL_HANDLE);
    if (!device->IsValid())
    {
        GTEST_SKIP() << "No suitable GPU found";
    }

    auto indices = device->GetQueueIndices();

    // Graphics is always required.
    EXPECT_TRUE(indices.GraphicsFamily.has_value());

    // Transfer always resolves (dedicated or graphics fallback).
    EXPECT_TRUE(indices.TransferFamily.has_value());

    // Safe accessors must not throw after validation.
    EXPECT_EQ(indices.Graphics(), indices.GraphicsFamily.value());
    EXPECT_EQ(indices.Transfer(), indices.TransferFamily.value());

    // Present is not required in headless mode.
    // (It may or may not be set depending on the driver.)

    // Graphics queue must be non-null.
    EXPECT_NE(device->GetGraphicsQueue(), VK_NULL_HANDLE);

    // Transfer queue must be non-null (resolved by ValidateQueueFamilyContract).
    EXPECT_NE(device->GetTransferQueue(), VK_NULL_HANDLE);
}

// =============================================================================
// Queue Domain Abstraction Tests (B2)
// =============================================================================

TEST(QueueDomain, DomainName_ReturnsHumanReadableStrings)
{
    EXPECT_STREQ(RHI::QueueDomainName(RHI::QueueDomain::Graphics), "Graphics");
    EXPECT_STREQ(RHI::QueueDomainName(RHI::QueueDomain::Compute),  "Compute");
    EXPECT_STREQ(RHI::QueueDomainName(RHI::QueueDomain::Transfer), "Transfer");
}

TEST(QueueDomain, EnumValues_AreDistinct)
{
    EXPECT_NE(static_cast<uint8_t>(RHI::QueueDomain::Graphics),
              static_cast<uint8_t>(RHI::QueueDomain::Compute));
    EXPECT_NE(static_cast<uint8_t>(RHI::QueueDomain::Graphics),
              static_cast<uint8_t>(RHI::QueueDomain::Transfer));
    EXPECT_NE(static_cast<uint8_t>(RHI::QueueDomain::Compute),
              static_cast<uint8_t>(RHI::QueueDomain::Transfer));
}

TEST(QueueDomain, EnumSize_IsCompact)
{
    static_assert(sizeof(RHI::QueueDomain) == 1);
    SUCCEED();
}

// GPU integration test: QueueSubmitter resolves queues and family indices.
TEST(QueueSubmitter, HeadlessDevice_ResolvesQueuesAndFamilies)
{
    RHI::ContextConfig config{
        .AppName = "QueueSubmitterTest",
        .EnableValidation = false,
        .Headless = true,
    };

    auto context = std::make_unique<RHI::VulkanContext>(config);
    if (!context->GetInstance())
    {
        GTEST_SKIP() << "No Vulkan instance available (headless environment)";
    }

    auto device = std::make_shared<RHI::VulkanDevice>(*context, VK_NULL_HANDLE);
    if (!device->IsValid())
    {
        GTEST_SKIP() << "No suitable GPU found";
    }

    RHI::QueueSubmitter submitter(*device);

    // Graphics domain always has a queue.
    EXPECT_NE(submitter.GetQueue(RHI::QueueDomain::Graphics), VK_NULL_HANDLE);

    // Compute maps to graphics (same queue) in the initial implementation.
    EXPECT_EQ(submitter.GetQueue(RHI::QueueDomain::Compute),
              submitter.GetQueue(RHI::QueueDomain::Graphics));

    // Transfer always resolves (dedicated or graphics fallback).
    EXPECT_NE(submitter.GetQueue(RHI::QueueDomain::Transfer), VK_NULL_HANDLE);

    // Queue family indices must be valid.
    const auto indices = device->GetQueueIndices();
    EXPECT_EQ(submitter.GetQueueFamilyIndex(RHI::QueueDomain::Graphics), indices.Graphics());
    EXPECT_EQ(submitter.GetQueueFamilyIndex(RHI::QueueDomain::Compute), indices.Graphics());
    EXPECT_EQ(submitter.GetQueueFamilyIndex(RHI::QueueDomain::Transfer), indices.Transfer());
}

TEST(QueueSubmitter, RequiresOwnershipTransfer_SameDomain_ReturnsFalse)
{
    RHI::ContextConfig config{
        .AppName = "QueueSubmitterOwnershipTest",
        .EnableValidation = false,
        .Headless = true,
    };

    auto context = std::make_unique<RHI::VulkanContext>(config);
    if (!context->GetInstance())
    {
        GTEST_SKIP() << "No Vulkan instance available (headless environment)";
    }

    auto device = std::make_shared<RHI::VulkanDevice>(*context, VK_NULL_HANDLE);
    if (!device->IsValid())
    {
        GTEST_SKIP() << "No suitable GPU found";
    }

    RHI::QueueSubmitter submitter(*device);

    // Same domain never requires ownership transfer.
    EXPECT_FALSE(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Graphics, RHI::QueueDomain::Graphics));
    EXPECT_FALSE(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Transfer, RHI::QueueDomain::Transfer));
    EXPECT_FALSE(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Compute, RHI::QueueDomain::Compute));

    // Compute→Graphics never requires transfer (same family in initial impl).
    EXPECT_FALSE(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Graphics, RHI::QueueDomain::Compute));
    EXPECT_FALSE(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Compute, RHI::QueueDomain::Graphics));
}

TEST(QueueSubmitter, RequiresOwnershipTransfer_GraphicsToTransfer_MatchesDedicatedStatus)
{
    RHI::ContextConfig config{
        .AppName = "QueueSubmitterOwnershipTest2",
        .EnableValidation = false,
        .Headless = true,
    };

    auto context = std::make_unique<RHI::VulkanContext>(config);
    if (!context->GetInstance())
    {
        GTEST_SKIP() << "No Vulkan instance available (headless environment)";
    }

    auto device = std::make_shared<RHI::VulkanDevice>(*context, VK_NULL_HANDLE);
    if (!device->IsValid())
    {
        GTEST_SKIP() << "No suitable GPU found";
    }

    RHI::QueueSubmitter submitter(*device);

    // Ownership transfer between Graphics↔Transfer is needed iff the transfer
    // queue is a dedicated (distinct) family.
    const bool hasDedicated = submitter.HasDedicatedQueue(RHI::QueueDomain::Transfer);
    EXPECT_EQ(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Graphics, RHI::QueueDomain::Transfer),
              hasDedicated);
    EXPECT_EQ(submitter.RequiresOwnershipTransfer(RHI::QueueDomain::Transfer, RHI::QueueDomain::Graphics),
              hasDedicated);
}

TEST(QueueSubmitter, HasDedicatedQueue_GraphicsAlwaysTrue_ComputeAlwaysFalse)
{
    RHI::ContextConfig config{
        .AppName = "QueueSubmitterDedicatedTest",
        .EnableValidation = false,
        .Headless = true,
    };

    auto context = std::make_unique<RHI::VulkanContext>(config);
    if (!context->GetInstance())
    {
        GTEST_SKIP() << "No Vulkan instance available (headless environment)";
    }

    auto device = std::make_shared<RHI::VulkanDevice>(*context, VK_NULL_HANDLE);
    if (!device->IsValid())
    {
        GTEST_SKIP() << "No suitable GPU found";
    }

    RHI::QueueSubmitter submitter(*device);

    // Graphics always has its own queue.
    EXPECT_TRUE(submitter.HasDedicatedQueue(RHI::QueueDomain::Graphics));

    // Compute is not yet dedicated (shares graphics).
    EXPECT_FALSE(submitter.HasDedicatedQueue(RHI::QueueDomain::Compute));

    // Transfer: depends on hardware.
    EXPECT_EQ(submitter.HasDedicatedQueue(RHI::QueueDomain::Transfer),
              device->GetQueueIndices().HasDistinctTransfer());
}

TEST(QueueSubmitter, GetDevice_ReturnsOriginalDevice)
{
    RHI::ContextConfig config{
        .AppName = "QueueSubmitterDeviceTest",
        .EnableValidation = false,
        .Headless = true,
    };

    auto context = std::make_unique<RHI::VulkanContext>(config);
    if (!context->GetInstance())
    {
        GTEST_SKIP() << "No Vulkan instance available (headless environment)";
    }

    auto device = std::make_shared<RHI::VulkanDevice>(*context, VK_NULL_HANDLE);
    if (!device->IsValid())
    {
        GTEST_SKIP() << "No suitable GPU found";
    }

    RHI::QueueSubmitter submitter(*device);
    EXPECT_EQ(&submitter.GetDevice(), device.get());
}
