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
import RHI;
import Core;
import ECS;

// ===========================================================================
// Headless Engine Smoke Test
//
// Mirrors Engine's subsystem construction order and frame loop in headless
// mode (no window, no swapchain, no ImGui).  Verifies that the CPU-side
// subsystem constellation — SceneManager, AssetPipeline, FrameGraph, and
// ECS Transform system — works end-to-end for a single frame.
//
// The full GPU rendering pipeline (RenderOrchestrator, RenderSystem) is
// intentionally excluded: it requires a swapchain/surface.  Individual
// headless tests for GraphicsBackend and RenderOrchestrator cover those.
// ===========================================================================

class HeadlessEngineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // --- Mirror Engine::Engine() construction order (headless) ---

        // 0. Core singletons (Engine does this first)
        Core::Tasks::Scheduler::Initialize();

        // 1. SceneManager (ECS scene, entity lifecycle)
        m_SceneManager = std::make_unique<Runtime::SceneManager>();

        // 2. Headless Vulkan (replaces Window + GraphicsBackend surface path)
        RHI::ContextConfig ctxConfig{
            .AppName = "HeadlessEngineTest",
            .EnableValidation = true,
            .Headless = true,
        };
        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        // 3. GPU infrastructure that GraphicsBackend normally owns
        m_TransferManager = std::make_unique<RHI::TransferManager>(*m_Device);
        m_Bindless = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
        m_TextureSystem = std::make_unique<RHI::TextureSystem>(*m_Device, *m_Bindless);
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);

        // 4. AssetPipeline (AssetManager, pending transfers, main-thread queue)
        m_AssetPipeline = std::make_unique<Runtime::AssetPipeline>(*m_TransferManager);

        // 5. Per-frame state (normally owned by RenderOrchestrator)
        m_FrameScope = std::make_unique<Core::Memory::ScopeStack>(64 * 1024);
    }

    void TearDown() override
    {
        // --- Mirror Engine::~Engine() destruction order ---

        // Wait for GPU and background tasks
        if (m_Device)
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        Core::Tasks::Scheduler::Shutdown();

        // Frame state
        m_FrameScope.reset();

        // SceneManager (clear entities before GPU teardown)
        m_SceneManager->Clear();

        // AssetPipeline
        m_AssetPipeline.reset();

        // Descriptors
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();

        // Texture + Bindless
        if (m_TextureSystem)
        {
            m_TextureSystem->ProcessDeletions();
            m_TextureSystem->Clear();
        }
        m_TextureSystem.reset();
        m_Bindless.reset();

        // Transfer
        m_TransferManager.reset();

        // SceneManager
        m_SceneManager.reset();

        // Device + Context
        if (m_Device)
            m_Device->FlushAllDeletionQueues();
        m_Device.reset();
        m_Context.reset();
    }

    // --- Subsystems ---
    std::unique_ptr<Runtime::SceneManager> m_SceneManager;

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::TransferManager> m_TransferManager;
    std::unique_ptr<RHI::BindlessDescriptorSystem> m_Bindless;
    std::unique_ptr<RHI::TextureSystem> m_TextureSystem;
    std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
    std::unique_ptr<RHI::DescriptorAllocator> m_DescriptorPool;

    std::unique_ptr<Runtime::AssetPipeline> m_AssetPipeline;
    std::unique_ptr<Core::Memory::ScopeStack> m_FrameScope;
};

// ---------------------------------------------------------------------------
// Core smoke test: one frame cycle with all minimal subsystems
// ---------------------------------------------------------------------------
TEST_F(HeadlessEngineTest, OneFrameCycle)
{
    // --- Setup scene (like Sandbox::OnStart) ---
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    entt::entity e1 = scene.CreateEntity("Entity_A");
    entt::entity e2 = scene.CreateEntity("Entity_B");

    // Set positions and mark dirty
    auto& t1 = registry.get<ECS::Components::Transform::Component>(e1);
    t1.Position = {10.0f, 0.0f, 0.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e1);

    auto& t2 = registry.get<ECS::Components::Transform::Component>(e2);
    t2.Position = {0.0f, 5.0f, -3.0f};
    t2.Scale = {2.0f, 2.0f, 2.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e2);

    // --- One frame cycle (mirrors Engine::Run loop body) ---

    // 1. Reset per-frame state
    m_FrameScope->Reset();

    // 2. Process main-thread queue (AssetPipeline)
    m_AssetPipeline->ProcessMainThreadQueue();

    // 3. Process uploads (AssetPipeline)
    m_AssetPipeline->ProcessUploads();

    // 4. FrameGraph: register, compile, execute
    Core::FrameGraph frameGraph(*m_FrameScope);

    ECS::Systems::Transform::RegisterSystem(frameGraph, registry);

    auto compileResult = frameGraph.Compile();
    ASSERT_TRUE(compileResult.has_value()) << "FrameGraph compile failed";
    EXPECT_GE(frameGraph.GetPassCount(), 1u);

    m_AssetPipeline->GetAssetManager().BeginReadPhase();
    frameGraph.Execute();
    m_AssetPipeline->GetAssetManager().EndReadPhase();

    // 5. Transfer GC
    m_TransferManager->GarbageCollect();

    // --- Verify frame results ---

    // WorldMatrix should be updated from the transform data
    auto& world1 = registry.get<ECS::Components::Transform::WorldMatrix>(e1);
    EXPECT_FLOAT_EQ(world1.Matrix[3][0], 10.0f);
    EXPECT_FLOAT_EQ(world1.Matrix[3][1], 0.0f);
    EXPECT_FLOAT_EQ(world1.Matrix[3][2], 0.0f);

    auto& world2 = registry.get<ECS::Components::Transform::WorldMatrix>(e2);
    EXPECT_FLOAT_EQ(world2.Matrix[3][0], 0.0f);
    EXPECT_FLOAT_EQ(world2.Matrix[3][1], 5.0f);
    EXPECT_FLOAT_EQ(world2.Matrix[3][2], -3.0f);
    // Scale should be reflected in the matrix diagonal
    EXPECT_FLOAT_EQ(world2.Matrix[0][0], 2.0f);
    EXPECT_FLOAT_EQ(world2.Matrix[1][1], 2.0f);
    EXPECT_FLOAT_EQ(world2.Matrix[2][2], 2.0f);

    // Dirty tags should be cleared after the frame
    EXPECT_FALSE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e1));
    EXPECT_FALSE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e2));
}

// ---------------------------------------------------------------------------
// Multi-frame: verify reset-and-rerun works correctly
// ---------------------------------------------------------------------------
TEST_F(HeadlessEngineTest, MultiFrameCycle)
{
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    entt::entity e = scene.CreateEntity("MovingEntity");

    for (int frame = 0; frame < 5; ++frame)
    {
        // Reset per-frame state (like Engine::Run top-of-loop)
        m_FrameScope->Reset();

        // Move entity each frame
        auto& t = registry.get<ECS::Components::Transform::Component>(e);
        t.Position.x = static_cast<float>(frame) * 3.0f;
        registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

        // Process pipeline
        m_AssetPipeline->ProcessMainThreadQueue();
        m_AssetPipeline->ProcessUploads();

        // FrameGraph cycle
        Core::FrameGraph frameGraph(*m_FrameScope);
        ECS::Systems::Transform::RegisterSystem(frameGraph, registry);

        auto result = frameGraph.Compile();
        ASSERT_TRUE(result.has_value()) << "Frame " << frame << " compile failed";

        m_AssetPipeline->GetAssetManager().BeginReadPhase();
        frameGraph.Execute();
        m_AssetPipeline->GetAssetManager().EndReadPhase();

        m_TransferManager->GarbageCollect();

        // Verify
        auto& world = registry.get<ECS::Components::Transform::WorldMatrix>(e);
        EXPECT_FLOAT_EQ(world.Matrix[3][0], static_cast<float>(frame) * 3.0f)
            << "Frame " << frame;
        EXPECT_FALSE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e))
            << "Frame " << frame;
    }
}

// ---------------------------------------------------------------------------
// Cross-subsystem: AssetPipeline main-thread queue integrates with frame loop
// ---------------------------------------------------------------------------
TEST_F(HeadlessEngineTest, AssetPipelineMainThreadQueueInFrameLoop)
{
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    entt::entity e = scene.CreateEntity("AsyncSpawned");
    bool taskExecuted = false;

    // Simulate what a worker thread would do: queue a main-thread task
    // that modifies an entity's transform (like LoadDroppedAsset does).
    m_AssetPipeline->RunOnMainThread([&]()
    {
        auto& t = registry.get<ECS::Components::Transform::Component>(e);
        t.Position = {42.0f, 0.0f, 0.0f};
        registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);
        taskExecuted = true;
    });

    EXPECT_FALSE(taskExecuted);

    // --- Frame 1: process queue, then run FrameGraph ---
    m_FrameScope->Reset();

    // This should execute our queued task
    m_AssetPipeline->ProcessMainThreadQueue();
    EXPECT_TRUE(taskExecuted);

    // Now the transform system should pick up the dirty entity
    Core::FrameGraph frameGraph(*m_FrameScope);
    ECS::Systems::Transform::RegisterSystem(frameGraph, registry);

    auto result = frameGraph.Compile();
    ASSERT_TRUE(result.has_value());

    m_AssetPipeline->GetAssetManager().BeginReadPhase();
    frameGraph.Execute();
    m_AssetPipeline->GetAssetManager().EndReadPhase();

    // Verify the async-queued transform made it through the full pipeline
    auto& world = registry.get<ECS::Components::Transform::WorldMatrix>(e);
    EXPECT_FLOAT_EQ(world.Matrix[3][0], 42.0f);
}

// ---------------------------------------------------------------------------
// Entity lifecycle: create and destroy within a frame
// ---------------------------------------------------------------------------
TEST_F(HeadlessEngineTest, EntityLifecycleDuringFrame)
{
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    // Create several entities
    entt::entity e1 = scene.CreateEntity("Permanent");
    entt::entity e2 = scene.CreateEntity("Temporary");

    auto& t1 = registry.get<ECS::Components::Transform::Component>(e1);
    t1.Position = {1.0f, 2.0f, 3.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e1);

    auto& t2 = registry.get<ECS::Components::Transform::Component>(e2);
    t2.Position = {4.0f, 5.0f, 6.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e2);

    // Run one frame
    m_FrameScope->Reset();
    Core::FrameGraph frameGraph(*m_FrameScope);
    ECS::Systems::Transform::RegisterSystem(frameGraph, registry);
    auto result = frameGraph.Compile();
    ASSERT_TRUE(result.has_value());
    frameGraph.Execute();

    // Destroy temporary entity
    registry.destroy(e2);
    EXPECT_EQ(scene.Size(), 1u);

    // Second frame: only permanent entity remains
    m_FrameScope->Reset();
    Core::FrameGraph frameGraph2(*m_FrameScope);

    t1.Position.x = 99.0f;
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e1);

    ECS::Systems::Transform::RegisterSystem(frameGraph2, registry);
    auto result2 = frameGraph2.Compile();
    ASSERT_TRUE(result2.has_value());
    frameGraph2.Execute();

    auto& world1 = registry.get<ECS::Components::Transform::WorldMatrix>(e1);
    EXPECT_FLOAT_EQ(world1.Matrix[3][0], 99.0f);
}

// ---------------------------------------------------------------------------
// Subsystem construction order: verify all subsystems are alive and wired
// ---------------------------------------------------------------------------
TEST_F(HeadlessEngineTest, SubsystemsInitializedCorrectly)
{
    // SceneManager
    EXPECT_EQ(m_SceneManager->GetScene().Size(), 0u);

    // Vulkan device
    EXPECT_NE(m_Device->GetLogicalDevice(), VK_NULL_HANDLE);

    // Descriptor plumbing
    EXPECT_TRUE(m_DescriptorLayout->IsValid());
    EXPECT_TRUE(m_DescriptorPool->IsValid());

    // AssetPipeline
    auto handle = m_AssetPipeline->GetAssetManager().Create("test", std::make_unique<int>(1));
    EXPECT_TRUE(handle.IsValid());
}

// ---------------------------------------------------------------------------
// Scene clear and reuse across frames
// ---------------------------------------------------------------------------
TEST_F(HeadlessEngineTest, SceneClearAndReuse)
{
    auto& scene = m_SceneManager->GetScene();
    auto& registry = m_SceneManager->GetRegistry();

    // Populate scene
    scene.CreateEntity("A");
    scene.CreateEntity("B");
    scene.CreateEntity("C");
    EXPECT_EQ(scene.Size(), 3u);

    // Clear (like starting a new scene)
    m_SceneManager->Clear();
    EXPECT_EQ(scene.Size(), 0u);

    // Repopulate and run a frame
    entt::entity e = scene.CreateEntity("Fresh");
    auto& t = registry.get<ECS::Components::Transform::Component>(e);
    t.Position = {7.0f, 8.0f, 9.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

    m_FrameScope->Reset();
    Core::FrameGraph frameGraph(*m_FrameScope);
    ECS::Systems::Transform::RegisterSystem(frameGraph, registry);
    auto result = frameGraph.Compile();
    ASSERT_TRUE(result.has_value());
    frameGraph.Execute();

    auto& world = registry.get<ECS::Components::Transform::WorldMatrix>(e);
    EXPECT_FLOAT_EQ(world.Matrix[3][0], 7.0f);
    EXPECT_FLOAT_EQ(world.Matrix[3][1], 8.0f);
    EXPECT_FLOAT_EQ(world.Matrix[3][2], 9.0f);
}
