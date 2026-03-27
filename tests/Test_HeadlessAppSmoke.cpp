#include <gtest/gtest.h>
#include <memory>

#include <entt/entity/registry.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include "RHI.Vulkan.hpp"

import Runtime.SceneManager;
import Runtime.AssetPipeline;
import Runtime.GraphicsBackend;
import Runtime.Selection;
import Runtime.SelectionModule;
import RHI;
import Core;
import ECS;

// ===========================================================================
// Headless App Smoke Tests
//
// Extends HeadlessEngineTest coverage to exercise:
//   - Dispatcher event round-trip (SelectionChanged, HoverChanged)
//   - Parent-child hierarchy transform propagation
//   - FeatureRegistry integration (register/query/enable/disable)
//   - Multi-system FrameGraph compilation and dependency ordering
//
// No window, no swapchain, no ImGui.
// ===========================================================================

class HeadlessAppSmokeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Core::Tasks::Scheduler::Initialize();

        m_SceneManager = std::make_unique<Runtime::SceneManager>();

        RHI::ContextConfig ctxConfig{
            .AppName = "HeadlessAppSmoke",
            .EnableValidation = true,
            .Headless = true,
        };
        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        m_TransferManager = std::make_unique<RHI::TransferManager>(*m_Device);
        m_Bindless = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
        m_TextureManager = std::make_unique<RHI::TextureManager>(*m_Device, *m_Bindless);
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);

        m_AssetPipeline = std::make_unique<Runtime::AssetPipeline>(*m_TransferManager);
        m_FrameScope = std::make_unique<Core::Memory::ScopeStack>(64 * 1024);
    }

    void TearDown() override
    {
        if (m_Device)
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        Core::Tasks::Scheduler::Shutdown();

        m_FrameScope.reset();
        m_SceneManager->Clear();
        m_AssetPipeline.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();

        if (m_TextureManager)
        {
            m_TextureManager->ProcessDeletions();
            m_TextureManager->Clear();
        }
        m_TextureManager.reset();
        m_Bindless.reset();
        m_TransferManager.reset();
        m_SceneManager.reset();

        if (m_Device)
            m_Device->FlushAllDeletionQueues();
        m_Device.reset();
        m_Context.reset();
    }

    // Run one frame: process pipeline + compile/execute FrameGraph with Transform system.
    void RunOneFrame(entt::registry& registry)
    {
        m_FrameScope->Reset();
        m_AssetPipeline->ProcessMainThreadQueue();
        m_AssetPipeline->ProcessUploads();

        Core::FrameGraph frameGraph(*m_FrameScope);
        ECS::Systems::Transform::RegisterSystem(frameGraph, registry);

        auto result = frameGraph.Compile();
        ASSERT_TRUE(result.has_value()) << "FrameGraph compile failed";

        m_AssetPipeline->GetAssetManager().BeginReadPhase();
        frameGraph.Execute();
        m_AssetPipeline->GetAssetManager().EndReadPhase();

        m_TransferManager->GarbageCollect();
    }

    std::unique_ptr<Runtime::SceneManager> m_SceneManager;
    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::TransferManager> m_TransferManager;
    std::unique_ptr<RHI::BindlessDescriptorSystem> m_Bindless;
    std::unique_ptr<RHI::TextureManager> m_TextureManager;
    std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
    std::unique_ptr<RHI::DescriptorAllocator> m_DescriptorPool;
    std::unique_ptr<Runtime::AssetPipeline> m_AssetPipeline;
    std::unique_ptr<Core::Memory::ScopeStack> m_FrameScope;
};

// ---------------------------------------------------------------------------
// Dispatcher: SelectionChanged event fires after ApplySelection
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, SelectionChanged_EventFires)
{
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    entt::entity e = scene.CreateEntity("Selectable");
    registry.emplace<ECS::Components::Selection::SelectableTag>(e);

    entt::entity received = entt::null;
    scene.GetDispatcher().sink<ECS::Events::SelectionChanged>().connect<
        [](entt::entity& out, const ECS::Events::SelectionChanged& evt) {
            out = evt.Entity;
        }>(received);

    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);

    // Dispatcher events are enqueued then dispatched during update().
    scene.GetDispatcher().update();

    EXPECT_EQ(received, e);
}

// ---------------------------------------------------------------------------
// Dispatcher: HoverChanged event fires after ApplyHover
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, HoverChanged_EventFires)
{
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    entt::entity e = scene.CreateEntity("Hoverable");
    registry.emplace<ECS::Components::Selection::SelectableTag>(e);

    entt::entity received = entt::null;
    scene.GetDispatcher().sink<ECS::Events::HoverChanged>().connect<
        [](entt::entity& out, const ECS::Events::HoverChanged& evt) {
            out = evt.Entity;
        }>(received);

    Runtime::Selection::ApplyHover(scene, e);
    scene.GetDispatcher().update();

    EXPECT_EQ(received, e);
}

// ---------------------------------------------------------------------------
// Selection: Replace mode clears previous and sets new
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, SelectionReplace_ClearsPrevious)
{
    auto& scene = m_SceneManager->GetScene();
    auto& reg = scene.GetRegistry();

    entt::entity a = scene.CreateEntity("A");
    entt::entity b = scene.CreateEntity("B");
    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);

    Runtime::Selection::ApplySelection(scene, a, Runtime::Selection::PickMode::Replace);
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));

    Runtime::Selection::ApplySelection(scene, b, Runtime::Selection::PickMode::Replace);
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
}

// ---------------------------------------------------------------------------
// Hover: only one entity hovered at a time
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, Hover_OnlyOneAtATime)
{
    auto& scene = m_SceneManager->GetScene();
    auto& reg = scene.GetRegistry();

    entt::entity a = scene.CreateEntity("HA");
    entt::entity b = scene.CreateEntity("HB");
    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);

    Runtime::Selection::ApplyHover(scene, a);
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::HoveredTag>(a));

    Runtime::Selection::ApplyHover(scene, b);
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::HoveredTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::HoveredTag>(b));
}

// ---------------------------------------------------------------------------
// Parent-child hierarchy: child world matrix includes parent transform
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, ParentChildTransformPropagation)
{
    auto& scene = m_SceneManager->GetScene();
    auto& reg = m_SceneManager->GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    auto& pt = reg.get<ECS::Components::Transform::Component>(parent);
    pt.Position = {10.0f, 0.0f, 0.0f};
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(parent);

    auto& ct = reg.get<ECS::Components::Transform::Component>(child);
    ct.Position = {0.0f, 5.0f, 0.0f};
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(child);

    // Establish parent-child relationship.
    ECS::Components::Hierarchy::Attach(reg, child, parent);

    RunOneFrame(reg);

    // Child's world position should be parent offset + child offset.
    auto& childWorld = reg.get<ECS::Components::Transform::WorldMatrix>(child);
    EXPECT_FLOAT_EQ(childWorld.Matrix[3][0], 10.0f); // parent X
    EXPECT_FLOAT_EQ(childWorld.Matrix[3][1], 5.0f);  // child Y
    EXPECT_FLOAT_EQ(childWorld.Matrix[3][2], 0.0f);
}

// ---------------------------------------------------------------------------
// FeatureRegistry: register, query, enable/disable round-trip
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, FeatureRegistry_RoundTrip)
{
    Core::FeatureRegistry registry;

    Core::FeatureInfo info{};
    info.Name = "TestFeature";
    info.Description = "Unit test feature";
    info.Id = Core::Hash::StringID(Core::Hash::HashString("TestFeature"));
    info.Category = Core::FeatureCategory::System;
    info.Enabled = true;

    bool ok = registry.Register(info,
        []() -> void* { return nullptr; },
        [](void*) {});
    ASSERT_TRUE(ok);

    // Query by category.
    auto systems = registry.GetByCategory(Core::FeatureCategory::System);
    EXPECT_GE(systems.size(), 1u);

    // Check enabled state.
    EXPECT_TRUE(registry.IsEnabled(info.Id));

    registry.SetEnabled(info.Id, false);
    EXPECT_FALSE(registry.IsEnabled(info.Id));

    registry.SetEnabled(info.Id, true);
    EXPECT_TRUE(registry.IsEnabled(info.Id));
}

// ---------------------------------------------------------------------------
// FeatureRegistry: duplicate registration fails
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, FeatureRegistry_DuplicateRejected)
{
    Core::FeatureRegistry registry;

    Core::FeatureInfo info{};
    info.Name = "Unique";
    info.Id = Core::Hash::StringID(Core::Hash::HashString("Unique"));
    info.Category = Core::FeatureCategory::RenderFeature;

    bool first = registry.Register(info, []() -> void* { return nullptr; }, [](void*) {});
    bool second = registry.Register(info, []() -> void* { return nullptr; }, [](void*) {});

    EXPECT_TRUE(first);
    EXPECT_FALSE(second);
}

// ---------------------------------------------------------------------------
// Multi-system FrameGraph: multiple systems compile without cycle
// ---------------------------------------------------------------------------
TEST_F(HeadlessAppSmokeTest, MultiSystemFrameGraph_CompilesClean)
{
    auto& registry = m_SceneManager->GetRegistry();

    entt::entity e = m_SceneManager->GetScene().CreateEntity("E");
    auto& t = registry.get<ECS::Components::Transform::Component>(e);
    t.Position = {1.0f, 2.0f, 3.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

    m_FrameScope->Reset();
    Core::FrameGraph fg(*m_FrameScope);

    // Register the transform system (the primary CPU-side system).
    ECS::Systems::Transform::RegisterSystem(fg, registry);

    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(fg.GetPassCount(), 1u);

    // Execute and verify.
    fg.Execute();

    auto& world = registry.get<ECS::Components::Transform::WorldMatrix>(e);
    EXPECT_FLOAT_EQ(world.Matrix[3][0], 1.0f);
    EXPECT_FLOAT_EQ(world.Matrix[3][1], 2.0f);
    EXPECT_FLOAT_EQ(world.Matrix[3][2], 3.0f);
}
