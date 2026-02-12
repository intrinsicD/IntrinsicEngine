#include <gtest/gtest.h>
#include <type_traits>

#include <entt/entity/registry.hpp>

import Runtime.SceneManager;
import ECS;
import Core;

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
