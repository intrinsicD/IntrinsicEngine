#include <gtest/gtest.h>
#include <memory>
#include <type_traits>

#include "RHI.Vulkan.hpp"

import Runtime.GraphicsBackend;
import RHI;
import Core;

// ---------------------------------------------------------------------------
// Compile-time API contract tests
// ---------------------------------------------------------------------------

TEST(GraphicsBackend, NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::GraphicsBackend>);
    static_assert(!std::is_copy_assignable_v<Runtime::GraphicsBackend>);
    SUCCEED();
}

TEST(GraphicsBackend, NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::GraphicsBackend>);
    static_assert(!std::is_move_assignable_v<Runtime::GraphicsBackend>);
    SUCCEED();
}

TEST(GraphicsBackend, RequiresWindowAndConfig)
{
    // Must be constructible with Window& + config.
    static_assert(std::is_constructible_v<Runtime::GraphicsBackend,
                                          Core::Windowing::Window&,
                                          const Runtime::GraphicsBackendConfig&>);

    // Must NOT be default-constructible.
    static_assert(!std::is_default_constructible_v<Runtime::GraphicsBackend>);

    SUCCEED();
}

TEST(GraphicsBackendConfig, DefaultValues)
{
    Runtime::GraphicsBackendConfig cfg{};
    EXPECT_EQ(cfg.AppName, "Intrinsic App");
    EXPECT_TRUE(cfg.EnableValidation);
}

// ---------------------------------------------------------------------------
// Headless integration tests (real Vulkan, no window surface)
// ---------------------------------------------------------------------------

class GraphicsBackendHeadlessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Minimal headless Vulkan setup to verify subsystem wiring.
        RHI::ContextConfig ctxConfig{
            .AppName = "GraphicsBackendTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
};

TEST_F(GraphicsBackendHeadlessTest, DescriptorSubsystemsCreatable)
{
    // Verify that the descriptor subsystems that GraphicsBackend bundles
    // can be created and destroyed in the correct order headlessly.
    auto bindless = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
    auto texSys = std::make_unique<RHI::TextureSystem>(*m_Device, *bindless);
    auto layout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
    auto pool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);
    auto transfer = std::make_unique<RHI::TransferManager>(*m_Device);

    EXPECT_TRUE(layout->IsValid());
    EXPECT_TRUE(pool->IsValid());

    // Destruction order: transfer, pool, layout, texSys, bindless (reverse of creation).
    transfer.reset();
    pool.reset();
    layout.reset();
    texSys.reset();
    bindless.reset();
}

TEST_F(GraphicsBackendHeadlessTest, DestructionOrderSafe)
{
    // Create subsystems in GraphicsBackend's init order, then destroy
    // in its destructor order.  No crashes = correct ordering.
    auto bindless = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
    auto texSys = std::make_unique<RHI::TextureSystem>(*m_Device, *bindless);
    auto transfer = std::make_unique<RHI::TransferManager>(*m_Device);
    auto layout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
    auto pool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);

    // Allocate a descriptor set to exercise the pool.
    VkDescriptorSet set = pool->Allocate(layout->GetHandle());
    ASSERT_NE(set, VK_NULL_HANDLE);

    // Mirror GraphicsBackend::~GraphicsBackend() destruction order:
    // 1. Texture system clear
    texSys->ProcessDeletions();
    texSys->Clear();

    // 2. Descriptors
    bindless.reset();
    pool.reset();
    layout.reset();

    // 3. Transfer
    transfer.reset();

    // 4. Texture system
    texSys.reset();

    // 5. Flush deferred deletions
    m_Device->FlushAllDeletionQueues();

    SUCCEED();
}

TEST_F(GraphicsBackendHeadlessTest, TransferManagerOperational)
{
    // Verify that a TransferManager created by GraphicsBackend's init
    // pattern can actually perform a transfer.
    auto transfer = std::make_unique<RHI::TransferManager>(*m_Device);

    constexpr size_t bufSize = 4096;
    auto dst = std::make_unique<RHI::VulkanBuffer>(
        *m_Device, bufSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::vector<std::byte> payload(bufSize, std::byte{0xAB});
    RHI::TransferToken token = transfer->UploadBuffer(dst->GetHandle(), payload);
    ASSERT_TRUE(token.IsValid());

    while (!transfer->IsCompleted(token))
        transfer->GarbageCollect();

    SUCCEED();
}
