#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    using namespace std::chrono_literals;

    [[nodiscard]] CoreConfig::EngineConfig NullWindowHeadlessConfig(
        const unsigned workers = 2u)
    {
        CoreConfig::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        config.Simulation.WorkerThreadCount = workers;
        return config;
    }

    void SetPositions(GS::Vertices& vertices,
                      const std::vector<glm::vec3>& positions)
    {
        vertices.Properties.Resize(positions.size());
        auto pos = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

    [[nodiscard]] ECS::EntityHandle AddPointCloud(
        ECS::Scene::Registry& scene,
        const std::vector<glm::vec3>& positions)
    {
        const ECS::EntityHandle entity = scene.Create();
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity);
        SetPositions(vertices, positions);
        return entity;
    }

    [[nodiscard]] bool HasPointLabels(ECS::Scene::Registry& scene,
                                      const ECS::EntityHandle entity)
    {
        if (!scene.Raw().valid(entity) ||
            !scene.Raw().all_of<GS::Vertices>(entity))
        {
            return false;
        }
        return static_cast<bool>(
            scene.Raw().get<GS::Vertices>(entity)
                .Properties.Get<std::uint32_t>("p:kmeans_label"));
    }

    [[nodiscard]] std::size_t PointLabelCount(ECS::Scene::Registry& scene,
                                              const ECS::EntityHandle entity)
    {
        auto labels = scene.Raw().get<GS::Vertices>(entity)
                          .Properties.Get<std::uint32_t>("p:kmeans_label");
        return labels ? labels.Vector().size() : 0u;
    }

    class KMeansSuccessApp final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            MainThread = std::this_thread::get_id();
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Entity = AddPointCloud(
                engine.GetScene(),
                {
                    {0.0f, 0.0f, 0.0f},
                    {0.1f, 0.0f, 0.0f},
                    {2.0f, 0.0f, 0.0f},
                    {2.1f, 0.0f, 0.0f},
                });
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);

            CompletionSub = service->SubscribeRunCompleted(
                [this](const Runtime::KMeansRunCompleted& completed)
                {
                    Completion = completed;
                    CompletionThread = std::this_thread::get_id();
                });
            ChangedSub = service->SubscribeClusterLabelsChanged(
                [this](const Runtime::ClusterLabelsChanged& changed)
                {
                    LabelsChanged = changed;
                    LabelsChangedThread = std::this_thread::get_id();
                });

            Correlation = service->RunKMeans(Runtime::RunKMeans{
                .StableEntityId = StableEntityId,
                .Domain = Runtime::ClusteringDomain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            });
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            Ticks += 1u;
            const bool committed =
                Entity != ECS::InvalidEntityHandle &&
                HasPointLabels(engine.GetScene(), Entity);
            const bool dirty =
                Entity != ECS::InvalidEntityHandle &&
                engine.GetScene().Raw().all_of<Dirty::DirtyVertexAttributes>(
                    Entity);
            if (Completion.has_value() &&
                LabelsChanged.has_value() &&
                committed &&
                dirty)
            {
                Stats = engine.Services()
                            .Find<Runtime::ClusteringService>()
                            ->Stats();
                engine.RequestExit();
                return;
            }

            if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::KernelEventSubscription CompletionSub{};
        Runtime::KernelEventSubscription ChangedSub{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::ClusteringModuleStats Stats{};
        std::optional<Runtime::KMeansRunCompleted> Completion{};
        std::optional<Runtime::ClusterLabelsChanged> LabelsChanged{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::thread::id MainThread{};
        std::thread::id CompletionThread{};
        std::thread::id LabelsChangedThread{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class KMeansWorldSwitchApp final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            SubmittedWorld = engine.ActiveWorld();
            SubmittedScene = &engine.GetScene();
            Entity = AddPointCloud(
                *SubmittedScene,
                {
                    {0.0f, 0.0f, 0.0f},
                    {0.1f, 0.0f, 0.0f},
                    {2.0f, 0.0f, 0.0f},
                    {2.1f, 0.0f, 0.0f},
                });
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);

            CompletionSub = service->SubscribeRunCompleted(
                [this](const Runtime::KMeansRunCompleted& completed)
                {
                    Completion = completed;
                });

            Extrinsic::Core::Tasks::Scheduler::Dispatch(
                [this]
                {
                    BlockerStarted.store(true, std::memory_order_release);
                    while (!ReleaseBlocker.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(1ms);
                });

            Correlation = service->RunKMeans(Runtime::RunKMeans{
                .StableEntityId = StableEntityId,
                .Domain = Runtime::ClusteringDomain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            });
            NextWorld = engine.Worlds().CreateWorld("Switched");
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            Ticks += 1u;
            if (!SwitchRequested &&
                BlockerStarted.load(std::memory_order_acquire))
            {
                (void)engine.Worlds().RequestSetActiveWorld(NextWorld);
                SwitchRequested = true;
            }

            if (SwitchRequested &&
                engine.ActiveWorld() == NextWorld &&
                !ReleaseBlocker.load(std::memory_order_acquire))
            {
                ReleaseBlocker.store(true, std::memory_order_release);
            }

            if (Completion.has_value())
            {
                Stats = engine.Services()
                            .Find<Runtime::ClusteringService>()
                            ->Stats();
                engine.RequestExit();
                return;
            }

            if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override
        {
            ReleaseBlocker.store(true, std::memory_order_release);
        }

        Runtime::KernelEventSubscription CompletionSub{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::ClusteringModuleStats Stats{};
        std::optional<Runtime::KMeansRunCompleted> Completion{};
        Runtime::WorldHandle SubmittedWorld{};
        Runtime::WorldHandle NextWorld{};
        ECS::Scene::Registry* SubmittedScene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::atomic<bool> BlockerStarted{false};
        std::atomic<bool> ReleaseBlocker{false};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool SwitchRequested{false};
        bool TimedOut{false};
    };

    class MissingModuleApp final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Entity = AddPointCloud(
                engine.GetScene(),
                {
                    {0.0f, 0.0f, 0.0f},
                    {0.1f, 0.0f, 0.0f},
                    {2.0f, 0.0f, 0.0f},
                    {2.1f, 0.0f, 0.0f},
                });
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);
            Correlation = engine.Commands().Enqueue(Runtime::RunKMeans{
                .StableEntityId = StableEntityId,
                .Domain = Runtime::ClusteringDomain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            });
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            Ticks += 1u;
            if (Ticks >= 2u)
            {
                CommandStats = engine.Commands().Stats();
                LabelsCommitted = HasPointLabels(engine.GetScene(), Entity);
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::CommandCorrelationId Correlation{};
        Runtime::CommandBusStats CommandStats{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::uint32_t Ticks{0u};
        bool LabelsCommitted{false};
    };
}

TEST(ClusteringModule, EngineRunCommitsLabelsAndPublishesChangeEvent)
{
    auto app = std::make_unique<KMeansSuccessApp>();
    KMeansSuccessApp* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Correlation.IsValid());
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_TRUE(appPtr->Completion->Succeeded()) << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->LabelCount, 4u);
    EXPECT_EQ(appPtr->Completion->ClusterCount, 2u);
    EXPECT_EQ(appPtr->Completion->ActualBackend,
              Runtime::ClusteringBackend::CpuReference);
    EXPECT_EQ(appPtr->CompletionThread, appPtr->MainThread);

    ASSERT_TRUE(appPtr->LabelsChanged.has_value());
    EXPECT_EQ(appPtr->LabelsChanged->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->LabelsChanged->StableEntityId, appPtr->StableEntityId);
    EXPECT_EQ(appPtr->LabelsChanged->LabelCount, 4u);
    EXPECT_EQ(appPtr->LabelsChangedThread, appPtr->MainThread);
    EXPECT_EQ(PointLabelCount(engine.GetScene(), appPtr->Entity), 4u);
    EXPECT_TRUE(engine.GetScene().Raw().all_of<Dirty::DirtyVertexAttributes>(
        appPtr->Entity));
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 1u);
    EXPECT_EQ(appPtr->Stats.VisualizationRefreshReactions, 1u);

    engine.Shutdown();
}

TEST(ClusteringModule, WorldSwitchBeforeCompletionDropsCommit)
{
    auto app = std::make_unique<KMeansWorldSwitchApp>();
    KMeansWorldSwitchApp* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(1u), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_EQ(appPtr->Completion->Status, Runtime::KMeansRunStatus::StaleWorld);
    EXPECT_EQ(appPtr->Completion->World, appPtr->SubmittedWorld);
    ASSERT_NE(appPtr->SubmittedScene, nullptr);
    EXPECT_FALSE(HasPointLabels(*appPtr->SubmittedScene, appPtr->Entity));
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 0u);
    EXPECT_EQ(appPtr->Stats.CommitsDropped, 1u);

    engine.Shutdown();
}

TEST(ClusteringModule, RunKMeansWithoutModuleFailsClosedAtCommandDrain)
{
    auto app = std::make_unique<MissingModuleApp>();
    MissingModuleApp* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.Initialize();
    engine.Run();

    EXPECT_TRUE(appPtr->Correlation.IsValid());
    EXPECT_EQ(appPtr->CommandStats.Executed, 0u);
    EXPECT_EQ(appPtr->CommandStats.MissingHandler, 1u);
    EXPECT_FALSE(appPtr->LabelsCommitted);

    engine.Shutdown();
}
