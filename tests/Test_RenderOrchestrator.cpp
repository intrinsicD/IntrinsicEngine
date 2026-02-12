#include <gtest/gtest.h>
#include <memory>
#include <type_traits>

#include "RHI.Vulkan.hpp"

import Runtime.RenderOrchestrator;
import RHI;
import Graphics;
import Core;

// ---------------------------------------------------------------------------
// Compile-time API contract tests
// ---------------------------------------------------------------------------

TEST(RenderOrchestrator, NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::RenderOrchestrator>);
    static_assert(!std::is_copy_assignable_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderOrchestrator, NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::RenderOrchestrator>);
    static_assert(!std::is_move_assignable_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderOrchestrator, NotDefaultConstructible)
{
    static_assert(!std::is_default_constructible_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Headless integration tests (real Vulkan, no window surface)
// ---------------------------------------------------------------------------

class RenderOrchestratorHeadlessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Minimal headless Vulkan setup to verify subsystem wiring.
        RHI::ContextConfig ctxConfig{
            .AppName = "RenderOrchestratorTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        m_Bindless = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
        m_TextureSystem = std::make_unique<RHI::TextureSystem>(*m_Device, *m_Bindless);
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);
    }

    void TearDown() override
    {
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_TextureSystem.reset();
        m_Bindless.reset();
        m_Device->FlushAllDeletionQueues();
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::BindlessDescriptorSystem> m_Bindless;
    std::unique_ptr<RHI::TextureSystem> m_TextureSystem;
    std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
    std::unique_ptr<RHI::DescriptorAllocator> m_DescriptorPool;
};

TEST_F(RenderOrchestratorHeadlessTest, ShaderRegistryPopulated)
{
    // The ShaderRegistry should have entries after construction.
    // We can't fully construct RenderOrchestrator without a swapchain/renderer,
    // but we can verify the ShaderRegistry type is usable.
    Graphics::ShaderRegistry reg;
    reg.Register(Core::Hash::StringID{42}, "test.spv");
    EXPECT_TRUE(reg.Contains(Core::Hash::StringID{42}));
    auto path = reg.Get(Core::Hash::StringID{42});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "test.spv");
}

TEST_F(RenderOrchestratorHeadlessTest, DescriptorSubsystemsReady)
{
    // Verify the descriptor subsystems that RenderOrchestrator depends on
    // (provided by GraphicsBackend) can be created headlessly.
    EXPECT_TRUE(m_DescriptorLayout->IsValid());
    EXPECT_TRUE(m_DescriptorPool->IsValid());
}

TEST_F(RenderOrchestratorHeadlessTest, MaterialSystemCreatable)
{
    // MaterialSystem is one of the first things RenderOrchestrator creates.
    // Verify it can be constructed with headless Vulkan infrastructure.
    Core::Assets::AssetManager assetManager;
    auto matSys = std::make_unique<Graphics::MaterialSystem>(*m_TextureSystem, assetManager);
    EXPECT_NE(matSys, nullptr);

    // Create and destroy a material to exercise the pool.
    Graphics::MaterialData data{};
    data.RoughnessFactor = 0.5f;
    auto handle = matSys->Create(data);
    EXPECT_NE(handle, Graphics::MaterialHandle{});

    matSys->Destroy(handle);
    matSys->ProcessDeletions(1);
}

TEST_F(RenderOrchestratorHeadlessTest, GeometryPoolInitializable)
{
    // GeometryPool is initialized by RenderOrchestrator with frames-in-flight.
    Graphics::GeometryPool pool;
    pool.Initialize(m_Device->GetFramesInFlight());
    pool.Clear();
    SUCCEED();
}
