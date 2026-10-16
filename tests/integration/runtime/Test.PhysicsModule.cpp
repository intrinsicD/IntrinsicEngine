#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PhysicsModule;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Collider = Extrinsic::ECS::Components::Collider;
namespace RigidBody = Extrinsic::ECS::Components::RigidBody;
namespace Transform = Extrinsic::ECS::Components::Transform;
namespace Components = Extrinsic::ECS::Components;
namespace Runtime = Extrinsic::Runtime;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;

namespace
{
    [[nodiscard]] Components::StableId Id(const std::uint64_t low)
    {
        return Components::StableId{0x5048'5953'4943'5304ull, low};
    }

    [[nodiscard]] EntityHandle CreateAuthoredBody(
        Registry& scene,
        const Components::StableId stableId,
        RigidBody::Component rigidBody,
        const glm::vec3 position = glm::vec3{0.0f})
    {
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<Components::StableId>(entity, stableId);
        Transform::Component transform{};
        transform.Position = position;
        raw.emplace<Transform::Component>(entity, transform);
        raw.emplace<Collider::Component>(
            entity,
            Collider::Component{{Collider::MakeSphere(0.5f)}, true});
        raw.emplace<RigidBody::Component>(entity, rigidBody);
        return entity;
    }

    [[nodiscard]] Runtime::PhysicsModuleConfig EnabledConfig()
    {
        Runtime::PhysicsModuleConfig config{};
        config.Enabled = true;
        config.FixedDeltaSeconds = 0.01f;
        config.MaxAccumulatedSeconds = 0.1f;
        config.MaxStepsPerFrame = 8u;
        config.Gravity = glm::vec3{0.0f};
        return config;
    }

    class PhysicsModuleHarness final
    {
    public:
        PhysicsModuleHarness()
        {
            m_DefaultWorld = m_Worlds.CreateWorld("physics-module-test");
            m_Services.BeginRegistration();
            Runtime::EngineSetup setup{
                m_Commands,
                m_Events,
                m_Jobs,
                m_Worlds,
                m_Services,
                [this](const Runtime::FramePhase phase,
                       Runtime::RuntimeFrameHook hook)
                {
                    if (phase == Runtime::FramePhase::Simulation)
                        m_SimulationHook = std::move(hook);
                },
            };
            EXPECT_TRUE(m_Module.OnRegister(setup).has_value());
            EXPECT_TRUE(static_cast<bool>(m_SimulationHook));
            m_Registered = true;
        }

        ~PhysicsModuleHarness()
        {
            Shutdown();
        }

        PhysicsModuleHarness(const PhysicsModuleHarness&) = delete;
        PhysicsModuleHarness& operator=(const PhysicsModuleHarness&) = delete;

        [[nodiscard]] Runtime::PhysicsModule& Module() noexcept
        {
            return m_Module;
        }

        [[nodiscard]] Runtime::WorldRegistry& Worlds() noexcept
        {
            return m_Worlds;
        }

        [[nodiscard]] Runtime::KernelEventBus& Events() noexcept
        {
            return m_Events;
        }

        [[nodiscard]] Runtime::JobService& Jobs() noexcept
        {
            return m_Jobs;
        }

        [[nodiscard]] Runtime::WorldHandle DefaultWorld() const noexcept
        {
            return m_DefaultWorld;
        }

        [[nodiscard]] Registry& ActiveScene()
        {
            return *m_Worlds.Get(m_Worlds.ActiveWorld());
        }

        void RunSimulation(const double frameDeltaSeconds)
        {
            ASSERT_TRUE(static_cast<bool>(m_SimulationHook));
            Registry* const scene = m_Worlds.Get(m_Worlds.ActiveWorld());
            ASSERT_NE(scene, nullptr);
            Runtime::RuntimeFrameHookContext context{
                .ActiveWorld = *scene,
                .ActiveWorldHandle = m_Worlds.ActiveWorld(),
                .Commands = m_Commands,
                .Events = m_Events,
                .Jobs = m_Jobs,
                .Worlds = m_Worlds,
                .Services = m_Services,
                .EditorCapture = m_EditorCapture,
                .Pacing = m_Pacing,
                .FrameDeltaSeconds = frameDeltaSeconds,
            };
            m_SimulationHook(context);
        }

        void Shutdown()
        {
            if (!m_Registered)
                return;
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = m_Commands,
                .Events = m_Events,
                .Jobs = m_Jobs,
                .Worlds = m_Worlds,
                .Services = m_Services,
            };
            m_Module.OnShutdown(context);
            m_Registered = false;
        }

    private:
        Runtime::CommandBus m_Commands{};
        Runtime::KernelEventBus m_Events{};
        Runtime::JobService m_Jobs{};
        Runtime::WorldRegistry m_Worlds{};
        Runtime::ServiceRegistry m_Services{};
        Runtime::PhysicsModule m_Module{};
        Runtime::RuntimeFrameHook m_SimulationHook{};
        Runtime::EditorInputCaptureSnapshot m_EditorCapture{};
        Runtime::RuntimeFramePacingDiagnostics m_Pacing{};
        Runtime::WorldHandle m_DefaultWorld{};
        bool m_Registered{false};
    };

    struct RealEngineProbe
    {
        EntityHandle Dynamic{Extrinsic::ECS::InvalidEntityHandle};
        EntityHandle Static{Extrinsic::ECS::InvalidEntityHandle};
        EntityHandle Kinematic{Extrinsic::ECS::InvalidEntityHandle};
        std::uint32_t Frames{0u};
        float DynamicY{10.0f};
        float StaticY{20.0f};
        float KinematicY{30.0f};
        bool DynamicDirty{false};
        bool FrameLimitExceeded{false};
    };

    class RealEnginePhysicsApplication final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit RealEnginePhysicsApplication(RealEngineProbe& probe)
            : m_Probe(probe)
        {
        }

        void Resolve() override
        {
            Registry& scene =
                *Kernel().Worlds().Get(Kernel().ActiveWorld());
            m_Probe.Dynamic = CreateAuthoredBody(
                scene,
                Id(100u),
                RigidBody::MakeDynamic(1.0f),
                glm::vec3{0.0f, 10.0f, 0.0f});
            m_Probe.Static = CreateAuthoredBody(
                scene,
                Id(101u),
                RigidBody::MakeStatic(),
                glm::vec3{0.0f, 20.0f, 0.0f});
            RigidBody::Component kinematic = RigidBody::MakeKinematic();
            kinematic.LinearVelocity = glm::vec3{0.0f, 10.0f, 0.0f};
            m_Probe.Kinematic = CreateAuthoredBody(
                scene,
                Id(102u),
                kinematic,
                glm::vec3{0.0f, 30.0f, 0.0f});
        }

        void Frame(double, double) override
        {
            ++m_Probe.Frames;
            if (m_Probe.Frames == 1u)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
                return;
            }

            Registry& scene =
                *Kernel().Worlds().Get(Kernel().ActiveWorld());
            auto& raw = scene.Raw();
            m_Probe.DynamicY =
                raw.get<Transform::Component>(m_Probe.Dynamic).Position.y;
            m_Probe.StaticY =
                raw.get<Transform::Component>(m_Probe.Static).Position.y;
            m_Probe.KinematicY =
                raw.get<Transform::Component>(m_Probe.Kinematic).Position.y;
            m_Probe.DynamicDirty =
                raw.all_of<Transform::IsDirtyTag>(m_Probe.Dynamic) &&
                raw.all_of<Transform::WorldUpdatedTag>(m_Probe.Dynamic);
            Kernel().RequestExit();
            if (m_Probe.Frames > 8u)
                m_Probe.FrameLimitExceeded = true;
        }
    private:
        RealEngineProbe& m_Probe;
    };

    [[nodiscard]] Runtime::RuntimeEngineConfigSectionRegistry
    CreatePhysicsConfigRegistry(
        Runtime::PhysicsModuleConfigChangedCallback onChanged = {})
    {
        Runtime::RuntimeEngineConfigSectionRegistry registry{};
        EXPECT_TRUE(registry.Register(
            Runtime::MakePhysicsModuleConfigSectionRegistration(
                std::move(onChanged))));
        return registry;
    }
}

TEST(PhysicsModule, DisabledOwnsNoWorldAndStepBudgetIsBounded)
{
    PhysicsModuleHarness harness{};
    harness.RunSimulation(0.5);
    auto snapshot = harness.Module().GetSnapshot();
    EXPECT_FALSE(snapshot.Diagnostics.Enabled);
    EXPECT_EQ(snapshot.Diagnostics.OwnedPhysicsWorlds, 0u);

    Runtime::PhysicsModuleConfig invalid = EnabledConfig();
    invalid.FixedDeltaSeconds = 0.0f;
    EXPECT_FALSE(harness.Module().ApplyConfig(invalid).has_value());
    EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.ConfigRejects, 1u);

    Runtime::PhysicsModuleConfig enabled = EnabledConfig();
    enabled.FixedDeltaSeconds = 0.1f;
    enabled.MaxAccumulatedSeconds = 0.5f;
    enabled.MaxStepsPerFrame = 2u;
    ASSERT_TRUE(harness.Module().ApplyConfig(enabled).has_value());
    harness.RunSimulation(0.5);

    snapshot = harness.Module().GetSnapshot();
    EXPECT_TRUE(snapshot.Diagnostics.Enabled);
    EXPECT_EQ(snapshot.Diagnostics.OwnedPhysicsWorlds, 1u);
    EXPECT_EQ(snapshot.Diagnostics.FixedSteps, 2u);
    EXPECT_EQ(snapshot.Diagnostics.StepBudgetExhaustions, 1u);
    EXPECT_NEAR(snapshot.Diagnostics.ActiveWorldAccumulatorSeconds, 0.3f, 1.0e-5f);

    enabled.Enabled = false;
    ASSERT_TRUE(harness.Module().ApplyConfig(enabled).has_value());
    snapshot = harness.Module().GetSnapshot();
    EXPECT_FALSE(snapshot.Diagnostics.Enabled);
    EXPECT_EQ(snapshot.Diagnostics.OwnedPhysicsWorlds, 0u);
    EXPECT_EQ(snapshot.Diagnostics.SidecarCount, 0u);
}

TEST(PhysicsModule, AuthoringFingerprintReusesBodiesAndTracksUpdatesAndRemoval)
{
    PhysicsModuleHarness harness{};
    Runtime::PhysicsModuleConfig config = EnabledConfig();
    config.FixedDeltaSeconds = 1.0f;
    config.MaxAccumulatedSeconds = 1.0f;
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());

    Registry& scene = harness.ActiveScene();
    const EntityHandle entity = CreateAuthoredBody(
        scene,
        Id(1u),
        RigidBody::MakeDynamic(1.0f),
        glm::vec3{0.0f, 3.0f, 0.0f});
    harness.RunSimulation(0.0);
    auto snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.ActiveWorldBodyCount, 1u);
    EXPECT_EQ(snapshot.Diagnostics.BodiesCreated, 1u);
    EXPECT_EQ(snapshot.Diagnostics.BodiesUpdated, 0u);

    harness.RunSimulation(0.0);
    snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.BodiesCreated, 1u);
    EXPECT_EQ(snapshot.Diagnostics.BodiesUpdated, 0u);

    scene.Raw().get<RigidBody::Component>(entity).Mass.Mass = 2.0f;
    harness.RunSimulation(0.0);
    snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.BodiesCreated, 1u);
    EXPECT_EQ(snapshot.Diagnostics.BodiesUpdated, 1u);

    scene.Destroy(entity);
    harness.RunSimulation(0.0);
    snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.ActiveWorldBodyCount, 0u);
    EXPECT_EQ(snapshot.Diagnostics.SidecarCount, 0u);
    EXPECT_EQ(snapshot.Diagnostics.BodiesRemoved, 1u);

    auto& raw = scene.Raw();
    const EntityHandle missingCollider = scene.Create();
    raw.emplace<Components::StableId>(missingCollider, Id(2u));
    raw.emplace<Transform::Component>(missingCollider);
    raw.emplace<RigidBody::Component>(
        missingCollider,
        RigidBody::MakeDynamic(1.0f));
    const EntityHandle missingStable = scene.Create();
    raw.emplace<Transform::Component>(missingStable);
    raw.emplace<Collider::Component>(
        missingStable,
        Collider::Component{{Collider::MakeSphere(0.5f)}, true});
    harness.RunSimulation(0.0);
    snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.MissingAuthoringComponents, 1u);
    EXPECT_EQ(snapshot.Diagnostics.MissingStableIds, 1u);
    EXPECT_EQ(snapshot.Diagnostics.ActiveWorldBodyCount, 0u);
}

TEST(PhysicsModule, SolverWritesBackOnlyDynamicBodiesWithoutResettingAuthoring)
{
    PhysicsModuleHarness harness{};
    Runtime::PhysicsModuleConfig config = EnabledConfig();
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
    Registry& scene = harness.ActiveScene();

    const EntityHandle dynamicA = CreateAuthoredBody(
        scene,
        Id(10u),
        RigidBody::MakeDynamic(1.0f),
        glm::vec3{-0.25f, 0.0f, 0.0f});
    const EntityHandle dynamicB = CreateAuthoredBody(
        scene,
        Id(11u),
        RigidBody::MakeDynamic(1.0f),
        glm::vec3{0.25f, 0.0f, 0.0f});
    const EntityHandle staticEntity = CreateAuthoredBody(
        scene,
        Id(12u),
        RigidBody::MakeStatic(),
        glm::vec3{0.0f, 20.0f, 0.0f});
    RigidBody::Component kinematic = RigidBody::MakeKinematic();
    kinematic.LinearVelocity = glm::vec3{0.0f, 5.0f, 0.0f};
    const EntityHandle kinematicEntity = CreateAuthoredBody(
        scene,
        Id(13u),
        kinematic,
        glm::vec3{0.0f, 30.0f, 0.0f});

    harness.RunSimulation(0.01);
    auto snapshot = harness.Module().GetSnapshot();
    auto& raw = scene.Raw();
    EXPECT_GT(snapshot.Diagnostics.LastContactsSolved, 0u);
    EXPECT_LT(snapshot.Diagnostics.LastSyncOrder,
              snapshot.Diagnostics.LastStepOrder);
    EXPECT_LT(snapshot.Diagnostics.LastStepOrder,
              snapshot.Diagnostics.LastWritebackOrder);
    EXPECT_GE(snapshot.Diagnostics.DynamicWritebacks, 2u);
    EXPECT_GE(snapshot.Diagnostics.StaticWritebacksSkipped, 1u);
    EXPECT_GE(snapshot.Diagnostics.KinematicWritebacksSkipped, 1u);
    EXPECT_LT(raw.get<Transform::Component>(dynamicA).Position.x, -0.25f);
    EXPECT_GT(raw.get<Transform::Component>(dynamicB).Position.x, 0.25f);
    EXPECT_TRUE(raw.all_of<Transform::IsDirtyTag>(dynamicA));
    EXPECT_TRUE(raw.all_of<Transform::WorldUpdatedTag>(dynamicA));
    EXPECT_FLOAT_EQ(
        raw.get<Transform::Component>(staticEntity).Position.y,
        20.0f);
    EXPECT_FLOAT_EQ(
        raw.get<Transform::Component>(kinematicEntity).Position.y,
        30.0f);

    harness.RunSimulation(0.01);
    snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.BodiesUpdated, 0u);
    EXPECT_EQ(snapshot.Diagnostics.BodiesCreated, 4u);
}

TEST(PhysicsModule, PerWorldStateIsRemovedOnDestroyAndShutdown)
{
    PhysicsModuleHarness harness{};
    ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
    (void)CreateAuthoredBody(
        harness.ActiveScene(),
        Id(20u),
        RigidBody::MakeDynamic(1.0f));
    harness.RunSimulation(0.0);

    const Runtime::WorldHandle second =
        harness.Worlds().CreateWorld("physics-second-world");
    Registry* const secondScene = harness.Worlds().Get(second);
    ASSERT_NE(secondScene, nullptr);
    (void)CreateAuthoredBody(
        *secondScene,
        Id(21u),
        RigidBody::MakeDynamic(1.0f));
    ASSERT_TRUE(harness.Worlds().RequestSetActiveWorld(second).has_value());
    (void)harness.Worlds().ApplyMaintenance(
        harness.Events(),
        harness.Jobs());
    (void)harness.Events().Pump();
    harness.RunSimulation(0.0);
    EXPECT_EQ(
        harness.Module().GetSnapshot().Diagnostics.OwnedPhysicsWorlds,
        2u);

    ASSERT_TRUE(harness.Worlds()
                    .RequestSetActiveWorld(harness.DefaultWorld())
                    .has_value());
    (void)harness.Worlds().ApplyMaintenance(
        harness.Events(),
        harness.Jobs());
    (void)harness.Events().Pump();
    ASSERT_TRUE(harness.Worlds().RequestDestroyWorld(second).has_value());
    (void)harness.Worlds().ApplyMaintenance(
        harness.Events(),
        harness.Jobs());
    (void)harness.Events().Pump();

    auto snapshot = harness.Module().GetSnapshot();
    EXPECT_EQ(snapshot.Diagnostics.OwnedPhysicsWorlds, 1u);
    EXPECT_EQ(snapshot.Diagnostics.WorldStatesRemoved, 1u);
    EXPECT_EQ(snapshot.Diagnostics.SidecarCount, 1u);

    harness.Shutdown();
    snapshot = harness.Module().GetSnapshot();
    EXPECT_FALSE(snapshot.Diagnostics.Enabled);
    EXPECT_EQ(snapshot.Diagnostics.OwnedPhysicsWorlds, 0u);
    EXPECT_EQ(snapshot.Diagnostics.SidecarCount, 0u);
    EXPECT_EQ(snapshot.Diagnostics.WorldStatesRemoved, 2u);
}

TEST(PhysicsModule, NullEngineRunAdvancesDynamicAndPreservesStaticKinematicEcsPose)
{
    auto physicsModule = std::make_unique<Runtime::PhysicsModule>();
    Runtime::PhysicsModule* const physics = physicsModule.get();
    std::uint32_t configChanges = 0u;
    auto registry = CreatePhysicsConfigRegistry(
        [physics, &configChanges](const Runtime::PhysicsModuleConfig& value)
        {
            ++configChanges;
            (void)physics->ApplyConfig(value);
        });
    CoreConfig::EngineConfig config =
        Runtime::CreateReferenceEngineConfig(registry);
    config.Simulation.WorkerThreadCount = 1u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = CoreConfig::WindowBackend::Null;
    config.Render.EnablePromotedVulkanDevice = false;
    config.Render.DefaultRecipeConfigPath.clear();

    Runtime::PhysicsModuleConfig physicsConfig = EnabledConfig();
    physicsConfig.FixedDeltaSeconds = 0.0001f;
    physicsConfig.MaxAccumulatedSeconds = 0.05f;
    physicsConfig.MaxStepsPerFrame = 512u;
    physicsConfig.Gravity = glm::vec3{0.0f, -9.80665f, 0.0f};
    Runtime::SetPhysicsModuleConfig(config, physicsConfig);

    RealEngineProbe probe{};
    auto app = std::make_unique<RealEnginePhysicsApplication>(probe);
    Intrinsic::Tests::RuntimeTestKernel engine{
        std::move(config),
        std::move(app)};
    Runtime::EngineConfigControl& control =
        engine.EmplaceModule<Runtime::EngineConfigControl>(
            std::move(registry));
    engine.AddModule(std::move(physicsModule));
    engine.Initialize();
    EXPECT_EQ(engine.Services().Find<Runtime::PhysicsModule>(), nullptr);

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    auto hotPhysics = Runtime::GetPhysicsModuleConfig(candidate);
    ASSERT_TRUE(hotPhysics.has_value());
    hotPhysics->Gravity.y = -5.0f;
    Runtime::SetPhysicsModuleConfig(candidate, *hotPhysics);
    auto preview = control.PreviewEngineConfigControlDocument(
        CoreConfig::SerializeEngineConfig(candidate),
        "physics-agent-apply");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));
    auto applied = control.ApplyEngineConfigHotSubset(
        preview,
        Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(applied.Succeeded());
    EXPECT_FLOAT_EQ(physics->GetSnapshot().Config.Gravity.y, -5.0f);

    candidate = engine.GetEngineConfig();
    hotPhysics = Runtime::GetPhysicsModuleConfig(candidate);
    ASSERT_TRUE(hotPhysics.has_value());
    hotPhysics->MaxStepsPerFrame = 256u;
    Runtime::SetPhysicsModuleConfig(candidate, *hotPhysics);
    preview = control.PreviewEngineConfigControlDocument(
        CoreConfig::SerializeEngineConfig(candidate),
        "physics-programmatic-apply");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));
    applied = control.ApplyEngineConfigHotSubset(
        preview,
        Runtime::RuntimeConfigControlSource::Programmatic);
    ASSERT_TRUE(applied.Succeeded());
    EXPECT_EQ(physics->GetSnapshot().Config.MaxStepsPerFrame, 256u);
    EXPECT_EQ(configChanges, 2u);

    engine.Run();
    const Runtime::PhysicsModuleSnapshot snapshot = physics->GetSnapshot();
    EXPECT_FALSE(probe.FrameLimitExceeded);
    EXPECT_GE(probe.Frames, 2u);
    EXPECT_LT(probe.DynamicY, 10.0f);
    EXPECT_FLOAT_EQ(probe.StaticY, 20.0f);
    EXPECT_FLOAT_EQ(probe.KinematicY, 30.0f);
    EXPECT_TRUE(probe.DynamicDirty);
    EXPECT_GT(snapshot.Diagnostics.FixedSteps, 0u);
    EXPECT_GT(snapshot.Diagnostics.DynamicWritebacks, 0u);
    EXPECT_GT(snapshot.Diagnostics.StaticWritebacksSkipped, 0u);
    EXPECT_GT(snapshot.Diagnostics.KinematicWritebacksSkipped, 0u);
}


TEST(PhysicsModule, FreshParentTransformsSeparateEqualLocalColliderPoses)
{
    PhysicsModuleHarness harness;
    ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
    auto& scene = harness.ActiveScene();
    auto& raw = scene.Raw();
    for (const int index : {0, 1})
    {
        const auto parent = scene.Create();
        Transform::Component pose;
        pose.Position.x = static_cast<float>(index) * 10.0f;
        raw.emplace<Transform::Component>(parent, pose);
        const auto child = CreateAuthoredBody(scene, Id(100u + index), RigidBody::MakeDynamic());
        raw.emplace<Components::Hierarchy::Component>(child).Parent = parent;
    }
    harness.RunSimulation(0.01);
    const auto diagnostics = harness.Module().GetSnapshot().Diagnostics;
    EXPECT_EQ(diagnostics.ActiveWorldBodyCount, 2u);
    EXPECT_EQ(diagnostics.LastContactsSolved, 0u);
    EXPECT_FLOAT_EQ(diagnostics.LastMaxPenetrationBefore, 0.0f);
}

TEST(PhysicsModule, WorldGravityWritesBackThroughRotatedParent)
{
    PhysicsModuleHarness harness;
    auto config = EnabledConfig();
    config.Gravity.y = -1.0f;
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
    auto& scene = harness.ActiveScene();
    auto& raw = scene.Raw();
    const auto parent = scene.Create();
    Transform::Component parentPose;
    parentPose.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    raw.emplace<Transform::Component>(parent, parentPose);
    const auto child = CreateAuthoredBody(scene, Id(110u), RigidBody::MakeDynamic());
    raw.emplace<Components::Hierarchy::Component>(child).Parent = parent;
    harness.RunSimulation(0.01);
    const auto& local = raw.get<Transform::Component>(child);
    EXPECT_NEAR(local.Position.x, -0.0001f, 1.0e-7f);
    EXPECT_NEAR(local.Position.y, 0.0f, 1.0e-7f);
    const auto worldPosition = parentPose.Rotation * local.Position;
    EXPECT_NEAR(worldPosition.x, 0.0f, 1.0e-7f);
    EXPECT_NEAR(worldPosition.y, -0.0001f, 1.0e-7f);
}

TEST(PhysicsModule, DynamicParentWritesBackBeforeDynamicChild)
{
    PhysicsModuleHarness harness;
    auto config = EnabledConfig();
    config.Gravity.y = -1.0f;
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
    auto& scene = harness.ActiveScene();
    auto& raw = scene.Raw();
    // Create the child first so entity-order writeback is not hierarchy order.
    const auto child = CreateAuthoredBody(scene, Id(120u), RigidBody::MakeDynamic(), glm::vec3(0.0f, 10.0f, 0.0f));
    const auto parent = CreateAuthoredBody(scene, Id(121u), RigidBody::MakeDynamic());
    raw.emplace<Components::Hierarchy::Component>(child).Parent = parent;
    harness.RunSimulation(0.01);
    EXPECT_NEAR(raw.get<Transform::Component>(parent).Position.y, -0.0001f, 1.0e-7f);
    EXPECT_NEAR(raw.get<Transform::Component>(child).Position.y, 10.0f, 1.0e-6f);
    EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.ActiveWorldBodyCount, 2u);
    harness.RunSimulation(0.01);
    EXPECT_NEAR(raw.get<Transform::Component>(parent).Position.y, -0.0003f, 1.0e-7f);
    EXPECT_NEAR(raw.get<Transform::Component>(child).Position.y, 10.0f, 1.0e-6f);
    EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.BodiesUpdated, 0u);
}

TEST(PhysicsModule, MissingOrRemovedTransformCannotRetainPhysicsSidecar)
{
    for (const bool createFirst : {false, true})
    {
        SCOPED_TRACE(createFirst);
        PhysicsModuleHarness harness;
        ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
        auto& scene = harness.ActiveScene();
        const auto entity = CreateAuthoredBody(scene, Id(130u), RigidBody::MakeDynamic());
        if (createFirst)
        {
            harness.RunSimulation(0.0);
            ASSERT_EQ(harness.Module().GetSnapshot().Diagnostics.ActiveWorldBodyCount, 1u);
        }
        scene.Raw().remove<Transform::Component>(entity);
        harness.RunSimulation(0.0);
        const auto diagnostics = harness.Module().GetSnapshot().Diagnostics;
        EXPECT_EQ(diagnostics.ActiveWorldBodyCount, 0u);
        EXPECT_EQ(diagnostics.SidecarCount, 0u);
        EXPECT_EQ(diagnostics.MissingTransforms, 1u);
    }
}

TEST(PhysicsModule, NonVelocityAuthoringEditsPreserveIntegratedVelocity)
{
    for (const int edit : {0, 1, 2})
    {
        SCOPED_TRACE(edit);
        PhysicsModuleHarness harness;
        auto config = EnabledConfig();
        config.Gravity.y = -1.0f;
        ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
        auto& scene = harness.ActiveScene();
        auto& raw = scene.Raw();
        const auto entity = CreateAuthoredBody(scene, Id(140u), RigidBody::MakeDynamic());
        harness.RunSimulation(0.01);
        ASSERT_NEAR(raw.get<Transform::Component>(entity).Position.y, -0.0001f, 1.0e-7f);
        auto& authoring = raw.get<RigidBody::Component>(entity);
        if (edit == 0)
            authoring.Mass.Mass = 2.0f;
        else if (edit == 1)
            raw.get<Collider::Component>(entity).Shapes[0] = Collider::MakeSphere(0.6f);
        else
            authoring.LinearDamping = 0.5f;
        const float damping = 1.0f - authoring.LinearDamping * 0.01f;
        harness.RunSimulation(0.01);
        const float secondY = -0.0001f - 0.0002f * damping;
        EXPECT_NEAR(raw.get<Transform::Component>(entity).Position.y, secondY, 1.0e-7f);
        EXPECT_EQ(authoring.LinearVelocity, glm::vec3(0.0f));
        authoring.LinearVelocity.y = -0.3f;
        harness.RunSimulation(0.01);
        EXPECT_NEAR(raw.get<Transform::Component>(entity).Position.y, secondY - 0.0031f * damping, 1.0e-7f);
    }
}

TEST(PhysicsModule, KinematicAuthoredPoseStaysFixedWhileVelocityDrivesContacts)
{
    PhysicsModuleHarness harness;
    ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
    auto& scene = harness.ActiveScene();
    auto kinematic = RigidBody::MakeKinematic();
    kinematic.LinearVelocity.x = 1.0f;
    const auto anchor = CreateAuthoredBody(scene, Id(150u), kinematic);
    const auto dynamic = CreateAuthoredBody(scene, Id(151u), RigidBody::MakeDynamic(), glm::vec3(0.8f, 0.0f, 0.0f));
    harness.RunSimulation(0.01);
    EXPECT_NEAR(harness.Module().GetSnapshot().Diagnostics.LastMaxPenetrationBefore, 0.2f, 1.0e-6f);
    EXPECT_EQ(scene.Raw().get<Transform::Component>(anchor).Position, glm::vec3(0.0f));
    const float afterContact = scene.Raw().get<Transform::Component>(dynamic).Position.x;
    EXPECT_GT(afterContact, 0.8f);
    scene.Raw().get<Collider::Component>(anchor).Shapes[0].Filter.CollidesWithBits = 0u;
    harness.RunSimulation(0.01);
    EXPECT_NEAR(scene.Raw().get<Transform::Component>(dynamic).Position.x, afterContact + 0.01f, 1.0e-6f);
    EXPECT_EQ(scene.Raw().get<Transform::Component>(anchor).Position, glm::vec3(0.0f));
}

TEST(PhysicsModule, RuntimeFiltersAdmitDefaultsOmitZeroAndRejectUnsupportedPolicy)
{
    for (const int policy : {0, 1, 2, 3, 4, 5})
    {
        SCOPED_TRACE(policy);
        PhysicsModuleHarness harness;
        ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
        auto& scene = harness.ActiveScene();
        const auto a = CreateAuthoredBody(scene, Id(160u), RigidBody::MakeDynamic());
        const auto b = CreateAuthoredBody(scene, Id(161u), RigidBody::MakeDynamic(), glm::vec3(0.8f, 0.0f, 0.0f));
        auto& filterA = scene.Raw().get<Collider::Component>(a).Shapes[0].Filter;
        auto& filterB = scene.Raw().get<Collider::Component>(b).Shapes[0].Filter;
        if (policy == 1)
            filterA.CategoryBits = 0u;
        if (policy == 2)
            filterA.CollidesWithBits = 0u;
        if (policy == 3)
            filterA.GroupIndex = filterB.GroupIndex = 7;
        if (policy == 4)
            filterA.GroupIndex = filterB.GroupIndex = -7;
        if (policy == 5)
            filterA.CollidesWithBits = 4u;
        harness.RunSimulation(0.01);
        const auto diagnostics = harness.Module().GetSnapshot().Diagnostics;
        const bool expectedContact = policy == 0;
        EXPECT_EQ(diagnostics.LastContactsSolved > 0u, expectedContact);
        EXPECT_EQ(scene.Raw().get<Transform::Component>(a).Position.x < 0.0f, expectedContact);
        const unsigned bodies = policy == 3 || policy == 4 ? 0u : policy == 5 ? 1u : 2u;
        EXPECT_EQ(diagnostics.ActiveWorldBodyCount, bodies);
        EXPECT_EQ(diagnostics.InvalidDescriptors > 0u, policy >= 3);
        if (policy >= 3)
            EXPECT_EQ(diagnostics.LastUnsupportedReason, Runtime::PhysicsUnsupportedReason::CollisionFilter);
    }
}

TEST(PhysicsModule, UnsupportedAuthoredFieldsRemoveExistingSidecar)
{
    for (const int field : {0, 1, 2, 3, 4, 5, 6})
    {
        SCOPED_TRACE(field);
        PhysicsModuleHarness harness;
        ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
        auto& scene = harness.ActiveScene();
        const auto entity = CreateAuthoredBody(scene, Id(170u), RigidBody::MakeDynamic());
        harness.RunSimulation(0.0);
        ASSERT_EQ(harness.Module().GetSnapshot().Diagnostics.ActiveWorldBodyCount, 1u);
        auto& rigidBody = scene.Raw().get<RigidBody::Component>(entity);
        auto& shape = scene.Raw().get<Collider::Component>(entity).Shapes[0];
        switch (field)
        {
        case 0: rigidBody.Mass.Policy = RigidBody::MassPolicy::Density; break;
        case 1: rigidBody.Mass.LocalCenterOfMass.x = 0.1f; break;
        case 2: rigidBody.AllowSleep = false; break;
        case 3: rigidBody.StartsAwake = false; break;
        case 4: rigidBody.ContinuousCollisionDetection = true; break;
        case 5: shape.Surface.StaticFriction = 0.1f; break;
        case 6: shape.Offsets.ContactOffset = 0.1f; break;
        }
        harness.RunSimulation(0.0);
        const auto diagnostics = harness.Module().GetSnapshot().Diagnostics;
        EXPECT_EQ(diagnostics.ActiveWorldBodyCount, 0u);
        EXPECT_EQ(diagnostics.SidecarCount, 0u);
        EXPECT_GT(diagnostics.InvalidDescriptors, 0u);
        using Reason = Runtime::PhysicsUnsupportedReason;
        constexpr Reason reasons[]{Reason::MassPolicy, Reason::CenterOfMass, Reason::SleepPolicy,
            Reason::SleepPolicy, Reason::ContinuousCollisionDetection, Reason::Material, Reason::ContactOffsets};
        EXPECT_EQ(diagnostics.LastUnsupportedReason, reasons[field]);
        EXPECT_EQ(diagnostics.UnsupportedAuthoring, 1u);
    }
}


TEST(PhysicsModule, DynamicParentMustHaveUniformWorldScaleEvenWhenChildWorldDoes)
{
    for (const bool uniformParent : {false, true})
    {
        SCOPED_TRACE(uniformParent);
        PhysicsModuleHarness harness;
        ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
        auto& scene = harness.ActiveScene();
        auto& raw = scene.Raw();
        const auto parent = scene.Create();
        Transform::Component parentPose;
        parentPose.Scale = uniformParent ? glm::vec3(2.0f) : glm::vec3(2.0f, 1.0f, 1.0f);
        raw.emplace<Transform::Component>(parent, parentPose);
        const auto child = CreateAuthoredBody(scene, Id(180u), RigidBody::MakeDynamic());
        raw.get<Transform::Component>(child).Scale = 1.0f / parentPose.Scale;
        raw.emplace<Components::Hierarchy::Component>(child).Parent = parent;
        harness.RunSimulation(0.0);
        const auto first = harness.Module().GetSnapshot().Diagnostics;
        EXPECT_EQ(first.ActiveWorldBodyCount, uniformParent ? 1u : 0u);
        EXPECT_EQ(first.InvalidDescriptors > 0u, !uniformParent);
        if (!uniformParent)
            EXPECT_EQ(first.LastUnsupportedReason, Runtime::PhysicsUnsupportedReason::DynamicParentTransform);
        harness.RunSimulation(0.01);
        const auto second = harness.Module().GetSnapshot().Diagnostics;
        EXPECT_EQ(second.ActiveWorldBodyCount, uniformParent ? 1u : 0u);
        EXPECT_EQ(second.BodiesCreated, uniformParent ? 1u : 0u);
        EXPECT_EQ(second.BodiesRemoved, 0u);
    }
}


TEST(PhysicsModule, GravityHotApplyWakesExistingSleepingBody)
{
    PhysicsModuleHarness harness;
    auto config = EnabledConfig();
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
    auto& scene = harness.ActiveScene();
    const auto dynamic = CreateAuthoredBody(scene, Id(190u), RigidBody::MakeDynamic());
    for (unsigned step = 0; step < 60u; ++step)
        harness.RunSimulation(0.01);
    ASSERT_EQ(scene.Raw().get<Transform::Component>(dynamic).Position, glm::vec3(0.0f));
    ASSERT_EQ(harness.Module().GetSnapshot().Diagnostics.BodiesCreated, 1u);
    config.Gravity.y = -1.0f;
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
    harness.RunSimulation(0.01);
    EXPECT_NEAR(scene.Raw().get<Transform::Component>(dynamic).Position.y, -0.0001f, 1.0e-7f);
    EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.BodiesCreated, 1u);
    EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.BodiesUpdated, 0u);
}

TEST(PhysicsModule, UnsupportedDynamicScaleOverFloorNeverBecomesActivePhysicsBody)
{
    PhysicsModuleHarness harness;
    auto config = EnabledConfig();
    config.Gravity.y = -1.0f;
    ASSERT_TRUE(harness.Module().ApplyConfig(config).has_value());
    auto& scene = harness.ActiveScene();
    const auto floor = CreateAuthoredBody(scene, Id(200u), RigidBody::MakeStatic(), glm::vec3(0.0f, -0.5f, 0.0f));
    scene.Raw().get<Collider::Component>(floor).Shapes[0] = Collider::MakeBox(glm::vec3(5.0f, 0.5f, 5.0f));
    const auto dynamic = CreateAuthoredBody(scene, Id(201u), RigidBody::MakeDynamic(), glm::vec3(0.0f, 2.0f, 0.0f));
    scene.Raw().get<Transform::Component>(dynamic).Scale = glm::vec3(1.0f, 2.0f, 1.0f);
    harness.RunSimulation(0.01);
    const auto diagnostics = harness.Module().GetSnapshot().Diagnostics;
    EXPECT_EQ(diagnostics.ActiveWorldBodyCount, 1u);
    EXPECT_EQ(diagnostics.SidecarCount, 1u);
    EXPECT_EQ(diagnostics.InvalidDescriptors, 1u);
    EXPECT_EQ(diagnostics.UnsupportedAuthoring, 1u);
    EXPECT_EQ(diagnostics.LastUnsupportedReason, Runtime::PhysicsUnsupportedReason::ShapeTransform);
    EXPECT_EQ(scene.Raw().get<Transform::Component>(dynamic).Position, glm::vec3(0.0f, 2.0f, 0.0f));
}


TEST(PhysicsModuleConfig, RegistrationSchemaDescribesExistingPayloadBoundsAndGravity)
{
    using Json = nlohmann::json;
    const auto registration = Runtime::MakePhysicsModuleConfigSectionRegistration();
    ASSERT_FALSE(registration.SchemaJson.empty());
    const auto schema = Json::parse(registration.SchemaJson, nullptr, false);
    ASSERT_TRUE(schema.is_object());
    ASSERT_TRUE(schema.contains("properties"));
    const auto& properties = schema["properties"];
    ASSERT_TRUE(properties.is_object());
    const auto defaults = Json::parse(registration.DefaultSection.PayloadJson);
    ASSERT_EQ(properties.size(), defaults.size());
    EXPECT_EQ(schema.value("additionalProperties", true), false);
    EXPECT_EQ(schema.value("x-fallback", false), true);
    for (const auto& [name, value] : defaults.items())
    {
        ASSERT_TRUE(properties.contains(name));
        ASSERT_TRUE(properties[name].contains("default"));
        EXPECT_EQ(properties[name]["default"], value);
    }
    EXPECT_EQ(properties["enabled"]["type"], "boolean");
    EXPECT_NEAR(properties["fixed_delta_seconds"]["minimum"].get<double>(), 1.0e-6, 1.0e-12);
    EXPECT_EQ(properties["fixed_delta_seconds"]["maximum"], 1.0);
    EXPECT_EQ(properties["max_accumulated_seconds"]["maximum"], 10.0);
    EXPECT_EQ(properties["max_steps_per_frame"]["type"], "integer");
    EXPECT_EQ(properties["max_steps_per_frame"]["minimum"], 1);
    EXPECT_EQ(properties["max_steps_per_frame"]["maximum"], 1024);
    const auto& gravity = properties["gravity"];
    EXPECT_EQ(gravity["type"], "array");
    EXPECT_EQ(gravity["minItems"], 3);
    EXPECT_EQ(gravity["maxItems"], 3);
    ASSERT_TRUE(gravity.contains("items"));
    EXPECT_EQ(gravity["items"]["type"], "number");
    EXPECT_EQ(gravity["items"]["minimum"], -100000.0);
    EXPECT_EQ(gravity["items"]["maximum"], 100000.0);
}

TEST(PhysicsModuleConfig, DeclarativeSchemaKeepsFallbackCountsAndAccumulationClamp)
{
    using Json = nlohmann::json;
    Runtime::PhysicsModuleConfig reference;
    reference.FixedDeltaSeconds = 0.02f;
    reference.MaxAccumulatedSeconds = 0.2f;
    reference.MaxStepsPerFrame = 3u;
    reference.Gravity = glm::vec3(1.0f, 2.0f, 3.0f);
    const auto defaults = Runtime::SerializePhysicsModuleConfig(reference);
    const auto result = Runtime::ValidatePhysicsModuleConfigSection(
        R"({"enabled":true,"fixed_delta_seconds":"bad","max_steps_per_frame":0,"gravity":[0,100001,0],"extra":1})",
        defaults, "physics");
    EXPECT_EQ(result.State, CoreConfig::EngineConfigState::FallbackApplied);
    EXPECT_EQ(result.ParsedFieldCount, 1u);
    ASSERT_EQ(result.Diagnostics.size(), 4u);
    EXPECT_EQ(result.Diagnostics[0].Code, CoreConfig::EngineConfigDiagnosticCode::UnknownField);
    EXPECT_EQ(result.Diagnostics[0].Subject, "physics.extra");
    EXPECT_EQ(result.Diagnostics[1].Subject, "physics.fixed_delta_seconds");
    EXPECT_EQ(result.Diagnostics[1].Message, "Expected a numeric value; reference value retained.");
    EXPECT_EQ(result.Diagnostics[2].Subject, "physics.max_steps_per_frame");
    EXPECT_EQ(result.Diagnostics[2].Message, "Expected an integer in [1, 1024]; reference value retained.");
    EXPECT_EQ(result.Diagnostics[3].Subject, "physics.gravity");
    EXPECT_EQ(result.Diagnostics[3].Message,
        "Expected three finite gravity components in the supported range; reference value retained.");
    auto expected = Json::parse(defaults);
    expected["enabled"] = true;
    EXPECT_EQ(Json::parse(result.CanonicalPayloadJson), expected);

    const auto clamp = Runtime::ValidatePhysicsModuleConfigSection(
        R"({"fixed_delta_seconds":0.5,"max_accumulated_seconds":0.1})", defaults, "physics");
    EXPECT_EQ(clamp.State, CoreConfig::EngineConfigState::FallbackApplied);
    EXPECT_EQ(clamp.ParsedFieldCount, 2u);
    ASSERT_EQ(clamp.Diagnostics.size(), 1u);
    EXPECT_EQ(clamp.Diagnostics[0].Subject, "physics.max_accumulated_seconds");
    expected = Json::parse(defaults);
    expected["fixed_delta_seconds"] = 0.5;
    expected["max_accumulated_seconds"] = 0.5;
    EXPECT_EQ(Json::parse(clamp.CanonicalPayloadJson), expected);

    for (const auto& gravity : {Json::array({-100000.0, 0.0, 100000.0}),
                               Json::array({1.0, 2.0}), Json::array({1.0, "bad", 3.0})})
    {
        const bool accepted = gravity.size() == 3u && gravity[1].is_number();
        const auto parsed = Runtime::ValidatePhysicsModuleConfigSection(
            Json{{"gravity", gravity}}.dump(), defaults, "physics");
        EXPECT_EQ(parsed.State, accepted ? CoreConfig::EngineConfigState::Valid :
            CoreConfig::EngineConfigState::FallbackApplied);
        EXPECT_EQ(parsed.ParsedFieldCount, accepted ? 1u : 0u);
        EXPECT_EQ(Json::parse(parsed.CanonicalPayloadJson)["gravity"],
            accepted ? gravity : Json::parse(defaults)["gravity"]);
    }
}


TEST(PhysicsModule, StaticAndKinematicChildrenFollowDynamicParentsAfterAdmission)
{
    for (const bool kinematic : {false, true})
    {
        SCOPED_TRACE(kinematic);
        PhysicsModuleHarness harness;
        ASSERT_TRUE(harness.Module().ApplyConfig(EnabledConfig()).has_value());
        auto& scene = harness.ActiveScene();
        auto& raw = scene.Raw();
        auto moving = RigidBody::MakeDynamic();
        moving.LinearVelocity = glm::vec3(100.0f, 0.0f, 0.0f);
        moving.LinearDamping = 0.0f;
        const auto parent = CreateAuthoredBody(scene, Id(230u), moving);
        const auto child = CreateAuthoredBody(scene, Id(231u),
            kinematic ? RigidBody::MakeKinematic() : RigidBody::MakeStatic(),
            glm::vec3(0.0f, 10.0f, 0.0f));
        raw.emplace<Components::Hierarchy::Component>(child).Parent = parent;
        const auto probe = CreateAuthoredBody(scene, Id(232u), RigidBody::MakeDynamic(),
            glm::vec3(1.8f, 10.0f, 0.0f));

        harness.RunSimulation(0.01);
        EXPECT_NEAR(raw.get<Transform::Component>(parent).Position.x, 1.0f, 1e-6f);
        EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.LastContactsSolved, 0u);
        EXPECT_EQ(harness.Module().GetSnapshot().Diagnostics.ActiveWorldBodyCount, 3u);
        // The next sync must move the child's physics collider from x=0 to x=1,
        // where the probe overlaps by 0.2. The parent's own collider is far below.
        harness.RunSimulation(0.01);
        const auto diagnostics = harness.Module().GetSnapshot().Diagnostics;
        EXPECT_EQ(diagnostics.BodiesUpdated, 1u);
        EXPECT_GT(diagnostics.LastContactsSolved, 0u);
        EXPECT_NEAR(diagnostics.LastMaxPenetrationBefore, 0.2f, 1e-6f);
        EXPECT_GT(raw.get<Transform::Component>(probe).Position.x, 1.8f);
        EXPECT_EQ(raw.get<Transform::Component>(child).Position, glm::vec3(0.0f, 10.0f, 0.0f));
    }
}
