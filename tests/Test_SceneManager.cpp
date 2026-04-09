#include <gtest/gtest.h>
#include <type_traits>
#include <memory>
#include <glm/glm.hpp>

#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"

import Runtime.SceneManager;
import Asset.Manager;
import ECS;
import Core;
import RHI;
import Graphics;
import Geometry;

import Graphics.LifecycleUtils;

using namespace Graphics::LifecycleUtils;

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

TEST_F(SceneManagerTest, CommitFixedTickAdvancesReadonlySnapshotGeneration)
{
    const Runtime::WorldSnapshot initialSnapshot = m_Mgr->CreateReadonlySnapshot();
    ASSERT_TRUE(initialSnapshot.IsValid());
    EXPECT_EQ(initialSnapshot.Scene, &m_Mgr->GetScene());
    EXPECT_EQ(initialSnapshot.Registry, &m_Mgr->GetRegistry());
    EXPECT_EQ(initialSnapshot.CommittedTick, 0u);
    EXPECT_EQ(m_Mgr->GetCommittedTick(), 0u);

    m_Mgr->CommitFixedTick();
    m_Mgr->CommitFixedTick();

    const Runtime::WorldSnapshot committedSnapshot = m_Mgr->CreateReadonlySnapshot();
    ASSERT_TRUE(committedSnapshot.IsValid());
    EXPECT_EQ(committedSnapshot.Scene, &m_Mgr->GetScene());
    EXPECT_EQ(committedSnapshot.Registry, &m_Mgr->GetRegistry());
    EXPECT_EQ(committedSnapshot.CommittedTick, 2u);
    EXPECT_EQ(m_Mgr->GetCommittedTick(), 2u);
    EXPECT_EQ(initialSnapshot.CommittedTick, 0u)
        << "Snapshots capture the committed-tick generation present at extraction time.";
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
    m_Mgr->CommitFixedTick();
    m_Mgr->GetScene().CreateEntity("A");
    m_Mgr->GetScene().CreateEntity("B");
    m_Mgr->GetScene().CreateEntity("C");
    EXPECT_EQ(m_Mgr->GetScene().Size(), 3u);

    m_Mgr->Clear();
    EXPECT_EQ(m_Mgr->GetScene().Size(), 0u);
    EXPECT_EQ(m_Mgr->GetCommittedTick(), 0u);
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

TEST_F(SceneManagerTest, SpawnModelAttachesMeshDataFromCollisionSourceMesh)
{
    Core::Assets::AssetManager assetManager;
    Graphics::GeometryPool geometryPool;
    m_Mgr->SetGeometryStorage(&geometryPool);

    auto model = std::make_unique<Graphics::Model>(geometryPool, std::shared_ptr<RHI::VulkanDevice>{});
    auto segment = std::make_shared<Graphics::MeshSegment>();
    segment->Name = "Duck";

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = mesh->AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh->AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh->AddVertex({0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh->AddTriangle(v0, v1, v2).has_value());

    collision->SourceMesh = mesh;
    collision->LocalAABB = Geometry::AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    segment->CollisionGeometry = collision;
    model->Meshes.push_back(segment);

    const auto modelHandle = assetManager.Create("test::duck", std::move(model));
    ASSERT_TRUE(modelHandle.IsValid());

    const entt::entity root = m_Mgr->SpawnModel(assetManager, modelHandle, Core::Assets::AssetHandle{},
                                                glm::vec3(0.0f), glm::vec3(1.0f));
    ASSERT_TRUE(root != entt::null);

    auto& reg = m_Mgr->GetRegistry();
    ASSERT_TRUE(reg.all_of<ECS::Surface::Component>(root));
    ASSERT_TRUE(reg.all_of<ECS::MeshCollider::Component>(root));
    ASSERT_TRUE(reg.all_of<ECS::Mesh::Data>(root));
    EXPECT_EQ(reg.get<ECS::Mesh::Data>(root).MeshRef, mesh);
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

    mgrA.ConnectGpuHooks(*gpuSceneA, *m_Device);
    mgrB.ConnectGpuHooks(*gpuSceneB, *m_Device);

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

    mgrA.ConnectGpuHooks(*gpuSceneA, *m_Device);
    mgrB.ConnectGpuHooks(*gpuSceneB, *m_Device);
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

TEST_F(SceneManagerGpuHooksHeadlessTest, ReleaseGpuSlotReclaimsRawSlotAndInvalidatesSentinel)
{
    auto gpuScene = CreateGpuScene();

    uint32_t slot = gpuScene->AllocateSlot();
    ASSERT_EQ(slot, 0u);

    ReleaseGpuSlot(*gpuScene, slot);

    EXPECT_EQ(slot, ECS::kInvalidGpuSlot);
    EXPECT_EQ(gpuScene->AllocateSlot(), 0u)
        << "Released slots must return to the GPUScene free list immediately.";
}

TEST_F(SceneManagerGpuHooksHeadlessTest, ReleaseGpuSlotReclaimsComponentOwnedSlot)
{
    auto gpuScene = CreateGpuScene();

    ECS::Surface::Component surface{};
    surface.GpuSlot = gpuScene->AllocateSlot();
    ASSERT_EQ(surface.GpuSlot, 0u);

    ReleaseGpuSlot(*gpuScene, surface);

    EXPECT_EQ(surface.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_EQ(gpuScene->AllocateSlot(), 0u)
        << "The shared reclaim helper must work for ECS components that own GpuSlot.";
}
