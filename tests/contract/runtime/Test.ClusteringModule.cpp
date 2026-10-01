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
#include <entt/entity/registry.hpp>

#include "RuntimeTestModule.hpp"
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"
#include "Modules/Clustering/Runtime.KMeansPaging.TestSupport.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <utility>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
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
            const auto nonfinite = AddPointCloud(scene,
                {{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}});
            auto nonfiniteRequest = MakePointCloudRequest(
                Runtime::SelectionController::ToStableEntityId(nonfinite));
            EXPECT_FALSE(Runtime::ValidateKMeansRequest(&scene.Raw(), nonfiniteRequest));
            Correlations.push_back(service->RunKMeans(nonfiniteRequest));
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
    ASSERT_EQ(appPtr->Completions.size(), 4u);
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
        2);
    for (const Runtime::KMeansRunCompleted& completion :
         appPtr->Completions)
    {
        EXPECT_EQ(completion.World, engine.ActiveWorld());
        EXPECT_NE(std::find(appPtr->Correlations.begin(), appPtr->Correlations.end(),
                           completion.Correlation), appPtr->Correlations.end());
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
        EXPECT_EQ(completion.World, engine.ActiveWorld());
        EXPECT_NE(std::find(appPtr->Correlations.begin(), appPtr->Correlations.end(),
                           completion.Correlation), appPtr->Correlations.end());
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

TEST(ClusteringModule, EditorReadinessUsesLiveConfigLaneAndCanonicalAdmission)
{
    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(1u));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    auto* service = engine.Services().Find<Runtime::ClusteringService>();
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Available());
    auto* scene = engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const auto entity = AddPointCloud(*scene, {{0, 0, 0}, {1, 0, 0}});
    auto request = MakePointCloudRequest(Runtime::SelectionController::ToStableEntityId(entity));
    auto& properties = scene->Raw().get<GS::Vertices>(entity).Properties;

    CoreConfig::EngineConfigSectionRegistry sections;
    ASSERT_TRUE(sections.Register(Runtime::MakeClusteringConfigSectionRegistration()));
    Runtime::RuntimeEngineConfigControlState state;
    CoreConfig::PopulateEngineConfigSectionDefaults(state.ActiveConfig, sections);
    unsigned previews = 0, applies = 0;
    bool attached = true;
    Runtime::EditorProcessingContext context{.Scene = scene};
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    context.PreviewEngineConfigDocument = [&](const auto& document, const auto& origin) {
        ++previews;
        return CoreConfig::PreviewEngineConfig(document, state.ActiveConfig, {origin, &sections});
    };
    context.ApplyEngineConfigHotSubset = [&](const auto& preview) {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return Runtime::RuntimeEngineConfigApplyResult{.Status = Runtime::RuntimeEngineConfigApplyStatus::Applied};
    };
    context.AttachmentActive = [&] { return attached; };
    const auto commands = Runtime::BindEditorProcessingCommands(context);
    const auto previewAction = [&](const auto& handle, const Runtime::ClusteringService* candidate) {
        return Runtime::ResolveEditorProcessingActionReadiness(handle,
            Runtime::PreviewEditorKMeansRun(handle, candidate, request));
    };
    const auto configUnavailable = Runtime::ResolveEditorProcessingActionReadiness({}, {});
    const auto ready = previewAction(commands, service);
    ASSERT_TRUE(ready.Enabled);
    EXPECT_TRUE(ready.DisabledReason.empty());

    for (unsigned missing = 0; missing < 5; ++missing)
    {
        SCOPED_TRACE(missing);
        auto incomplete = context;
        if (missing == 0) incomplete.EngineConfigControlState = nullptr;
        if (missing == 1) incomplete.EngineConfigCommandsAvailable = false;
        if (missing == 2) incomplete.PreviewEngineConfigDocument = {};
        if (missing == 3) incomplete.ApplyEngineConfigHotSubset = {};
        if (missing == 4) incomplete.AttachmentActive = [] { return false; };
        const auto handle = Runtime::BindEditorProcessingCommands(incomplete);
        for (auto* candidate : {service, static_cast<Runtime::ClusteringService*>(nullptr)})
        {
            const auto blocked = previewAction(handle, candidate);
            EXPECT_FALSE(blocked.Enabled);
            EXPECT_EQ(blocked.DisabledReason, configUnavailable.DisabledReason);
        }
        if (missing < 4)
            EXPECT_TRUE(Runtime::PreviewEditorKMeansRun(handle, service, request).Enabled);
        EXPECT_FALSE(Runtime::ApplyEditorClusteringConfig(handle, {}).Succeeded());
    }

    Runtime::ClusteringService unavailableService;
    for (auto* candidate : {&unavailableService, static_cast<Runtime::ClusteringService*>(nullptr)})
    {
        const auto blocked = previewAction(commands, candidate);
        EXPECT_FALSE(blocked.Enabled);
        EXPECT_EQ(blocked.DisabledReason,
                  Runtime::SubmitKMeansRun(commands, candidate, request).Message);
    }

    request.Parameters.ClusterCount = 0u;
    const auto rejected = Runtime::ValidateKMeansRequest(&scene->Raw(), request);
    ASSERT_TRUE(rejected);
    const auto invalid = previewAction(commands, service);
    EXPECT_FALSE(invalid.Enabled);
    EXPECT_EQ(invalid.DisabledReason, rejected->Message);
    EXPECT_EQ(Runtime::SubmitKMeansRun(commands, service, request).Message, rejected->Message);
    request.Parameters.ClusterCount = 2u;
    // Admission reads metadata; numerical validation still belongs to execution.
    properties.Get<glm::vec3>(std::string{PN::kPosition}).Vector()[1].x =
        std::numeric_limits<float>::infinity();
    EXPECT_TRUE(previewAction(commands, service).Enabled);
    EXPECT_EQ(previews, 0u);
    EXPECT_EQ(applies, 0u);
    EXPECT_EQ(service->Stats().CommandsHandled, 0u);
    EXPECT_EQ(service->Stats().JobsSubmitted, 0u);
    EXPECT_FALSE(properties.Exists(request.Properties.OutputLabels.Name));

    properties.Get<glm::vec3>(std::string{PN::kPosition}).Vector()[1].x = 1.0f;
    auto withoutConfig = context;
    withoutConfig.EngineConfigControlState = nullptr;
    const auto submission = Runtime::SubmitKMeansRun(
        Runtime::BindEditorProcessingCommands(withoutConfig), service, request);
    EXPECT_EQ(submission.Status, Runtime::KMeansRunStatus::Queued);
    EXPECT_TRUE(submission.Correlation.IsValid());

    attached = false;
    const auto expired = previewAction(commands, service);
    EXPECT_FALSE(expired.Enabled);
    EXPECT_EQ(expired.DisabledReason, configUnavailable.DisabledReason);
    EXPECT_FALSE(Runtime::ApplyEditorClusteringConfig(commands, {}).Succeeded());
    EXPECT_EQ(Runtime::SubmitKMeansRun(commands, service, request).Status,
              Runtime::KMeansRunStatus::ModuleUnavailable);
    EXPECT_EQ(previews, 0u);
    EXPECT_EQ(applies, 0u);
    engine.Shutdown();
    EXPECT_EQ(previewAction(commands, service).DisabledReason,
              configUnavailable.DisabledReason);
}

TEST(ClusteringModule, MetadataAdmissionRejectsInvalidBindingsAndSourcesWithoutScanningValues)
{
    ECS::Scene::Registry scene;
    const auto entity = AddPointCloud(scene, {{0, 0, 0}, {1, 0, 0}});
    auto request = MakePointCloudRequest(Runtime::SelectionController::ToStableEntityId(entity));
    auto& properties = scene.Raw().get<GS::Vertices>(entity).Properties;
    const auto expectRejected = [&](Runtime::KMeansRunStatus status) {
        const auto result = Runtime::ValidateKMeansRequest(&scene.Raw(), request);
        ASSERT_TRUE(result);
        EXPECT_EQ(result->Status, status);
        EXPECT_EQ(result->StableEntityId, request.StableEntityId);
        EXPECT_EQ(result->RequestedBackend, request.Backend);
        EXPECT_EQ(result->ActualBackend, Runtime::ClusteringBackend::None);
        EXPECT_FALSE(result->Message.empty());
        EXPECT_FALSE(result->Correlation.IsValid());
        EXPECT_FALSE(properties.Exists(request.Properties.OutputLabels.Name));
    };
    EXPECT_FALSE(Runtime::ValidateKMeansRequest(&scene.Raw(), request));
    request.Parameters.ClusterCount = 0u;
    EXPECT_EQ(Runtime::ValidateKMeansRequest(nullptr, request)->Status,
              Runtime::KMeansRunStatus::MissingScene);
    request.StableEntityId = 0u;
    expectRejected(Runtime::KMeansRunStatus::InvalidProcessingParameters);
    request.Parameters.ClusterCount = 2u;
    expectRejected(Runtime::KMeansRunStatus::StaleEntity);
    request.StableEntityId = Runtime::SelectionController::ToStableEntityId(entity);
    request.Properties.InputPositions.Name = "p:custom";
    expectRejected(Runtime::KMeansRunStatus::UnsupportedGeometryDomain);
    auto input = properties.GetOrAdd<glm::vec3>("p:custom", {});
    input.Vector() = {{0, 0, 0}, {std::numeric_limits<float>::infinity(), 0, 0}};
    EXPECT_FALSE(Runtime::ValidateKMeansRequest(&scene.Raw(), request));
    input.Vector().pop_back();
    expectRejected(Runtime::KMeansRunStatus::InvalidProcessingParameters);
    input.Vector().push_back({1, 0, 0});
    auto conflict = properties.GetOrAdd<float>(request.Properties.OutputColors.Name, 0);
    expectRejected(Runtime::KMeansRunStatus::InvalidProcessingParameters);
    EXPECT_EQ(Runtime::ValidateKMeansRequest(&scene.Raw(), request)->Error,
              Extrinsic::Core::ErrorCode::TypeMismatch);
    properties.Remove(conflict);
    EXPECT_FALSE(Runtime::ValidateKMeansRequest(&scene.Raw(), request));
    properties.Remove(input);
    expectRejected(Runtime::KMeansRunStatus::UnsupportedGeometryDomain);
}

namespace
{
    class KMeansElementDomainApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit KMeansElementDomainApp(Runtime::GeometryElementDomain domain, bool rejectConversion = false)
            : Domain(domain), RejectConversion(rejectConversion) {}
        void Resolve() override
        {
            auto& engine = Kernel();
            auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
            Entity = scene.Create();
            auto& raw = scene.Raw();
            auto& vertices = raw.emplace<GS::Vertices>(Entity);
            SetPositions(vertices, std::vector<glm::vec3>(6, {0, 0, 0}));
            const bool mesh = Domain >= Runtime::GeometryElementDomain::MeshVertex &&
                              Domain <= Runtime::GeometryElementDomain::MeshFace;
            const bool graph = Domain >= Runtime::GeometryElementDomain::GraphNode &&
                               Domain <= Runtime::GeometryElementDomain::GraphEdge;
            if (mesh || graph)
            {
                raw.emplace<GS::Edges>(Entity).Properties.Resize(6);
                raw.emplace<GS::Halfedges>(Entity).Properties.Resize(12);
            }
            if (mesh) raw.emplace<GS::Faces>(Entity).Properties.Resize(6);
            if (graph) raw.emplace<GS::HasGraphTopology>(Entity);
            auto& props = Properties(scene);
            auto samples = props.GetOrAdd<glm::vec3>("samples", {});
            for (std::size_t i = 0; i < props.Size(); ++i)
                samples[i] = {static_cast<float>(i / 2) * 10.f, static_cast<float>(i % 2), 0.f};
            auto& deletion = (Domain == Runtime::GeometryElementDomain::MeshHalfedge ||
                              Domain == Runtime::GeometryElementDomain::GraphHalfedge)
                ? raw.get<GS::Edges>(Entity).Properties : props;
            const char* deletionName = (Domain == Runtime::GeometryElementDomain::MeshFace) ? "f:deleted" :
                (Domain == Runtime::GeometryElementDomain::MeshEdge || Domain == Runtime::GeometryElementDomain::GraphEdge ||
                 Domain == Runtime::GeometryElementDomain::MeshHalfedge || Domain == Runtime::GeometryElementDomain::GraphHalfedge)
                    ? "e:deleted" : "v:deleted";
            deletion.GetOrAdd<bool>(deletionName, false)[0] = true;
            props.GetOrAdd<std::uint64_t>("labels", 9223372036854775809ull);
            props.GetOrAdd<glm::vec4>("colors", glm::vec4(0.25f));
            if (RejectConversion) props.GetOrAdd<bool>("scalar_labels", true);
            else props.GetOrAdd<double>("scalar_labels", -3.0);
            auto* service = engine.Services().Find<Runtime::ClusteringService>();
            Subscription = service->SubscribeRunCompleted([this](const auto& completion) { Completion = completion; });
            auto command = MakePointCloudRequest(Runtime::SelectionController::ToStableEntityId(Entity));
            command.Properties = Runtime::MakeKMeansPropertyRefs(Domain);
            command.Properties.InputPositions.Name = "samples";
            command.Properties.OutputLabels = {Domain, "labels", Geometry::PropertyValueKind::UInt64};
            command.Properties.OutputColors.Name = "colors";
            command.Properties.OutputScalarLabels = Runtime::GeometryPropertyRef{Domain, "scalar_labels",
                RejectConversion ? Geometry::PropertyValueKind::Bool : Geometry::PropertyValueKind::Double};
            command.Parameters.ClusterCount = RejectConversion ? 3u : 2u;
            command.Parameters.Initialization = Runtime::KMeansInitialization::Hierarchical;
            (void)service->RunKMeans(command);
        }
        Geometry::PropertySet& Properties(ECS::Scene::Registry& scene)
        {
            auto& raw = scene.Raw();
            switch (Domain)
            {
            case Runtime::GeometryElementDomain::MeshFace: return raw.get<GS::Faces>(Entity).Properties;
            case Runtime::GeometryElementDomain::MeshEdge:
            case Runtime::GeometryElementDomain::GraphEdge: return raw.get<GS::Edges>(Entity).Properties;
            case Runtime::GeometryElementDomain::MeshHalfedge:
            case Runtime::GeometryElementDomain::GraphHalfedge: return raw.get<GS::Halfedges>(Entity).Properties;
            default: return raw.get<GS::Vertices>(Entity).Properties;
            }
        }
        void Frame(double, double) override
        {
            if (Completion || ++Ticks > 240u) Kernel().RequestExit();
        }
        void Shutdown() override {}
        Runtime::GeometryElementDomain Domain;
        bool RejectConversion;
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription Subscription{};
        std::optional<Runtime::KMeansRunCompleted> Completion;
        unsigned Ticks{};
    };
}

TEST(ClusteringModule, EveryPointDomainPreservesDeletedSlotsAndExactScalarUndo)
{
    for (const auto domain : {Runtime::GeometryElementDomain::MeshVertex, Runtime::GeometryElementDomain::MeshEdge,
                             Runtime::GeometryElementDomain::MeshHalfedge, Runtime::GeometryElementDomain::MeshFace,
                             Runtime::GeometryElementDomain::GraphNode, Runtime::GeometryElementDomain::GraphEdge,
                             Runtime::GeometryElementDomain::GraphHalfedge, Runtime::GeometryElementDomain::PointCloudPoint})
    {
        SCOPED_TRACE(static_cast<int>(domain));
        auto app = std::make_unique<KMeansElementDomainApp>(domain);
        auto* state = app.get();
        Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));
        engine.EmplaceModule<Runtime::ClusteringModule>();
        engine.EmplaceModule<Runtime::SceneDocumentModule>();
        engine.Initialize();
        engine.Run();
        ASSERT_TRUE(state->Completion);
        ASSERT_TRUE(state->Completion->Succeeded()) << state->Completion->Message;
        auto& props = state->Properties(*engine.Worlds().Get(engine.ActiveWorld()));
        const auto labels = props.Get<std::uint64_t>("labels").Vector();
        EXPECT_EQ(labels[0], 9223372036854775809ull);
        EXPECT_EQ(props.Get<double>("scalar_labels")[0], -3.0);
        EXPECT_EQ(props.Get<glm::vec4>("colors")[0], glm::vec4(0.25f));
        const std::size_t firstLive = domain == Runtime::GeometryElementDomain::MeshHalfedge ||
                                      domain == Runtime::GeometryElementDomain::GraphHalfedge ? 2 : 1;
        EXPECT_LT(labels[firstLive], 2u);
        EXPECT_EQ(props.Get<double>("scalar_labels")[firstLive], static_cast<double>(labels[firstLive]));
        auto* history = engine.Services().Find<Runtime::EditorCommandHistory>();
        ASSERT_NE(history, nullptr);
        ASSERT_TRUE(history->Undo().Succeeded());
        EXPECT_EQ(props.Get<std::uint64_t>("labels")[firstLive], 9223372036854775809ull);
        EXPECT_EQ(props.Get<double>("scalar_labels")[firstLive], -3.0);
        ASSERT_TRUE(history->Redo().Succeeded());
        EXPECT_EQ(props.Get<std::uint64_t>("labels").Vector(), labels);
        engine.Shutdown();
    }
}

TEST(ClusteringModule, UnrepresentableScalarLabelRejectsAllOutputPublication)
{
    auto app = std::make_unique<KMeansElementDomainApp>(Runtime::GeometryElementDomain::MeshFace, true);
    auto* state = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();
    ASSERT_TRUE(state->Completion);
    EXPECT_FALSE(state->Completion->Succeeded());
    auto& props = state->Properties(*engine.Worlds().Get(engine.ActiveWorld()));
    for (std::size_t i = 0; i < props.Size(); ++i)
    {
        EXPECT_EQ(props.Get<std::uint64_t>("labels")[i], 9223372036854775809ull);
        EXPECT_TRUE(props.Get<bool>("scalar_labels")[i]);
        EXPECT_EQ(props.Get<glm::vec4>("colors")[i], glm::vec4(0.25f));
    }
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->UndoCount(), 0u);
    engine.Shutdown();
}


TEST(ClusteringModule, ScalarOutputKindsAndPointDomainsRoundTripCanonically)
{
    for (const auto kind : {Geometry::PropertyValueKind::Bool, Geometry::PropertyValueKind::Int32,
                           Geometry::PropertyValueKind::UInt32, Geometry::PropertyValueKind::UInt64,
                           Geometry::PropertyValueKind::Float, Geometry::PropertyValueKind::Double})
    {
        Runtime::ClusteringConfig config;
        config.Properties = Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::MeshFace);
        config.Properties->InputPositions.Name = "centers";
        config.Properties->OutputLabels.ValueKind = kind;
        config.Properties->OutputScalarLabels->ValueKind = kind;
        CoreConfig::EngineConfig engine;
        Runtime::SetClusteringConfig(engine, config);
        const auto decoded = Runtime::GetClusteringConfig(engine);
        ASSERT_TRUE(decoded);
        ASSERT_TRUE(decoded->Properties);
        EXPECT_EQ(decoded->Properties->OutputLabels, config.Properties->OutputLabels);
        EXPECT_EQ(decoded->Properties->OutputScalarLabels, config.Properties->OutputScalarLabels);
    }
}

namespace
{
    class ClusteringModuleResident : public ::testing::Test
    {
    protected:
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        std::unique_ptr<Extrinsic::Graphics::IRenderer> Renderer{Extrinsic::Graphics::CreateRenderer()};
        Runtime::WorldRegistry Worlds;
        Runtime::CommandBus Commands;
        Runtime::KernelEventBus Events;
        Runtime::ServiceRegistry Services;
        Runtime::SpatialIndexCache Cache;
        Runtime::ClusteringModule Module;
        Runtime::EditorCommandHistory History;
        Runtime::WorldHandle World;
        ECS::EntityHandle Entity;
        Runtime::ClusteringService* Service{};
        Runtime::RunKMeans Request;
        Runtime::CommandCorrelationId Correlation;
        Runtime::KernelEventSubscription Subscription;
        std::vector<Runtime::KMeansRunCompleted> Results;
        std::vector<std::function<void()>> Completions;
        std::optional<Extrinsic::RHI::ReadbackSink> Held;
        std::vector<std::byte> HeldData;
        bool Hold{}, Attached{true}, Converged{};
        std::uint32_t MaximumDispatches{};
        Runtime::KMeansPagingLimits SavedLimits{Runtime::KMeansPagingForTesting};
        auto& Scene(){return *Worlds.Get(World);}
        auto& Properties(){return Scene().Raw().get<GS::Vertices>(Entity).Properties;}
        void SetUp()override
        {
            World=Worlds.CreateWorld("resident-kmeans");Entity=Scene().Create();
            auto& vertices=Scene().Raw().emplace<GS::Vertices>(Entity);
            SetPositions(vertices,std::vector<glm::vec3>(12,glm::vec3(1,2,3)));
            Device.TransferQueue.AcceptBufferUploads=true;Device.ShaderFloat64=true;Renderer->Initialize(Device);
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device,"test"));
            ASSERT_TRUE(Services.Provide<Extrinsic::Graphics::IRenderer>(*Renderer,"test"));
            ASSERT_TRUE(Services.Provide<Runtime::EditorCommandHistory>(History,"test"));
            Runtime::EngineSetup setup{Commands,Events,Jobs.Jobs(),Worlds,Services,[](Runtime::FramePhase,Runtime::RuntimeFrameHook){}};
            ASSERT_TRUE(Cache.OnRegister(setup));ASSERT_TRUE(Module.OnRegister(setup));
            Services.BeginResolution();ASSERT_TRUE(Cache.OnResolve(setup));ASSERT_TRUE(Module.OnResolve(setup));Services.Lock();
            Service=Services.Find<Runtime::ClusteringService>();ASSERT_NE(Service,nullptr);
            Subscription=Service->SubscribeRunCompleted([this](const auto& r){Results.push_back(r);});
            Request.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entity);
            Request.Properties=Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint);
            Request.Parameters.ClusterCount=3;Request.Parameters.MaxIterations=4;Request.Parameters.GpuPreviewInterval=2;
            Request.Backend=Runtime::ClusteringBackend::VulkanCompute;Request.AutoAccept=false;
            Request.AttachmentActive=[this]{return Attached;};
            Device.ComputeReadback=[this](auto record,auto bytes,auto sink){
                const auto before=Device.CommandContext.DispatchCalls;
                const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
                EXPECT_TRUE(record(Device.CommandContext).IsValid());
                std::uint64_t pairs=0;
                std::uint32_t serialDepth=0;
                for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i){
                    const auto& payload=Device.CommandContext.PushConstantPayloads[i];
                    EXPECT_EQ(payload.size(),128u);
                    if(payload.size()!=128u)continue;
                    std::uint32_t rows{},columns{},phase{};
                    std::memcpy(&phase,payload.data()+104,4);
                    std::memcpy(&rows,payload.data()+112,4);std::memcpy(&columns,payload.data()+120,4);
                    const auto pagePairs=std::uint64_t(rows)*columns;
                    EXPECT_LE(pagePairs,Runtime::KMeansPagingForTesting.PagePairs);pairs+=pagePairs;
                    serialDepth+=phase==1?columns+1:phase==2?(columns+63)/64+7:phase==3?(rows+63)/64+7:1;
                }
                EXPECT_LE(pairs,Runtime::KMeansPagingForTesting.SubmissionPairs);
                EXPECT_LE(serialDepth,Runtime::KMeansSubmissionSerialDepth);
                MaximumDispatches=std::max(MaximumDispatches,std::uint32_t(Device.CommandContext.DispatchCalls-before));
                std::vector<std::byte> data(bytes);
                if(bytes==32){
                    std::array<std::uint32_t,6> diagnostics{};
                    diagnostics[2]=std::bit_cast<std::uint32_t>(1.0f);diagnostics[4]=!Converged;
                    std::memcpy(data.data(),diagnostics.data(),24);
                }
                if(Hold){Held=std::move(sink);HeldData=std::move(data);return Extrinsic::RHI::ReadbackToken{2};}
                Completions.push_back([sink=std::move(sink),data=std::move(data)]()mutable{sink.Deliver(data);});
                return Extrinsic::RHI::ReadbackToken{2};
            };
            Device.TransferQueue.BufferDownload=[this](auto,auto bytes,auto,auto sink){
                Completions.push_back([sink=std::move(sink),bytes]()mutable{std::vector<std::byte> data(bytes);sink.Deliver(data);});
                return Extrinsic::RHI::ReadbackToken{3};
            };
        }
        void Tick()
        {
            auto completions=std::exchange(Completions,{});for(auto& complete:completions)complete();
            Commands.Drain(Scene(),{.Events=&Events,.Jobs=&Jobs.Jobs(),.Worlds=&Worlds});
            (void)Jobs.Jobs().DrainCompletions(Events);(void)Events.Pump();
            Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);++Device.GlobalFrameNumber;
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
        }
        template<class P>bool Until(P predicate)
        {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!predicate()&&std::chrono::steady_clock::now()<end){Tick();std::this_thread::yield();}
            return predicate();
        }
        auto Observation(){return Service->GpuRun(Correlation);}
        void Start(){Correlation=Service->RunKMeans(Request);}
    public:
        void DiscardDuringPublication(entt::registry&, entt::entity)
        { (void)Service->GpuRun(Correlation, Runtime::KMeansGpuAction::Discard); }
    protected:
        void TearDown()override
        {
            if(Held){Held->Deliver(HeldData);Held.reset();}
            if(Service){(void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);Service->Unsubscribe(Subscription);}
            for(int i=0;i<5;++i)Tick();
            Jobs.Jobs().CancelAndDrain();(void)Jobs.Jobs().ShutdownGpuQueueParticipants([this]{Device.WaitIdle();});
            Runtime::RuntimeModuleShutdownContext shutdown{Commands,Events,Jobs.Jobs(),Worlds,Services};
            Module.OnShutdown(shutdown);Cache.OnShutdown(shutdown);Services.Reset();Renderer->Shutdown();
            Runtime::KMeansPagingForTesting=SavedLimits;
        }
    };
}
TEST_F(ClusteringModuleResident, ResidentInputWaitsForCompletionAndRepeatRunUploadsZero)
{
    Hold=true;Start();ASSERT_TRUE(Until([&]{return Held.has_value();}));
    EXPECT_EQ(Observation().InputUploadBytes,12u*12u);
    const auto submissions=Observation().Submissions;for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Observation().Submissions,submissions);
    Hold=false;Held->Deliver(HeldData);Held.reset();
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Observation().Iterations,4u);EXPECT_EQ(Observation().Previews,2u);
    // One submission per iteration (the interval preview shares iteration 3's)
    // plus the terminal preview, which also reads the centroids back.
    EXPECT_EQ(Observation().Submissions,5u);
    EXPECT_EQ(Observation().CpuStageUploadBytes,3u*12u);
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Observation().InputUploadBytes,0u);EXPECT_GT(Observation().InputCacheHits,0u);
}
TEST_F(ClusteringModuleResident, RetainedWorkspaceReusesCapacityAndUnbindsSlots)
{
    const auto run=[&](std::size_t results){
        Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
        (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
        ASSERT_TRUE(Until([&]{return Results.size()==results;}));
    };
    run(1);
    EXPECT_GT(Results[0].GpuWorkspaceBuffersCreated,0u);EXPECT_EQ(Results[0].GpuWorkspacePipelinesCreated,3u);
    Properties().GetOrAdd<bool>("v:deleted",false)[4]=true;
    run(2);
    // Eleven live rows fit the retained buffers; only the live-slot map is new.
    EXPECT_EQ(Results[1].GpuWorkspaceBuffersCreated,1u);EXPECT_EQ(Results[1].GpuWorkspacePipelinesCreated,0u);
    Properties().GetOrAdd<bool>("v:deleted",false)[4]=false;
    const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
    run(3);
    EXPECT_EQ(Results[2].GpuWorkspaceBuffersCreated,0u);EXPECT_EQ(Results[2].GpuWorkspacePipelinesCreated,0u);
    std::size_t pushes=0;
    for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i){
        const auto& payload=Device.CommandContext.PushConstantPayloads[i];
        if(payload.size()!=128u)continue;
        std::uint64_t slots{};std::memcpy(&slots,payload.data()+16,8);
        EXPECT_EQ(slots,0u)<<"an all-live run must not bind the retained slot map";++pushes;
    }
    EXPECT_GT(pushes,0u);
}
TEST_F(ClusteringModuleResident, MultiPageSubmissionsAndConvergence)
{
    Runtime::KMeansPagingForTesting={.PagePairs=3,.SubmissionPairs=12};
    Converged=true;Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_GT(MaximumDispatches,1u);EXPECT_GT(Observation().Submissions,5u);
    EXPECT_EQ(Observation().Iterations,1u);EXPECT_EQ(Observation().Previews,1u);
}
TEST_F(ClusteringModuleResident, StaleAcceptRefusedAndDetachDiscards)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    Properties().Get<glm::vec3>("v:position")[0].x=9;
    EXPECT_FALSE(Observation().CanAccept);EXPECT_FALSE(Observation().Message.empty());
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);EXPECT_TRUE(Results.empty());
    Attached=false;ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
TEST_F(ClusteringModuleResident, StopAcceptIsExactlyOnceAndUndoable)
{
    Request.Properties.OutputScalarLabels=Runtime::GeometryPropertyRef{
        .Domain=Runtime::GeometryElementDomain::PointCloudPoint,.Name="scalar_labels",.ValueKind=Geometry::PropertyValueKind::Double};
    Start();ASSERT_TRUE(Until([&]{return Observation().Submissions>0;}));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Stop);
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));EXPECT_EQ(Observation().Iterations,1u);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    EXPECT_TRUE(Properties().Exists("p:kmeans_label"));EXPECT_TRUE(Properties().Exists("p:kmeans_color"));
    EXPECT_TRUE(Properties().Exists("scalar_labels"));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    for(int i=0;i<5;++i)Tick();EXPECT_EQ(Results.size(),1u);
    ASSERT_TRUE(History.Undo().Succeeded());EXPECT_FALSE(Properties().Exists("p:kmeans_label"));EXPECT_FALSE(Properties().Exists("p:kmeans_color"));
    EXPECT_FALSE(Properties().Exists("scalar_labels"));
}
TEST_F(ClusteringModuleResident, BatchAutoAccept)
{
    Request.AutoAccept=true;Start();ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;EXPECT_GT(Results[0].CpuStageReadbackBytes,0u);
    EXPECT_EQ(Results[0].ImplementationId,"vulkan_resident_paged_lloyd");
}
TEST_F(ClusteringModuleResident, PreviewCopiesOnlyAtCadenceAndPublishesAfterCompletion)
{
    const auto key=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
    auto* residency=Cache.PropertyResidency();ASSERT_NE(residency,nullptr);
    const auto submit=Device.ComputeReadback;
    bool heldOnce=false;
    std::uint64_t copiedBack{};
    std::vector<std::uint32_t> phases{};
    Device.ComputeReadback=[&](auto record,auto bytes,auto sink){
        // The interval preview is recorded in the submission after iteration 2.
        const bool boundary=!heldOnce&&Observation().Iterations==2;
        Hold=boundary;heldOnce|=boundary;
        const auto token=submit([&](auto& cmd){
            const auto count=Device.CommandContext.CopyBufferRecords.size();
            const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
            const auto result=record(cmd);
            if(boundary){
                EXPECT_EQ(Device.CommandContext.CopyBufferRecords.size(),count);
                for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i){
                    const auto& push=Device.CommandContext.PushConstantPayloads[i];
                    std::uint32_t phase{};std::memcpy(&phase,push.data()+104,4);
                    if(phases.empty()||phases.back()!=phase)phases.push_back(phase);
                    // Phase 4 scatters every live label; all-live previews need no copy.
                    if(phase==4)std::memcpy(&copiedBack,push.data()+80,8);
                }
                EXPECT_FALSE(residency->Front(key));
            }
            return result;
        },bytes,std::move(sink));
        Hold=false;return token;
    };
    Start();ASSERT_TRUE(Until([&]{return Held.has_value();}));ASSERT_NE(copiedBack,0u);
    // Preview scatter and presentation share the submission with iteration 3:
    // Assign(1), Reduce(3), Update(2), stopping at its diagnostics boundary.
    EXPECT_EQ(phases,(std::vector<std::uint32_t>{4u,5u,1u,3u,2u}));
    EXPECT_EQ(Observation().Previews,0u);for(int i=0;i<5;++i)Tick();EXPECT_FALSE(residency->Front(key));
    Held->Deliver(HeldData);Held.reset();
    ASSERT_TRUE(Until([&]{return Observation().Previews==1;}));
    ASSERT_TRUE(residency->Front(key));EXPECT_EQ(residency->Front(key)->Address,copiedBack);
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Device.CommandContext.CopyBufferRecords.size(),0u);
}
TEST_F(ClusteringModuleResident, ReentrantDiscardDuringAcceptKeepsOneAppliedCompletion)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    Scene().Raw().on_construct<Dirty::DirtyVertexAttributes>().connect<&ClusteringModuleResident::DiscardDuringPublication>(*this);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    Scene().Raw().on_construct<Dirty::DirtyVertexAttributes>().disconnect<&ClusteringModuleResident::DiscardDuringPublication>(*this);
    EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    for(int i=0;i<10;++i)Tick();EXPECT_EQ(Results.size(),1u);
    EXPECT_TRUE(Properties().Exists("p:kmeans_label"));ASSERT_TRUE(History.Undo().Succeeded());
}
TEST_F(ClusteringModuleResident, DiscardPendingAcceptDeliversOnce)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    Hold=true;(void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Held.has_value();}));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    Hold=false;std::vector<std::byte> bytes(12*4);Held->Deliver(bytes);Held.reset();
    for(int i=0;i<10;++i)Tick();EXPECT_EQ(Results.size(),1u);EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
TEST(ClusteringModule, AgentRunAutoAcceptsAndReportsBackendAndIo)
{
    auto config=NullWindowHeadlessConfig();
    Runtime::ClusteringConfig clustering;
    clustering.Backend=Runtime::ClusteringBackend::VulkanCompute;
    Runtime::SetClusteringConfig(config,clustering);
    Intrinsic::Tests::RuntimeTestKernel engine{std::move(config)};
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    CoreConfig::EngineConfigSectionRegistry sections;
    ASSERT_TRUE(sections.Register(Runtime::MakeClusteringConfigSectionRegistration()));
    engine.EmplaceModule<Runtime::EngineConfigControl>(std::move(sections));engine.Initialize();
    auto* scene=engine.Worlds().Get(engine.ActiveWorld());ASSERT_NE(scene,nullptr);
    const auto entity=scene->Create();auto& vertices=scene->Raw().emplace<GS::Vertices>(entity);
    SetPositions(vertices,{{0,0,0},{1,0,0},{3,0,0}});
    Runtime::EditorWorkspaceAttachment attachment;attachment.Attach(engine.Worlds(),engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
    Runtime::AgentOperationRegistry registry;Runtime::RegisterEditorAgentOperations(registry);
    const Runtime::AgentOperationContext context{.Attachment=&attachment};
    auto outcome=Runtime::InvokeAgentOperation(registry,"run_kmeans",context,
        "{\"entity\":"+std::to_string(Runtime::SelectionController::ToStableEntityId(entity))+",\"domain\":\"PointCloudPoint\"}",false);
    ASSERT_TRUE(outcome.Continuation)<<outcome.Text;
    Runtime::AgentOperationOutcome completed;bool done=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!done&&std::chrono::steady_clock::now()<deadline){
        engine.Commands().Drain(*scene,{&engine.Events(),&engine.Jobs(),&engine.Worlds()});
        (void)engine.Jobs().DrainCompletions(engine.Events());(void)engine.Events().Pump();
        done=outcome.Continuation(context,completed);std::this_thread::yield();
    }
    EXPECT_TRUE(done);EXPECT_FALSE(completed.IsError)<<completed.Text;
    for(const auto key:{"gpu_input_upload_bytes","gpu_input_cache_hits","cpu_stage_upload_bytes","cpu_stage_readback_bytes","implementation_id"})
        EXPECT_NE(completed.Text.find(key),std::string::npos);
    EXPECT_TRUE(vertices.Properties.Exists("p:kmeans_label"));
    outcome.Continuation={};attachment.Detach();engine.Shutdown();
}
TEST_F(ClusteringModuleResident, BusyAdmissionHasOneRefusedCompletionAndPreservesPendingRun)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    const auto second=Service->RunKMeans(Request);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Correlation,second);EXPECT_FALSE(Results[0].Succeeded());
    EXPECT_TRUE(Observation().CanAccept);
    for(int i=0;i<5;++i)Tick();EXPECT_EQ(Results.size(),1u);
}
TEST_F(ClusteringModuleResident, DeletedSlotsUseDeclaredMapAndPreservePriorLabels)
{
    Properties().GetOrAdd<bool>("v:deleted",false)[1]=true;
    (void)Properties().GetOrAdd<std::uint32_t>("p:kmeans_label",9u);
    Request.AutoAccept=true;Start();ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    ASSERT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    EXPECT_EQ(Results[0].LabelCount,11u);
    EXPECT_EQ(Results[0].CpuStageUploadBytes,3u*12u+11u*4u);
    EXPECT_EQ(std::as_const(Properties()).Get<std::uint32_t>("p:kmeans_label")[1],9u);
    ASSERT_TRUE(History.Undo().Succeeded());
    for(auto label:std::as_const(Properties()).Get<std::uint32_t>("p:kmeans_label").Vector())EXPECT_EQ(label,9u);
}

TEST_F(ClusteringModuleResident, FailedRecordedPageRequiresIdleAtShutdown)
{
    Device.ComputeReadback=[&](auto record,auto,auto){
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        return Extrinsic::RHI::ReadbackToken{};
    };
    Start();ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::GeometryProcessingFailed);
    EXPECT_EQ(Service->Stats().GpuCompletions,0u);
    unsigned waits=0;
    (void)Jobs.Jobs().ShutdownGpuQueueParticipants([&]{++waits;Device.WaitIdle();});
    EXPECT_EQ(waits,1u);
}
TEST_F(ClusteringModuleResident, AutoAcceptRefusalCompletesStaleAndAllowsNextRun)
{
    bool arm=false;unsigned checks=0;
    Request.AutoAccept=true;
    Request.AttachmentActive=[&]{
        if(arm&&++checks==3)Properties().Get<glm::vec3>("v:position")[0].x=9;
        return true;
    };
    const auto submit=Device.ComputeReadback;
    Device.ComputeReadback=[&](auto record,auto bytes,auto sink){
        const auto token=submit(std::move(record),bytes,std::move(sink));
        if(bytes==3*12)Completions.push_back([&]{arm=true;});
        return token;
    };
    Start();ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::StaleSource);
    EXPECT_EQ(Service->Stats().GpuCompletions,0u);
    arm=false;Request.AttachmentActive={};Start();
    ASSERT_TRUE(Until([&]{return Results.size()==2;}));EXPECT_TRUE(Results[1].Succeeded());
    EXPECT_EQ(Service->Stats().GpuCompletions,1u);
}
TEST_F(ClusteringModuleResident, StaleDuringAcceptReadbackReportsStaleSource)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    Properties().Get<glm::vec3>("v:position")[0].x=7;
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::StaleSource);
}
TEST_F(ClusteringModuleResident, AdmissionReservesTypedAndPresentationAgainstScalarOwners)
{
    Start();Tick();
    auto* residency=Cache.PropertyResidency();ASSERT_NE(residency,nullptr);
    const auto typed=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
    const auto presentation=Runtime::MakeGpuPropertyKey(World,Entity,Runtime::GpuPropertyPresentationRef(Request.Properties.OutputLabels));
    ASSERT_TRUE(residency->HasRing(typed));ASSERT_TRUE(residency->HasRing(presentation));
    const auto generation=residency->RingGeneration(presentation);
    Runtime::EditorProcessingContext context;
    context.Scene=&Scene();context.World=World;context.Device=&Device;context.SpatialIndices=&Cache;
    Jobs.Attach(context);
    Runtime::KernelDensityConfig scalar;
    scalar.StableEntityId=Request.StableEntityId;scalar.Positions=Request.Properties.InputPositions;
    scalar.Density=Runtime::GpuPropertyPresentationRef(Request.Properties.OutputLabels);
    const auto commands=Runtime::BindEditorProcessingCommands(context);
    auto competing=Runtime::MakeEditorKernelDensityTransactionForTest(commands,scalar,std::vector<float>(12,3),*residency);
    EXPECT_FALSE(competing);EXPECT_EQ(residency->RingGeneration(presentation),generation);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    competing=Runtime::MakeEditorKernelDensityTransactionForTest(commands,scalar,std::vector<float>(12,3),*residency);
    ASSERT_TRUE(competing);
    Start();ASSERT_TRUE(Until([&]{return Results.size()==2;}));EXPECT_FALSE(Results[1].Succeeded());
    EXPECT_TRUE(Runtime::SnapshotEditorPointScalar(commands,competing).CanAccept);
    Runtime::DiscardEditorPointScalar(commands,competing);
}
TEST_F(ClusteringModuleResident, TerminalPreviewRetriesAreBounded)
{
    Request.Parameters.GpuPreviewInterval=1;
    Start();
    std::vector<Extrinsic::Graphics::GpuPropertyView> leases;
    std::uint64_t publication=0;
    ASSERT_TRUE(Until([&]{
        const auto key=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
        if(auto front=Cache.PropertyResidency()->Front(key);front&&front->Publication!=publication){
            publication=front->Publication;leases.push_back(*front);
        }
        return Results.size()==1;
    }));
    EXPECT_EQ(leases.size(),3u);
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::GeometryProcessingFailed);
    EXPECT_NE(Results[0].Message.find("600 retries"),std::string::npos);
}

TEST_F(ClusteringModuleResident, MillionRowsRespectPairAndSerialBudgets)
{
    SetPositions(Scene().Raw().get<GS::Vertices>(Entity),std::vector<glm::vec3>(1u<<20,glm::vec3(1,2,3)));
    Request.Parameters.ClusterCount=8;Request.Parameters.MaxIterations=1;
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    // Fixture checks every recorded submission's pair and lane-depth totals.
    // Fused phases still split at the serial-depth budget and host boundary.
    EXPECT_EQ(Observation().Submissions,4u);
    EXPECT_LE(MaximumDispatches,Runtime::KMeansSubmissionSerialDepth);
}
