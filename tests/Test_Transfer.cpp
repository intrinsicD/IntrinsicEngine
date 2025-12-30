#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "RHI.Vulkan.hpp"

import RHI;
import Core;

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
        m_TransferMgr = std::make_unique<RHI::TransferManager>(m_Device);
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::TransferManager> m_TransferMgr;
};

TEST_F(TransferTest, AsyncBufferUpload) {
    using namespace RHI;

    const size_t bufferSize = 1024 * 1024; // 1MB

    // 1. Create a Destination Buffer (GPU Only)
    auto dstBuffer = std::make_unique<VulkanBuffer>(
        m_Device, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    // 2. Create Staging Buffer (CPU Visible)
    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        m_Device, bufferSize,
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