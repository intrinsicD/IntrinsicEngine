#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SceneDocumentModule;
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

    void SetCustomKMeansInput(
        GS::Vertices& vertices,
        const std::vector<glm::vec3>& positions)
    {
        auto input = vertices.Properties.GetOrAdd<glm::vec3>(
            "p:cluster_input",
            glm::vec3{0.0f});
        input.Vector() = positions;
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

    [[nodiscard]] Runtime::RunKMeans MakePointCloudRequest(
        const std::uint32_t stableEntityId,
        const Runtime::ClusteringBackend backend =
            Runtime::ClusteringBackend::CpuReference)
    {
        return Runtime::RunKMeans{
            .StableEntityId = stableEntityId,
            .Properties = Runtime::MakeKMeansPropertyRefs(
                Runtime::GeometryElementDomain::PointCloudPoint),
            .Parameters = Runtime::KMeansParameters{
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            },
            .Backend = backend,
        };
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

    class KMeansSuccessApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit KMeansSuccessApp(
            const Runtime::ClusteringBackend backend =
                Runtime::ClusteringBackend::CpuReference)
            : Backend(backend)
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            MainThread   = std::this_thread::get_id();
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Entity = AddPointCloud(
                *engine.Worlds().Get(engine.ActiveWorld()),
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

            Correlation = service->RunKMeans(
                MakePointCloudRequest(StableEntityId, Backend));
        }


        void Frame(double, double) override
        {
            auto& engine = Kernel();
            Ticks += 1u;
            const bool committed =
                Entity != ECS::InvalidEntityHandle &&
                HasPointLabels(*engine.Worlds().Get(engine.ActiveWorld()), Entity);
            const bool dirty =
                Entity != ECS::InvalidEntityHandle &&
                engine.Worlds().Get(engine.ActiveWorld())->Raw().all_of<Dirty::DirtyVertexAttributes>(
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

        void Shutdown() override {}

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
        Runtime::ClusteringBackend Backend{
            Runtime::ClusteringBackend::CpuReference};
    };

    class KMeansWorldSwitchApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            SubmittedWorld = engine.ActiveWorld();
            SubmittedScene = &*engine.Worlds().Get(engine.ActiveWorld());
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

            Correlation = service->RunKMeans(
                MakePointCloudRequest(StableEntityId));
            NextWorld = engine.Worlds().CreateWorld("Switched");
        }


        void Frame(double, double) override
        {
            auto& engine = Kernel();
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

        void Shutdown() override { ReleaseBlocker.store(true, std::memory_order_release); }

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

    class MissingModuleApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Entity =
                AddPointCloud(*engine.Worlds().Get(engine.ActiveWorld()), {
                                                                              {0.0f, 0.0f, 0.0f},
                                                                              {0.1f, 0.0f, 0.0f},
                                                                              {2.0f, 0.0f, 0.0f},
                                                                              {2.1f, 0.0f, 0.0f},
                                                                          });
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);
            Correlation = engine.Commands().Enqueue(
                MakePointCloudRequest(StableEntityId));
        }


        void Frame(double, double) override
        {
            auto& engine = Kernel();
            Ticks += 1u;
            if (Ticks >= 2u)
            {
                CommandStats = engine.Commands().Stats();
                LabelsCommitted = HasPointLabels(*engine.Worlds().Get(engine.ActiveWorld()), Entity);
                engine.RequestExit();
            }
        }

        void Shutdown() override {}

        Runtime::CommandCorrelationId Correlation{};
        Runtime::CommandBusStats CommandStats{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::uint32_t Ticks{0u};
        bool LabelsCommitted{false};
    };

    enum class ControlledCompletionAction : std::uint8_t
    {
        Cancel,
        MutateSource,
    };

    class KMeansControlledCompletionApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit KMeansControlledCompletionApp(
            const ControlledCompletionAction action)
            : Action(action)
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Scene = engine.Worlds().Get(engine.ActiveWorld());
            Entity = AddPointCloud(
                *Scene,
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
            Correlation = service->RunKMeans(
                MakePointCloudRequest(StableEntityId));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            Ticks += 1u;
            if (!ActionTaken &&
                BlockerStarted.load(std::memory_order_acquire))
            {
                const std::vector<Runtime::JobSnapshot> jobs =
                    engine.Jobs().SnapshotAll();
                const auto kmeans = std::find_if(
                    jobs.begin(),
                    jobs.end(),
                    [](const Runtime::JobSnapshot& job)
                    {
                        return job.DebugName ==
                               "Runtime.Clustering.KMeans.CPU";
                    });
                if (kmeans != jobs.end())
                {
                    Job = kmeans->Token;
                    if (Action == ControlledCompletionAction::Cancel)
                    {
                        CancelAccepted = engine.Jobs().Cancel(Job);
                    }
                    else
                    {
                        SetPositions(
                            Scene->Raw().get<GS::Vertices>(Entity),
                            {
                                {10.0f, 0.0f, 0.0f},
                                {11.0f, 0.0f, 0.0f},
                                {12.0f, 0.0f, 0.0f},
                                {13.0f, 0.0f, 0.0f},
                            });
                    }
                    ActionTaken = true;
                    ReleaseBlocker.store(true, std::memory_order_release);
                }
            }

            if (Completion.has_value())
            {
                JobStats = engine.Jobs().Stats();
                engine.RequestExit();
                return;
            }
            if (Ticks > 240u)
            {
                TimedOut = true;
                ReleaseBlocker.store(true, std::memory_order_release);
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            ReleaseBlocker.store(true, std::memory_order_release);
        }

        ControlledCompletionAction Action{};
        Runtime::KernelEventSubscription CompletionSub{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::JobToken Job{};
        Runtime::JobServiceStats JobStats{};
        std::optional<Runtime::KMeansRunCompleted> Completion{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::atomic<bool> BlockerStarted{false};
        std::atomic<bool> ReleaseBlocker{false};
        std::uint32_t Ticks{0u};
        bool ActionTaken{false};
        bool CancelAccepted{false};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class KMeansValidationApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            ECS::Scene::Registry& scene =
                *engine.Worlds().Get(engine.ActiveWorld());
            const ECS::EntityHandle valid = AddPointCloud(
                scene,
                {
                    {0.0f, 0.0f, 0.0f},
                    {1.0f, 0.0f, 0.0f},
                });
            const ECS::EntityHandle missingInput = scene.Create();
            auto& missingVertices =
                scene.Raw().emplace<GS::Vertices>(missingInput);
            missingVertices.Properties.Resize(2u);

            CompletionSub = service->SubscribeRunCompleted(
                [this](const Runtime::KMeansRunCompleted& completed)
                {
                    Completions.push_back(completed);
                });

            Runtime::RunKMeans invalid = MakePointCloudRequest(
                Runtime::SelectionController::ToStableEntityId(valid));
            invalid.Parameters.ClusterCount = 0u;
            Correlations.push_back(service->RunKMeans(std::move(invalid)));
            Correlations.push_back(service->RunKMeans(MakePointCloudRequest(
                std::numeric_limits<std::uint32_t>::max())));
            Correlations.push_back(service->RunKMeans(MakePointCloudRequest(
                Runtime::SelectionController::ToStableEntityId(
                    missingInput))));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            Ticks += 1u;
            if (Completions.size() == Correlations.size())
            {
                engine.RequestExit();
                return;
            }
            if (Ticks > 60u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override {}

        Runtime::KernelEventSubscription CompletionSub{};
        std::vector<Runtime::CommandCorrelationId> Correlations{};
        std::vector<Runtime::KMeansRunCompleted> Completions{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class KMeansCustomWritebackApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Runtime::ClusteringService* service =
                engine.Services().Find<Runtime::ClusteringService>();
            if (service == nullptr || !service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Scene = engine.Worlds().Get(engine.ActiveWorld());
            const std::vector<glm::vec3> positions{
                {0.0f, 0.0f, 0.0f},
                {0.1f, 0.0f, 0.0f},
                {2.0f, 0.0f, 0.0f},
                {2.1f, 0.0f, 0.0f},
            };
            First = AddPointCloud(*Scene, positions);
            Second = AddPointCloud(*Scene, positions);
            SetCustomKMeansInput(
                Scene->Raw().get<GS::Vertices>(First), positions);
            SetCustomKMeansInput(
                Scene->Raw().get<GS::Vertices>(Second), positions);

            CompletionSub = service->SubscribeRunCompleted(
                [this](const Runtime::KMeansRunCompleted& completed)
                {
                    Completions.push_back(completed);
                });
            Correlations.push_back(service->RunKMeans(
                MakeRequest(First)));
            Correlations.push_back(service->RunKMeans(
                MakeRequest(Second)));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            Ticks += 1u;
            if (Completions.size() == Correlations.size())
            {
                const Geometry::PropertySet& firstProperties =
                    Scene->Raw().get<GS::Vertices>(First).Properties;
                const Geometry::PropertySet& secondProperties =
                    Scene->Raw().get<GS::Vertices>(Second).Properties;
                const auto firstLabels =
                    firstProperties.Get<std::uint32_t>("p:cluster_id");
                const auto secondLabels =
                    secondProperties.Get<std::uint32_t>("p:cluster_id");
                const auto firstColors =
                    firstProperties.Get<glm::vec4>("p:cluster_color");
                const auto firstScalars =
                    firstProperties.Get<float>("p:cluster_scalar");
                OutputsReady = firstLabels && secondLabels && firstColors &&
                               firstScalars &&
                               firstLabels.Vector().size() == 4u &&
                               firstColors.Vector().size() == 4u &&
                               firstScalars.Vector().size() == 4u;
                LabelsEqual = OutputsReady &&
                              firstLabels.Vector() == secondLabels.Vector();
                engine.RequestExit();
                return;
            }
            if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override {}

        [[nodiscard]] static Runtime::RunKMeans MakeRequest(
            const ECS::EntityHandle entity)
        {
            Runtime::RunKMeans request = MakePointCloudRequest(
                Runtime::SelectionController::ToStableEntityId(entity));
            request.Properties.InputPositions.Name = "p:cluster_input";
            request.Properties.OutputLabels.Name = "p:cluster_id";
            request.Properties.OutputColors.Name = "p:cluster_color";
            request.Properties.OutputScalarLabels = Runtime::GeometryPropertyRef{
                .Domain = Runtime::GeometryElementDomain::PointCloudPoint,
                .Name = "p:cluster_scalar",
                .ValueKind = Geometry::PropertyValueKind::Float,
            };
            return request;
        }

        Runtime::KernelEventSubscription CompletionSub{};
        std::vector<Runtime::CommandCorrelationId> Correlations{};
        std::vector<Runtime::KMeansRunCompleted> Completions{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle First{ECS::InvalidEntityHandle};
        ECS::EntityHandle Second{ECS::InvalidEntityHandle};
        std::uint32_t Ticks{0u};
        bool OutputsReady{false};
        bool LabelsEqual{false};
        bool MissingService{false};
        bool TimedOut{false};
    };
}

TEST(ClusteringModule, EngineRunCommitsLabelsAndPublishesChangeEvent)
{
    auto app = std::make_unique<KMeansSuccessApp>();
    KMeansSuccessApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));
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
    EXPECT_EQ(appPtr->Completion->Properties.InputPositions.Domain,
              Runtime::GeometryElementDomain::PointCloudPoint);
    EXPECT_EQ(appPtr->Completion->Properties.InputPositions.Name,
              "v:position");
    EXPECT_EQ(appPtr->Completion->Properties.OutputLabels.Name,
              "p:kmeans_label");
    EXPECT_EQ(appPtr->Completion->Properties.OutputColors.Name,
              "p:kmeans_color");
    EXPECT_FALSE(
        appPtr->Completion->Properties.OutputScalarLabels.has_value());
    EXPECT_EQ(appPtr->CompletionThread, appPtr->MainThread);

    ASSERT_TRUE(appPtr->LabelsChanged.has_value());
    EXPECT_EQ(appPtr->LabelsChanged->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->LabelsChanged->StableEntityId, appPtr->StableEntityId);
    EXPECT_EQ(appPtr->LabelsChanged->LabelCount, 4u);
    EXPECT_EQ(appPtr->LabelsChangedThread, appPtr->MainThread);
    EXPECT_EQ(PointLabelCount(*engine.Worlds().Get(engine.ActiveWorld()), appPtr->Entity), 4u);
    EXPECT_TRUE(engine.Worlds().Get(engine.ActiveWorld())->Raw().all_of<Dirty::DirtyVertexAttributes>(
        appPtr->Entity));
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 1u);
    EXPECT_EQ(appPtr->Stats.VisualizationRefreshReactions, 1u);

    engine.Shutdown();
}

TEST(ClusteringModule, DocumentHistoryOwnsQueuedOutputUndoRedo)
{
    auto app = std::make_unique<KMeansSuccessApp>();
    KMeansSuccessApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(),
        std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();

    ASSERT_TRUE(appPtr->Completion.has_value());
    ASSERT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    Runtime::EditorCommandHistory* history =
        engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->UndoCount(), 1u);

    ECS::Scene::Registry* scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    auto& properties =
        scene->Raw().get<GS::Vertices>(appPtr->Entity).Properties;
    const auto labels =
        properties.Get<std::uint32_t>("p:kmeans_label");
    const auto colors =
        properties.Get<glm::vec4>("p:kmeans_color");
    ASSERT_TRUE(labels);
    ASSERT_TRUE(colors);
    const std::vector<std::uint32_t> publishedLabels =
        labels.Vector();
    const std::vector<glm::vec4> publishedColors =
        colors.Vector();

    scene->Raw().remove<Dirty::DirtyVertexAttributes>(
        appPtr->Entity);
    EXPECT_EQ(history->Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists("p:kmeans_label"));
    EXPECT_FALSE(properties.Exists("p:kmeans_color"));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexAttributes>(
        appPtr->Entity));

    scene->Raw().remove<Dirty::DirtyVertexAttributes>(
        appPtr->Entity);
    EXPECT_EQ(history->Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    auto redoneLabels =
        properties.Get<std::uint32_t>("p:kmeans_label");
    auto redoneColors =
        properties.Get<glm::vec4>("p:kmeans_color");
    ASSERT_TRUE(redoneLabels);
    ASSERT_TRUE(redoneColors);
    EXPECT_EQ(redoneLabels.Vector(), publishedLabels);
    EXPECT_EQ(redoneColors.Vector(), publishedColors);
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexAttributes>(
        appPtr->Entity));

    redoneLabels.Vector()[0] += 1u;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        history->Snapshot();
    EXPECT_EQ(history->Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(history->UndoCount(), 1u);
    EXPECT_EQ(history->RedoCount(), 0u);
    EXPECT_EQ(history->Snapshot().Revision,
              beforeRejectedUndo.Revision);

    redoneLabels.Vector() = publishedLabels;
    EXPECT_EQ(history->Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists("p:kmeans_label"));
    EXPECT_FALSE(properties.Exists("p:kmeans_color"));

    engine.Shutdown();
}

TEST(ClusteringModule, NullDeviceVulkanRequestFallsBackHonestly)
{
    auto app = std::make_unique<KMeansSuccessApp>(
        Runtime::ClusteringBackend::VulkanCompute);
    KMeansSuccessApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->RequestedBackend,
              Runtime::ClusteringBackend::VulkanCompute);
    EXPECT_EQ(appPtr->Completion->ActualBackend,
              Runtime::ClusteringBackend::CpuReference);
    EXPECT_TRUE(appPtr->Completion->FellBackToCpu);
    EXPECT_FALSE(appPtr->Completion->BackendDiagnostic.empty());

    engine.Shutdown();
}

TEST(ClusteringModule, WorldSwitchBeforeCompletionDropsCommit)
{
    auto app = std::make_unique<KMeansWorldSwitchApp>();
    KMeansWorldSwitchApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(1u), std::move(app));
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

TEST(ClusteringModule, ValidationFailuresPublishCanonicalTypedCompletions)
{
    auto app = std::make_unique<KMeansValidationApp>();
    KMeansValidationApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_EQ(appPtr->Completions.size(), 3u);
    EXPECT_TRUE(std::all_of(
        appPtr->Correlations.begin(),
        appPtr->Correlations.end(),
        [](const Runtime::CommandCorrelationId correlation)
        {
            return correlation.IsValid();
        }));
    const auto countStatus = [appPtr](const Runtime::KMeansRunStatus status)
    {
        return std::count_if(
            appPtr->Completions.begin(),
            appPtr->Completions.end(),
            [status](const Runtime::KMeansRunCompleted& completion)
            {
                return completion.Status == status;
            });
    };
    EXPECT_EQ(
        countStatus(Runtime::KMeansRunStatus::InvalidProcessingParameters),
        1);
    EXPECT_EQ(countStatus(Runtime::KMeansRunStatus::StaleEntity), 1);
    EXPECT_EQ(
        countStatus(Runtime::KMeansRunStatus::UnsupportedGeometryDomain),
        1);
    for (const Runtime::KMeansRunCompleted& completion :
         appPtr->Completions)
    {
        EXPECT_FALSE(completion.Succeeded());
        EXPECT_EQ(completion.ActualBackend,
                  Runtime::ClusteringBackend::None);
        EXPECT_FALSE(completion.Message.empty());
    }

    engine.Shutdown();
}

TEST(ClusteringModule, CpuResultsAreDeterministicAndHonorCustomPropertyRefs)
{
    auto app = std::make_unique<KMeansCustomWritebackApp>();
    KMeansCustomWritebackApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_TRUE(appPtr->OutputsReady);
    EXPECT_TRUE(appPtr->LabelsEqual);
    ASSERT_EQ(appPtr->Completions.size(), 2u);
    for (const Runtime::KMeansRunCompleted& completion :
         appPtr->Completions)
    {
        EXPECT_TRUE(completion.Succeeded()) << completion.Message;
        EXPECT_EQ(completion.Properties.InputPositions.Name,
                  "p:cluster_input");
        EXPECT_EQ(completion.Properties.OutputLabels.Name,
                  "p:cluster_id");
        EXPECT_EQ(completion.Properties.OutputColors.Name,
                  "p:cluster_color");
        ASSERT_TRUE(
            completion.Properties.OutputScalarLabels.has_value());
        EXPECT_EQ(completion.Properties.OutputScalarLabels->Name,
                  "p:cluster_scalar");
    }
    EXPECT_FLOAT_EQ(appPtr->Completions[0].Inertia,
                    appPtr->Completions[1].Inertia);

    engine.Shutdown();
}

TEST(ClusteringModule, SourceMutationBeforeCompletionDropsWriteback)
{
    auto app = std::make_unique<KMeansControlledCompletionApp>(
        ControlledCompletionAction::MutateSource);
    KMeansControlledCompletionApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(1u), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_TRUE(appPtr->ActionTaken);
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->Status,
              Runtime::KMeansRunStatus::StaleSource);
    EXPECT_FALSE(HasPointLabels(*appPtr->Scene, appPtr->Entity));

    engine.Shutdown();
}

TEST(ClusteringModule, CancelledCpuWorkPublishesCanonicalCompletion)
{
    auto app = std::make_unique<KMeansControlledCompletionApp>(
        ControlledCompletionAction::Cancel);
    KMeansControlledCompletionApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(1u), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_TRUE(appPtr->ActionTaken);
    EXPECT_TRUE(appPtr->CancelAccepted);
    ASSERT_TRUE(appPtr->Job.IsValid());
    EXPECT_GE(appPtr->JobStats.CancelledJobs, 1u);
    EXPECT_GE(appPtr->JobStats.FinalizedUnpublishedJobs, 1u);
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->Status,
              Runtime::KMeansRunStatus::Cancelled);
    EXPECT_FALSE(HasPointLabels(*appPtr->Scene, appPtr->Entity));

    engine.Shutdown();
}

TEST(ClusteringModule, RunKMeansWithoutModuleFailsClosedAtCommandDrain)
{
    auto app = std::make_unique<MissingModuleApp>();
    MissingModuleApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));
    engine.Initialize();
    engine.Run();

    EXPECT_TRUE(appPtr->Correlation.IsValid());
    EXPECT_EQ(appPtr->CommandStats.Executed, 0u);
    EXPECT_EQ(appPtr->CommandStats.MissingHandler, 1u);
    EXPECT_FALSE(appPtr->LabelsCommitted);

    engine.Shutdown();
}
