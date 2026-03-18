#include <gtest/gtest.h>
#include <type_traits>
#include <memory>

#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"

import Runtime.SceneManager;
import ECS;
import Core;
import RHI;
import Graphics;

// ---------------------------------------------------------------------------
// Compile-time API contract tests
// ---------------------------------------------------------------------------

TEST(SceneManager, NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::SceneManager>);
    static_assert(!std::is_copy_assignable_v<Runtime::SceneManager>);
    SUCCEED();
}

TEST(SceneManager, NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::SceneManager>);
    static_assert(!std::is_move_assignable_v<Runtime::SceneManager>);
    SUCCEED();
}

TEST(SceneManager, DefaultConstructible)
{
    static_assert(std::is_default_constructible_v<Runtime::SceneManager>);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Functional tests (no Vulkan required)
// ---------------------------------------------------------------------------

class SceneManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Mgr = std::make_unique<Runtime::SceneManager>();
    }

    void TearDown() override
    {
        m_Mgr.reset();
    }

    std::unique_ptr<Runtime::SceneManager> m_Mgr;
};

TEST_F(SceneManagerTest, SceneAccessible)
{
    // GetScene() returns a valid, empty scene.
    ECS::Scene& scene = m_Mgr->GetScene();
    EXPECT_EQ(scene.Size(), 0u);
}

TEST_F(SceneManagerTest, RegistryAccessible)
{
    // GetRegistry() returns the same registry as GetScene().GetRegistry().
    entt::registry& reg = m_Mgr->GetRegistry();
    entt::registry& sceneReg = m_Mgr->GetScene().GetRegistry();
    EXPECT_EQ(&reg, &sceneReg);
}

TEST_F(SceneManagerTest, CreateEntityViaScene)
{
    entt::entity e = m_Mgr->GetScene().CreateEntity("TestEntity");
    EXPECT_TRUE(e != entt::null);
    EXPECT_EQ(m_Mgr->GetScene().Size(), 1u);

    // Verify default components were added by Scene::CreateEntity.
    auto& reg = m_Mgr->GetRegistry();
    EXPECT_TRUE(reg.all_of<ECS::Components::NameTag::Component>(e));
    EXPECT_TRUE(reg.all_of<ECS::Components::Transform::Component>(e));
}

TEST_F(SceneManagerTest, ClearEmptiesRegistry)
{
    m_Mgr->GetScene().CreateEntity("A");
    m_Mgr->GetScene().CreateEntity("B");
    m_Mgr->GetScene().CreateEntity("C");
    EXPECT_EQ(m_Mgr->GetScene().Size(), 3u);

    m_Mgr->Clear();
    EXPECT_EQ(m_Mgr->GetScene().Size(), 0u);
}

TEST_F(SceneManagerTest, MultipleCreateAndDestroy)
{
    // Create, destroy, recreate — no crashes, counters correct.
    entt::entity e1 = m_Mgr->GetScene().CreateEntity("E1");
    [[maybe_unused]] entt::entity e2 = m_Mgr->GetScene().CreateEntity("E2");
    EXPECT_EQ(m_Mgr->GetScene().Size(), 2u);

    m_Mgr->GetRegistry().destroy(e1);
    EXPECT_EQ(m_Mgr->GetScene().Size(), 1u);

    entt::entity e3 = m_Mgr->GetScene().CreateEntity("E3");
    EXPECT_TRUE(e3 != entt::null);
    EXPECT_EQ(m_Mgr->GetScene().Size(), 2u);
}

TEST_F(SceneManagerTest, DisconnectGpuHooksNoOpWithoutConnect)
{
    // Calling DisconnectGpuHooks without ConnectGpuHooks should be safe.
    m_Mgr->DisconnectGpuHooks();
    SUCCEED();
}

TEST_F(SceneManagerTest, DestructorDisconnectsHooks)
{
    // Constructing and destroying without connecting hooks should be safe.
    auto mgr = std::make_unique<Runtime::SceneManager>();
    mgr->GetScene().CreateEntity("X");
    mgr.reset(); // destructor calls DisconnectGpuHooks
    SUCCEED();
}

class SceneManagerGpuHooksHeadlessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RHI::ContextConfig ctxConfig{
            .AppName = "SceneManagerGpuHooksTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
        m_DummyCompute = std::make_unique<RHI::ComputePipeline>(m_Device, VK_NULL_HANDLE, VK_NULL_HANDLE);
    }

    void TearDown() override
    {
        m_DummyCompute.reset();
        if (m_Device)
            m_Device->FlushAllDeletionQueues();
        m_Device.reset();
        m_Context.reset();
    }

    [[nodiscard]] std::unique_ptr<Graphics::GPUScene> CreateGpuScene()
    {
        return std::make_unique<Graphics::GPUScene>(*m_Device, *m_DummyCompute, VK_NULL_HANDLE, 8u);
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::ComputePipeline> m_DummyCompute;
};

TEST_F(SceneManagerGpuHooksHeadlessTest, GpuDestroyHooksRemainInstanceScopedAcrossManagers)
{
    Runtime::SceneManager mgrA;
    Runtime::SceneManager mgrB;
    auto gpuSceneA = CreateGpuScene();
    auto gpuSceneB = CreateGpuScene();

    mgrA.ConnectGpuHooks(*gpuSceneA);
    mgrB.ConnectGpuHooks(*gpuSceneB);

    const uint32_t slotA = gpuSceneA->AllocateSlot();
    const uint32_t slotB = gpuSceneB->AllocateSlot();
    ASSERT_EQ(slotA, 0u);
    ASSERT_EQ(slotB, 0u);

    const entt::entity entityA = mgrA.GetScene().CreateEntity("A");
    auto& surfaceA = mgrA.GetRegistry().emplace<ECS::Surface::Component>(entityA);
    surfaceA.GpuSlot = slotA;

    mgrA.GetRegistry().destroy(entityA);

    EXPECT_EQ(gpuSceneA->AllocateSlot(), slotA)
        << "Destroying an entity in manager A must reclaim manager A's GPUScene slot.";
    EXPECT_EQ(gpuSceneB->AllocateSlot(), 1u)
        << "Destroying an entity in manager A must not reclaim manager B's GPUScene slot.";
}

TEST_F(SceneManagerGpuHooksHeadlessTest, DisconnectingOneManagerDoesNotDisableAnotherManagersHooks)
{
    Runtime::SceneManager mgrA;
    Runtime::SceneManager mgrB;
    auto gpuSceneA = CreateGpuScene();
    auto gpuSceneB = CreateGpuScene();

    mgrA.ConnectGpuHooks(*gpuSceneA);
    mgrB.ConnectGpuHooks(*gpuSceneB);
    mgrA.DisconnectGpuHooks();

    const uint32_t slotB = gpuSceneB->AllocateSlot();
    ASSERT_EQ(slotB, 0u);

    const entt::entity entityB = mgrB.GetScene().CreateEntity("B");
    auto& surfaceB = mgrB.GetRegistry().emplace<ECS::Surface::Component>(entityB);
    surfaceB.GpuSlot = slotB;

    mgrB.GetRegistry().destroy(entityB);

    EXPECT_EQ(gpuSceneB->AllocateSlot(), slotB)
        << "Disconnecting manager A must not clear manager B's hook context.";
}
