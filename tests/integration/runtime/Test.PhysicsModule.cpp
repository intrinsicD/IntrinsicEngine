#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Component.Collider;
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
