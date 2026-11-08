#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <tuple>
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
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
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
import Geometry.KMeans;

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
            .Properties = Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint),
            .Parameters =
                Runtime::KMeansParameters{
                    .ClusterCount = 2u,
                    .Seed = 13u,
                    .Execution = {.IterationCap = 8u,
                        .PreviewEvery = 5u,
                        .PreviewIntervalMs = 0u,
                        .AutoAccept = Runtime::StepRunAutoAccept::End,
                        .Start = Runtime::StepRunStart::Run},
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
            PositionsRevision = engine.Worlds().Get(engine.ActiveWorld())->Raw().get<GS::Vertices>(Entity)
                                    .Properties.Get<glm::vec3>(PN::kPosition).Revision();

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
                engine.Worlds().Get(engine.ActiveWorld())->Raw().all_of<Dirty::DirtyVertexColors>(
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
        Geometry::PropertyRevision PositionsRevision{};
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
                    JobCorrelation = kmeans->CorrelationId;
                    // The shared progress model finds the run by the correlation
                    // id of its submission: queued behind the blocker, not
                    // reported yet, so indeterminate rather than 0%.
                    if (!Attached)
                    {
                        Attachment.Attach(engine.Worlds(), engine.Services());
                        Attached = true;
                    }
                    Runtime::EditorWorkspaceSnapshotRequest request{};
                    request.Hierarchy = request.Inspector = request.Selection = request.Document = false;
                    request.SceneFile = request.FileImport = request.AssetImportQueue = false;
                    request.RenderGraph = request.RenderRecipe = request.CameraRender = request.Visualization = false;
                    PreparedFrame = Runtime::PrepareEditorWorkspaceSnapshotFrame(Attachment, request).has_value();
                    if (PreparedFrame)
                    {
                        const auto commands = Runtime::PrepareEditorProcessingCommands(Attachment);
                        CommandsBound = commands.IsBound();
                        ProgressBeforeAction = Runtime::GetEditorOperationProgress(
                            commands, Runtime::EditorRunCorrelation{Correlation.Value});
                    }
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
            Attachment.Detach();
            ReleaseBlocker.store(true, std::memory_order_release);
        }

        ControlledCompletionAction Action{};
        Runtime::KernelEventSubscription CompletionSub{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::JobToken Job{};
        std::uint64_t JobCorrelation{0u};
        Runtime::EditorWorkspaceAttachment Attachment{};
        bool Attached{false};
        bool PreparedFrame{false};
        bool CommandsBound{false};
        Runtime::EditorOperationProgress ProgressBeforeAction{};
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    // Only the colors changed for the renderer, and the positions kept their revision (ARCH-022 Q75).
    const auto& published = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    EXPECT_TRUE(published.all_of<Dirty::DirtyVertexColors>(appPtr->Entity));
    EXPECT_FALSE(published.all_of<Dirty::DirtyVertexAttributes>(appPtr->Entity));
    EXPECT_EQ(published.get<GS::Vertices>(appPtr->Entity).Properties.Get<glm::vec3>(PN::kPosition).Revision(),
              appPtr->PositionsRevision);
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    // Each point's color is its cluster's palette entry (ARCH-022 Q75: one entry per cluster, looked up per point).
    const std::array<glm::vec4, 2> palette{glm::vec4{0.95f, 0.3325f, 0.3325f, 1.0f},
                                           glm::vec4{0.3325f, 0.51268f, 0.95f, 1.0f}};
    ASSERT_EQ(publishedColors.size(), publishedLabels.size());
    for (std::size_t i = 0; i < publishedLabels.size(); ++i)
    {
        ASSERT_LT(publishedLabels[i], palette.size());
        for (int c = 0; c < 4; ++c)
            EXPECT_NEAR(publishedColors[i][c], palette[publishedLabels[i]][c], 1.0e-4f) << i;
    }
    const auto positionsRevision = properties.Get<glm::vec3>(PN::kPosition).Revision();
    EXPECT_EQ(positionsRevision, appPtr->PositionsRevision);

    scene->Raw().remove<Dirty::DirtyVertexColors>(
        appPtr->Entity);
    EXPECT_EQ(history->Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists("p:kmeans_label"));
    EXPECT_FALSE(properties.Exists("p:kmeans_color"));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexColors>(
        appPtr->Entity));
    EXPECT_FALSE(scene->Raw().all_of<Dirty::DirtyVertexAttributes>(appPtr->Entity));
    EXPECT_EQ(properties.Get<glm::vec3>(PN::kPosition).Revision(), positionsRevision);

    scene->Raw().remove<Dirty::DirtyVertexColors>(
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
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexColors>(
        appPtr->Entity));
    EXPECT_FALSE(scene->Raw().all_of<Dirty::DirtyVertexAttributes>(appPtr->Entity));
    EXPECT_EQ(properties.Get<glm::vec3>(PN::kPosition).Revision(), positionsRevision);

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

// ARCH-022 Q79 step 6: closing the scene document (it clears the registry in the same world) releases the capture the
// module keeps for the next request.
TEST(ClusteringModule, ClosingTheSceneDocumentReleasesTheKeptCapture)
{
    auto app = std::make_unique<KMeansSuccessApp>();
    KMeansSuccessApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(1u), std::move(app));
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();
    ASSERT_TRUE(appPtr->Completion.has_value());
    ASSERT_TRUE(appPtr->Completion->Succeeded()) << appPtr->Completion->Message;
    const auto* clustering = engine.Services().Find<Runtime::ClusteringService>();
    auto* documents = engine.Services().Find<Runtime::SceneDocumentModule>();
    ASSERT_NE(clustering, nullptr);
    ASSERT_NE(documents, nullptr);
    EXPECT_EQ(clustering->Stats().CachedCapturePoints, appPtr->Completion->LabelCount) << "kept for the next request";
    ASSERT_TRUE(documents->CloseSceneDocument().has_value());
    EXPECT_EQ(clustering->Stats().CachedCapturePoints, 0u) << "released with the document";
    engine.Shutdown();
}

TEST(ClusteringModule, NullDeviceVulkanRequestFallsBackHonestly)
{
    auto app = std::make_unique<KMeansSuccessApp>(
        Runtime::ClusteringBackend::VulkanCompute);
    KMeansSuccessApp* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(
        NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // the editor session reaches JobService through it
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_TRUE(appPtr->ActionTaken);
    EXPECT_TRUE(appPtr->CancelAccepted);
    ASSERT_TRUE(appPtr->Job.IsValid());
    // The run's job carries the correlation id its submission returned, which
    // is how the shared progress model finds it without keeping a token.
    EXPECT_EQ(appPtr->JobCorrelation, appPtr->Correlation.Value);
    EXPECT_TRUE(appPtr->PreparedFrame);
    EXPECT_TRUE(appPtr->CommandsBound);
    EXPECT_NE(appPtr->ProgressBeforeAction.State, Runtime::EditorOperationState::None);
    EXPECT_FALSE(appPtr->ProgressBeforeAction.Determinate);
    EXPECT_EQ(appPtr->ProgressBeforeAction.Label, "Runtime.Clustering.KMeans.CPU");
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
        engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
        engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
        Runtime::StepRunsService Steps; // a GPU run is a step run; the module's own workspace carries its jobs
        Runtime::ClusteringModule Module;
        Runtime::EditorCommandHistory History;
        Runtime::WorldHandle World;
        ECS::EntityHandle Entity;
        Runtime::ClusteringService* Service{};
        Runtime::RunKMeans Request;
        Runtime::CommandCorrelationId Correlation;
        Runtime::KernelEventSubscription Subscription;
        std::vector<Runtime::KMeansRunCompleted> Results;
        // ARCH-023: a mock GPU the run's worker and the main thread share (Gpu guards it). A submitted page is
        // complete at once unless held; an ordinary sink is delivered by the next Tick (CollectCompleted's analogue),
        // a pulled one only by the worker's take. A held page stays pending until ReleaseHeld.
        struct MockPage
        {
            std::vector<std::byte> Data{};
            Extrinsic::RHI::ReadbackSink Sink{};
            bool Done{}, Released{};
        };
        std::mutex Gpu;
        std::map<std::uint64_t, MockPage> Pages;
        std::uint64_t NextToken{100u};
        std::optional<std::uint64_t> HeldToken;
        std::vector<std::function<void()>> Completions; // guarded by Gpu
        std::atomic<bool> Hold{};
        // Holds the submission whose bytes and phase sequence it accepts (a preview copy, say); on the submitter's
        // thread, under Gpu.
        std::function<bool(std::uint64_t bytes, const std::vector<std::uint32_t>& phases, std::size_t firstPush)>
            HoldWhen{};
        std::atomic<std::uint32_t> PreviewCopySubmissions{}; // completion-only pages that scatter labels (phase 4)
        // Per submission (under Gpu): the transfer-queue uploads issued so far and the submission's input order.
        std::vector<std::pair<std::size_t, Extrinsic::RHI::ComputeInputOrder>> OrderLog{};
        bool Attached{true}, Converged{}, Moved{};
        std::uint32_t MaximumDispatches{};
        bool HeldPending()
        {
            std::scoped_lock lock{Gpu};
            return HeldToken.has_value();
        }
        void Later(std::function<void()> work)
        {
            std::scoped_lock lock{Gpu};
            Completions.push_back(std::move(work));
        }
        void SetHoldWhen(decltype(HoldWhen) when)
        {
            std::scoped_lock lock{Gpu};
            HoldWhen = std::move(when);
        }
        // Completes the held page (with `data` instead of its own bytes when given).
        void ReleaseHeld(std::optional<std::vector<std::byte>> data = {})
        {
            std::scoped_lock lock{Gpu};
            if (!HeldToken)
                return;
            auto& page = Pages[*std::exchange(HeldToken, std::nullopt)];
            if (data)
                page.Data = std::move(*data);
            page.Done = true;
        }
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
            ASSERT_TRUE(Services.Provide<Runtime::JobService>(Jobs.Jobs(),"test"));
            ASSERT_TRUE(Services.Provide<Runtime::StepRunsService>(Steps,"test"));
            Runtime::EngineSetup setup{Commands,Events,Jobs.Jobs(),Worlds,Services,[](Runtime::FramePhase,Runtime::RuntimeFrameHook){}};
            ASSERT_TRUE(Cache.OnRegister(setup));ASSERT_TRUE(Module.OnRegister(setup));
            Services.BeginResolution();ASSERT_TRUE(Cache.OnResolve(setup));ASSERT_TRUE(Module.OnResolve(setup));Services.Lock();
            Service=Services.Find<Runtime::ClusteringService>();ASSERT_NE(Service,nullptr);
            Subscription=Service->SubscribeRunCompleted([this](const auto& r){Results.push_back(r);});
            Request.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entity);
            Request.Properties=Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint);
            Request.Parameters.ClusterCount = 3;
            Request.Parameters.Execution.IterationCap = 4;
            Request.Parameters.Execution.PreviewEvery = 2;
            Request.Backend = Runtime::ClusteringBackend::VulkanCompute;
            Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::Never;
            Request.AttachmentActive=[this]{return Attached;};
            Device.ComputeReadback = [this](auto record, auto bytes, auto sink)
            {
                std::scoped_lock lock{Gpu}; // the run's worker records here, the owner's terminal pages too
                {
                    std::scoped_lock orders{Device.ComputeInputOrderMutex};
                    OrderLog.emplace_back(Device.TransferQueue.BufferUploads.size(), Device.ComputeInputOrders.back());
                }
                const auto before=Device.CommandContext.DispatchCalls;
                const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
                EXPECT_TRUE(record(Device.CommandContext).IsValid());
                std::uint64_t pairs=0;
                std::uint32_t serialDepth=0;
                for (std::size_t i = firstPush; i < Device.CommandContext.PushConstantPayloads.size(); ++i)
                {
                    const auto& payload=Device.CommandContext.PushConstantPayloads[i];
                    EXPECT_EQ(payload.size(),128u);
                    if (payload.size() != 128u)
                        continue;
                    std::uint32_t points{},clusters{},phase{},first{},rows{},innerFirst{},columns{};
                    std::memcpy(&points, payload.data() + 96, 4);
                    std::memcpy(&clusters, payload.data() + 100, 4);
                    std::memcpy(&phase, payload.data() + 104, 4);
                    std::memcpy(&first, payload.data() + 108, 4);
                    std::memcpy(&rows, payload.data() + 112, 4);
                    std::memcpy(&innerFirst, payload.data() + 116, 4);
                    std::memcpy(&columns,payload.data()+120,4);
                    // ARCH-022 Q79 step 7b: a Reduce (3) or Update (2) dispatch holds a batch of pages (their count in
                    // InnerCount or Clusters) and its combine (6, 7) adds their partials; PagePairs caps each page,
                    // the batch's covered pairs count against the submission, the combine's serial path is its pages.
                    const std::uint32_t pages=phase==3||phase==6?columns:phase==2||phase==7?clusters:1u;
                    const std::uint64_t pagePairs=phase==3?rows:std::uint64_t(rows)*columns;
                    const std::uint64_t dispatchPairs=phase==3?std::min<std::uint64_t>(std::uint64_t(pages)*rows,points-first)
                        :phase==2?rows*std::min<std::uint64_t>(std::uint64_t(pages)*columns,points-innerFirst)
                        :phase==6||phase==7?0u:pagePairs;
                    if (phase != 6 && phase != 7)
                        EXPECT_LE(pagePairs, Runtime::KMeansPagingForTesting.PagePairs);
                    if (phase == 2 || phase == 7)
                        EXPECT_LE(rows, 64u) << "an Update tile holds at most 64 centroids";
                    EXPECT_LE(pages, 64u);
                    pairs += dispatchPairs;
                    serialDepth+=phase==1?columns+1:phase==2?(columns+63)/64+7:phase==3?(rows+63)/64+7
                        :phase==6||phase==7?pages+7:1;
                }
                EXPECT_LE(pairs,Runtime::KMeansPagingForTesting.SubmissionPairs);
                EXPECT_LE(serialDepth,Runtime::KMeansSubmissionSerialDepth);
                MaximumDispatches=std::max(MaximumDispatches,std::uint32_t(Device.CommandContext.DispatchCalls-before));
                std::vector<std::byte> data(bytes);
                if (bytes == 32)
                {
                    std::array<std::uint32_t,6> diagnostics{};
                    diagnostics[2] = std::bit_cast<std::uint32_t>(1.0f);
                    diagnostics[4] = (Converged ? 0u : 1u) | (Moved ? 2u : 0u);
                    std::memcpy(data.data(),diagnostics.data(),24);
                }
                std::vector<std::uint32_t> phases;
                for (std::size_t i = firstPush; i < Device.CommandContext.PushConstantPayloads.size(); ++i)
                {
                    const auto& push = Device.CommandContext.PushConstantPayloads[i];
                    if (push.size() != 128u)
                        continue;
                    std::uint32_t phase{};
                    std::memcpy(&phase, push.data() + 104, 4);
                    if (phases.empty() || phases.back() != phase)
                        phases.push_back(phase);
                }
                if (bytes == 0 && !phases.empty() && phases.front() == 4u)
                    ++PreviewCopySubmissions;
                const Extrinsic::RHI::ReadbackToken token{NextToken++};
                const bool held = !HeldToken && (Hold.load() || (HoldWhen && HoldWhen(bytes, phases, firstPush)));
                if (held)
                    HeldToken = token.Value;
                Pages[token.Value] = MockPage{.Data = std::move(data), .Sink = std::move(sink), .Done = !held};
                return token;
            };
            Device.TransferQueue.ComputeWait = [this](auto token, auto timeout)
            {
                {
                    std::scoped_lock lock{Gpu};
                    const auto page = Pages.find(token.Value);
                    if (page == Pages.end() || page->second.Done)
                        return Extrinsic::RHI::ComputeWaitStatus::Complete;
                }
                std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<std::uint64_t>(timeout, 1'000'000u)));
                return Extrinsic::RHI::ComputeWaitStatus::Pending;
            };
            Device.TransferQueue.TakeReadback = [this](auto token, std::span<std::byte> destination)
            {
                std::scoped_lock lock{Gpu};
                const auto page = Pages.find(token.Value);
                if (page == Pages.end() || !page->second.Done || !page->second.Sink.Pulled || page->second.Released ||
                    page->second.Data.size() != destination.size())
                    return false;
                std::ranges::copy(page->second.Data, destination.begin());
                Pages.erase(page);
                return true;
            };
            Device.TransferQueue.ReleaseReadbackHook = [this](auto token)
            {
                std::scoped_lock lock{Gpu};
                if (const auto page = Pages.find(token.Value); page != Pages.end() && page->second.Sink.Pulled)
                    page->second.Released = true;
            };
            // A compute token completes once the device finished it and it is not a pulled page awaiting its take.
            Device.TransferQueue.ReadbackComplete = [this](auto token)
            {
                std::scoped_lock lock{Gpu};
                const auto page = Pages.find(token.Value);
                return token.Value < 100u || page == Pages.end() ||
                    (page->second.Done && (!page->second.Sink.Pulled || page->second.Released));
            };
            Device.TransferQueue.BufferDownload = [this](auto, auto bytes, auto, auto sink)
            {
                Later(
                    [sink = std::move(sink), bytes]() mutable
                    {
                        std::vector<std::byte> data(bytes);
                        sink.Deliver(data);
                    });
                return Extrinsic::RHI::ReadbackToken{3};
            };
        }
        void Tick(const bool advanceGpu = true)
        {
            if(Shell)(void)Runtime::PrepareEditorWorkspaceSnapshotFrame(*Shell);
            std::vector<std::function<void()>> completions;
            {
                // CollectCompleted's analogue: completed ordinary pages deliver, released pulled ones go; a pulled
                // page waits for its worker's take.
                std::scoped_lock lock{Gpu};
                completions = std::exchange(Completions, {});
                for (auto it = Pages.begin(); it != Pages.end();)
                {
                    auto& page = it->second;
                    if (!page.Done || (page.Sink.Pulled && !page.Released))
                    {
                        ++it;
                        continue;
                    }
                    if (!page.Sink.Pulled)
                        completions.push_back(
                            [sink = std::move(page.Sink), data = std::move(page.Data)]() mutable
                            {
                                sink.Deliver(data);
                            });
                    it = Pages.erase(it);
                }
            }
            for (auto& complete : completions)
                complete();
            Commands.Drain(Scene(),{.Events=&Events,.Jobs=&Jobs.Jobs(),.Worlds=&Worlds});
            (void)Jobs.Jobs().DrainCompletions(Events);(void)Events.Pump();
            if (advanceGpu)
                Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
            ++Device.GlobalFrameNumber;
            if (advanceGpu)
                (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
            Cache.Prune(); // the Maintenance hook: returned method scratch is stamped and matures
        }
        // ARCH-023: every method scratch buffer is back in the cache and reusable.
        bool ScratchRetired()
        {
            const auto s = Cache.Stats();
            return s.ScratchActiveBytes == 0u && s.ScratchPendingBytes == 0u;
        }
        template <class P> bool Until(P predicate, const bool advanceGpu = true)
        {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (!predicate() && std::chrono::steady_clock::now() < end)
            {
                Tick(advanceGpu);
                std::this_thread::yield();
            }
            return predicate();
        }
        // The run's step run (known once the command drained), remembered so it stays observable after the run.
        std::uint64_t RunId{};
        auto Observation(){auto run=Service->GpuRun(Correlation);if(!RunId)RunId=run.StepRun;return run;}
        void Start(){Correlation=Service->RunKMeans(Request);RunId=0;}
        // Occupies both job workers until the returned flag is set: a seeding job then waits in the queue (ARCH-022 Q90).
        std::shared_ptr<std::atomic_bool> BlockWorkers()
        {
            auto release=std::make_shared<std::atomic_bool>(false);
            for(int i=0;i<2;++i)
                (void)Jobs.Jobs().Submit({.DebugName="blocker",.Work=[release](const Runtime::JobCancellation& c){
                    while(!release->load()&&!c.IsCancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    return Runtime::JobResultEnvelope::Make<bool>(true);},
                    .PublishCompletion=[](Runtime::KernelEventBus&,const Runtime::JobResultEnvelope&){return true;}});
            return release;
        }
        std::uint64_t StepRunId(){return RunId?RunId:Observation().StepRun;}
        Runtime::StepRunObservation StepRun(){auto run=Steps.Observe(StepRunId());EXPECT_TRUE(run);return run?*run:Runtime::StepRunObservation{};}
        bool Control(Runtime::StepRunAction action,std::uint32_t count=1){std::string refusal;return Steps.Control(StepRunId(),action,count,refusal);}
        // The app's editor workspace (the Sandbox shell's), lent as the shell does: the run's jobs are its rows.
        std::unique_ptr<Runtime::EditorWorkspaceAttachment> Shell{};
        void LendShell()
        {
            Shell=std::make_unique<Runtime::EditorWorkspaceAttachment>();Shell->Attach(Worlds,Services);
            (void)Runtime::PrepareEditorWorkspaceSnapshotFrame(*Shell); // the shell prepares one every frame (Tick)
            Service->UseEditorWorkspace(*Shell);
        }
        std::vector<Runtime::EditorJobRecord> Rows(){return Runtime::GetEditorJobs(Runtime::PrepareEditorProcessingCommands(*Shell));}
        // The phases of the pages recorded from now on (push constant 104), one entry per change.
        std::vector<std::uint32_t> PhasesSince(std::size_t firstPush)
        {
            std::vector<std::uint32_t> phases;
            for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i){
                const auto& push=Device.CommandContext.PushConstantPayloads[i];if(push.size()!=128u)continue;
                std::uint32_t phase{};std::memcpy(&phase,push.data()+104,4);
                if(phases.empty()||phases.back()!=phase)phases.push_back(phase);
            }
            return phases;
        }
    public:
        void DiscardDuringPublication(entt::registry&, entt::entity)
        { (void)Service->GpuRun(Correlation, Runtime::KMeansGpuAction::Discard); }
    protected:
        void TearDown()override
        {
            Hold = false;
            SetHoldWhen({});
            ReleaseHeld();
            if(Service){(void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);Service->Unsubscribe(Subscription);}
            for(int i=0;i<5;++i)Tick();
            if(Shell){Service->ReleaseEditorWorkspace(*Shell);Shell->Detach();}
            Jobs.Jobs().CancelAndDrain();(void)Jobs.Jobs().ShutdownGpuQueueParticipants([this]{Device.WaitIdle();});
            Runtime::RuntimeModuleShutdownContext shutdown{Commands,Events,Jobs.Jobs(),Worlds,Services};
            Module.OnShutdown(shutdown);Cache.OnShutdown(shutdown);Services.Reset();Renderer->Shutdown();
            Runtime::KMeansPagingForTesting=SavedLimits;
        }
    };
}
TEST_F(ClusteringModuleResident, ResidentInputWaitsForCompletionAndRepeatRunUploadsZero)
{
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    EXPECT_EQ(Observation().InputUploadBytes,12u*12u);
    const auto submissions=Observation().Submissions;for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Observation().Submissions,submissions);
    Hold = false;
    ReleaseHeld();
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Observation().Iterations, 4u);
    // The owner may offer another slot before iteration 4; loose preview timing cannot change compute work.
    const auto liveCopies = PreviewCopySubmissions.load();
    EXPECT_GE(liveCopies, 1u);
    EXPECT_LE(liveCopies, 2u);
    EXPECT_EQ(Observation().Previews, liveCopies + 1u);
    Runtime::EditorProcessingContext previewContext{.Scene = &Scene(), .World = World, .SpatialIndices = &Cache};
    const auto previewCommands = Runtime::BindEditorProcessingCommands(previewContext);
    const auto blocked = Runtime::PreviewEditorKMeansRun(previewCommands, Service, Request);
    EXPECT_FALSE(blocked.Enabled);
    EXPECT_NE(blocked.DisabledReason.find("Accept or Discard"), std::string::npos);
    EXPECT_EQ(Runtime::SubmitKMeansRun(previewCommands, Service, Request).Message, blocked.DisabledReason);
    auto otherOutput = Request;
    otherOutput.Properties.OutputLabels.Name = "other_labels";
    otherOutput.Properties.OutputColors.Name = "other_colors";
    otherOutput.Properties.OutputScalarLabels.reset();
    EXPECT_FALSE(Service->GpuReadinessDiagnostic(Request, previewContext).empty());
    EXPECT_EQ(Runtime::PreviewEditorKMeansRun(previewCommands, Service, otherOutput).DisabledReason, blocked.DisabledReason);
    // One submission per iteration, the interval preview's own (ARCH-023: never fused into an iteration's), and
    // the terminal preview, which also reads the centroids back.
    EXPECT_EQ(Observation().Submissions, 5u + liveCopies);
    EXPECT_EQ(Observation().CpuStageUploadBytes,3u*12u);
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_TRUE(Runtime::PreviewEditorKMeansRun(previewCommands, Service, Request).Enabled);
    EXPECT_TRUE(Service->GpuReadinessDiagnostic(Request, previewContext).empty());
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Observation().InputUploadBytes,0u);EXPECT_GT(Observation().InputCacheHits,0u);
}
// ARCH-022 Q75: an Accept and its undo and redo write only the outputs, so the positions keep their revision and the
// next run reuses the resident input instead of uploading it again.
TEST_F(ClusteringModuleResident, AnAcceptAndItsUndoKeepTheResidentInput)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_GT(Observation().InputUploadBytes,0u);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));ASSERT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    ASSERT_TRUE(Properties().Exists("p:kmeans_label"));
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Observation().InputUploadBytes,0u);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==2;}));
    ASSERT_TRUE(History.Undo().Succeeded());ASSERT_TRUE(History.Redo().Succeeded());
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Observation().InputUploadBytes,0u);
}
TEST_F(ClusteringModuleResident, RetainedWorkspaceReusesCapacityAndUnbindsSlots)
{
    const auto run=[&](std::size_t results){
        Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
        (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
        ASSERT_TRUE(Until([&]{return Results.size()==results;}));
        ASSERT_TRUE(Until(
            [&]
            {
                return ScratchRetired();
            })); // the parked workspace's buffers retired
    };
    run(1);
    EXPECT_GT(Results[0].GpuWorkspaceBuffersCreated,0u);EXPECT_EQ(Results[0].GpuWorkspacePipelinesCreated,3u);
    Properties().GetOrAdd<bool>("v:deleted",false)[4]=true;
    run(2);
    // Eleven live rows fit the retired buffers' size classes; only the live-slot map is new.
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
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        })); // the first iteration's page is in flight
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Stop);
    Hold = false;
    ReleaseHeld();
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
    Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1;
        }));
    EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;EXPECT_GT(Results[0].CpuStageReadbackBytes,0u);
    EXPECT_EQ(Results[0].ImplementationId,"vulkan_resident_paged_lloyd");
}
// ARCH-023: a live preview is its own completion-only submission (scatter 4 and presentation 5 alone, never fused into
// an iteration's), copied into the slot the owner offered, and published only once it completed; a held copy stalls
// no iteration. Preview off acquires and copies nothing. Mutation: publishing at submission shows the front early;
// fusing it into the next iteration changes the phase sequence; waiting for the copy holds the next step.
TEST_F(ClusteringModuleResident, PreviewCopiesOnlyAtCadenceAndPublishesAfterCompletion)
{
    const auto key=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    std::uint64_t copiedBack{};
    std::vector<std::uint32_t> phases{};
    HoldWhen = [&](std::uint64_t bytes, const std::vector<std::uint32_t>& recorded, std::size_t firstPush)
    {
        if (bytes != 0u || recorded.empty() || recorded.front() != 4u || !phases.empty())
            return false;
        phases = recorded;
        for (std::size_t i = firstPush; i < Device.CommandContext.PushConstantPayloads.size(); ++i)
        {
            const auto& push = Device.CommandContext.PushConstantPayloads[i];
            std::uint32_t phase{};
            std::memcpy(&phase, push.data() + 104, 4);
            if (phase == 4)
                std::memcpy(&copiedBack, push.data() + 80, 8); // phase 4 scatters every live label
        }
        return true; // the first live preview copy stays in flight
    };
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRunId() != 0u;
        }));
    for (int i = 0; i < 3; ++i)
        Tick(); // the owner offers its slot
    ASSERT_TRUE(Control(Runtime::StepRunAction::Step, 2u));
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    ASSERT_NE(copiedBack, 0u);
    EXPECT_EQ(phases, (std::vector<std::uint32_t>{4u, 5u})) << "the copy alone, after iteration 2";
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRun().PreviewCopies == 1u;
        })); // the submission hook precedes its receipt
    for (int i = 0; i < 5; ++i)
        Tick();
    EXPECT_EQ(Observation().Previews, 0u);
    EXPECT_FALSE(residency->Front(key)) << "not shown before it completed";
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRun().Phase == Runtime::StepRunPhase::Paused;
        }));
    ASSERT_TRUE(Control(Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRun().Steps == 3u && StepRun().Phase == Runtime::StepRunPhase::Paused;
        }))
        << "the held copy stalls no iteration";
    ReleaseHeld();
    ASSERT_TRUE(Until([&]{return Observation().Previews==1;}));
    ASSERT_TRUE(residency->Front(key));EXPECT_EQ(residency->Front(key)->Address,copiedBack);
    ASSERT_TRUE(Control(Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(Device.CommandContext.CopyBufferRecords.size(),0u);
    (void)Service->GpuRun(Correlation, Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1;
        }));

    // Preview off: no slot acquired, no copy submitted, only the terminal labels.
    SetHoldWhen({});
    const auto copies = PreviewCopySubmissions.load();
    Request.Parameters.Execution.PreviewEvery = 0;
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Run;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    EXPECT_EQ(PreviewCopySubmissions.load(), copies);
    EXPECT_EQ(StepRun().PreviewCopies, 0u);
    EXPECT_EQ(Observation().Previews, 1u);
}
TEST_F(ClusteringModuleResident, ReentrantDiscardDuringAcceptKeepsOneAppliedCompletion)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    Scene().Raw().on_construct<Dirty::DirtyVertexColors>().connect<&ClusteringModuleResident::DiscardDuringPublication>(*this);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    Scene().Raw().on_construct<Dirty::DirtyVertexColors>().disconnect<&ClusteringModuleResident::DiscardDuringPublication>(*this);
    EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    for(int i=0;i<10;++i)Tick();EXPECT_EQ(Results.size(),1u);
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Committed)<<"the publication under way decided";
    EXPECT_EQ(History.UndoCount(),1u);
    EXPECT_TRUE(Properties().Exists("p:kmeans_label"));ASSERT_TRUE(History.Undo().Succeeded());
}
TEST_F(ClusteringModuleResident, DiscardPendingAcceptDeliversOnce)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    Hold=true;(void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    Hold = false;
    ReleaseHeld(std::vector<std::byte>(12 * 4));
    for(int i=0;i<10;++i)Tick();EXPECT_EQ(Results.size(),1u);EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled);
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Discarded)<<"discarded before its publication";
}
TEST(ClusteringModule, AgentRunAutoAcceptsAndReportsBackendAndIo)
{
    auto config=NullWindowHeadlessConfig();
    Runtime::ClusteringConfig clustering;
    clustering.Backend=Runtime::ClusteringBackend::VulkanCompute;
    Runtime::SetClusteringConfig(config,clustering);
    Intrinsic::Tests::RuntimeTestKernel engine{std::move(config)};
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
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
    auto* history=engine.Services().Find<Runtime::EditorCommandHistory>();ASSERT_NE(history,nullptr);
    const Runtime::AgentOperationContext context{.Attachment=&attachment,.History=history};
    auto outcome=Runtime::InvokeAgentOperation(registry,"run_kmeans",context,
        "{\"entity\":"+std::to_string(Runtime::SelectionController::ToStableEntityId(entity))+",\"domain\":\"PointCloudPoint\"}",false);
    ASSERT_TRUE(outcome.Continuation)<<outcome.Text;
    Runtime::AgentOperationOutcome completed;bool done=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!done&&std::chrono::steady_clock::now()<deadline){
        (void)Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment);
        engine.Commands().Drain(*scene,{&engine.Events(),&engine.Jobs(),&engine.Worlds()});
        (void)engine.Jobs().DrainCompletions(engine.Events());(void)engine.Events().Pump();
        done=outcome.Continuation(context,completed);std::this_thread::yield();
    }
    EXPECT_TRUE(done);EXPECT_FALSE(completed.IsError)<<completed.Text;
    for(const auto key:{"gpu_input_upload_bytes","gpu_input_cache_hits","cpu_stage_upload_bytes","cpu_stage_readback_bytes","implementation_id"})
        EXPECT_NE(completed.Text.find(key),std::string::npos);
    EXPECT_TRUE(history->Snapshot().UndoLabel.starts_with("Agent: "))<<"the run commits on a later frame but keeps the call's label: "<<history->Snapshot().UndoLabel;
    EXPECT_TRUE(vertices.Properties.Exists("p:kmeans_label"));
    outcome.Continuation={};attachment.Detach();engine.Shutdown();
}
TEST(ClusteringModule, AgentCompletionListenerIsReleasedOnDetachAndCancellation)
{
    auto config=NullWindowHeadlessConfig();
    Intrinsic::Tests::RuntimeTestKernel engine{std::move(config)};
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.EmplaceModule<Runtime::AsyncWorkModule>(); // ARCH-023 B8: a CPU run's editor jobs reach JobService
    engine.EmplaceModule<Runtime::StepRunsModule>(); // a CPU or GPU K-Means run is a step run
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    CoreConfig::EngineConfigSectionRegistry sections;
    ASSERT_TRUE(sections.Register(Runtime::MakeClusteringConfigSectionRegistration()));
    engine.EmplaceModule<Runtime::EngineConfigControl>(std::move(sections));engine.Initialize();
    auto* scene=engine.Worlds().Get(engine.ActiveWorld());ASSERT_NE(scene,nullptr);
    const auto entity=scene->Create();auto& vertices=scene->Raw().emplace<GS::Vertices>(entity);
    SetPositions(vertices,{{0,0,0},{1,0,0},{3,0,0}});
    Runtime::AgentOperationRegistry registry;Runtime::RegisterEditorAgentOperations(registry);
    Runtime::EditorWorkspaceAttachment attachment;
    const Runtime::AgentOperationContext context{.Attachment=&attachment};
    const auto listenerCalls=[&]{
        const auto before=engine.Events().Stats().ListenerInvocations;
        engine.Events().Publish(Runtime::KMeansRunCompleted{});(void)engine.Events().Pump();
        return engine.Events().Stats().ListenerInvocations-before;
    };
    const auto baseline=listenerCalls();
    for(const auto mode:{"detach","cancel"})
    {
        SCOPED_TRACE(mode);
        attachment.Attach(engine.Worlds(),engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto attached=listenerCalls();
        auto outcome=Runtime::InvokeAgentOperation(registry,"run_kmeans",context,
            "{\"entity\":"+std::to_string(Runtime::SelectionController::ToStableEntityId(entity))+",\"domain\":\"PointCloudPoint\"}",false);
        ASSERT_TRUE(outcome.Continuation)<<outcome.Text;
        EXPECT_EQ(listenerCalls(),attached+1u);
        if(std::string_view(mode)=="detach")
        {
            attachment.Detach();Runtime::AgentOperationOutcome completed;
            EXPECT_TRUE(outcome.Continuation(context,completed));EXPECT_TRUE(completed.IsError);
            EXPECT_EQ(listenerCalls(),baseline);
        }
        else {outcome.Continuation={};EXPECT_EQ(listenerCalls(),attached);}
        outcome.Continuation={};attachment.Detach();
    }
    engine.Shutdown();
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
    Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1;
        }));
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
    Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
    Request.AttachmentActive=[&]{
        if(arm&&++checks==3)Properties().Get<glm::vec3>("v:position")[0].x=9;
        return true;
    };
    const auto submit=Device.ComputeReadback;
    Device.ComputeReadback=[&](auto record,auto bytes,auto sink){
        const auto token=submit(std::move(record),bytes,std::move(sink));
        if (bytes == 3 * 12)
            Later(
                [&]
                {
                    arm = true;
                }); // the owner's terminal page
        return token;
    };
    Start();ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::StaleSource);
    EXPECT_EQ(Service->Stats().GpuCompletions,0u);
    arm=false;Request.AttachmentActive={};Start();
    ASSERT_TRUE(Until([&]{return Results.size()==2;}));EXPECT_TRUE(Results[1].Succeeded())<<Results[1].Message;
    EXPECT_EQ(Service->Stats().GpuCompletions,1u);
}
TEST_F(ClusteringModuleResident, StaleDuringAcceptReadbackReportsStaleSource)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    Properties().Get<glm::vec3>("v:position")[0].x=7;
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::StaleSource);
    ASSERT_TRUE(Until([&]{return Runtime::IsTerminal(StepRun().Phase);}));
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Failed);EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
// ARCH-022 Q90: the Accept's first apply checks the run's revision watches instead of recapturing the input and the
// outputs. A change during the Accept's readback through any mutable path that keeps the property contract still ends
// the run stale and writes nothing: positions through 0 Vector(), 1 operator[], 2 Span(), 3 Data(), 4 a span retained
// from before the run and marked after the write; 5 the deletion mask; 6 the colors output created; 7 a point added;
// existing 8 labels, 9 colors and 10 scalar labels edited.
TEST_F(ClusteringModuleResident, AChangeDuringTheAcceptThroughAnyMutablePathEndsItStale)
{
    const auto base = Request;
    const auto& labels = base.Properties.OutputLabels.Name;
    const auto& colors = base.Properties.OutputColors.Name;
    const std::string scalars = "scalar_labels";
    const auto remove = [&]<class T>(const std::string& name) {
        if (auto property = Properties().Get<T>(name)) Properties().Remove(property);
    };
    for (unsigned path = 0; path < 11; ++path)
    {
        SCOPED_TRACE(path);
        SetPositions(Scene().Raw().get<GS::Vertices>(Entity), std::vector<glm::vec3>(12, glm::vec3(1, 2, 3)));
        remove.template operator()<bool>("v:deleted");
        remove.template operator()<std::uint32_t>(labels);
        remove.template operator()<glm::vec4>(colors);
        remove.template operator()<double>(scalars);
        Request = base;
        if (path == 8) Properties().GetOrAdd<std::uint32_t>(labels, 0u);
        if (path == 9) Properties().GetOrAdd<glm::vec4>(colors, glm::vec4(0));
        if (path == 10)
        {
            Request.Properties.OutputScalarLabels = Runtime::GeometryPropertyRef{
                .Domain = Runtime::GeometryElementDomain::PointCloudPoint, .Name = scalars,
                .ValueKind = Geometry::PropertyValueKind::Double};
            Properties().GetOrAdd<double>(scalars, 0.0);
        }
        auto retained = Properties().Get<glm::vec3>("v:position");
        const auto span = retained.Span(); // marks now, before the run's capture
        const auto before = Results.size();
        Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
        (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
        switch (path)
        {
        case 0: Properties().Get<glm::vec3>("v:position").Vector()[0].x += 1; break;
        case 1: Properties().Get<glm::vec3>("v:position")[0].x += 1; break;
        case 2: Properties().Get<glm::vec3>("v:position").Span()[0].x += 1; break;
        case 3: Properties().Get<glm::vec3>("v:position").Data()[0].x += 1; break;
        case 4: span[0].x += 1; retained.MarkModified(); break;
        case 5: Properties().GetOrAdd<bool>("v:deleted",false)[4] = true; break;
        case 6: Properties().GetOrAdd<glm::vec4>(colors, glm::vec4(0)); break;
        case 7: Properties().Resize(13); break;
        case 8: Properties().Get<std::uint32_t>(labels)[0] = 7u; break;
        case 9: Properties().Get<glm::vec4>(colors)[0] = glm::vec4(7); break;
        case 10: Properties().Get<double>(scalars)[0] = 7.0; break;
        }
        ASSERT_TRUE(Until([&]{return Results.size()==before+1;}));
        EXPECT_EQ(Results.back().Status,Runtime::KMeansRunStatus::StaleSource) << Results.back().Message;
        if (path == 8) EXPECT_EQ(std::as_const(Properties()).Get<std::uint32_t>(labels)[0], 7u) << "nothing was written";
        else EXPECT_FALSE(Properties().Exists(labels)) << "nothing was written";
        if (path == 9) EXPECT_EQ(std::as_const(Properties()).Get<glm::vec4>(colors)[0], glm::vec4(7));
        if (path == 10) EXPECT_EQ(std::as_const(Properties()).Get<double>(scalars)[0], 7.0);
        for (int i = 0; i < 5; ++i) Tick();
    }
}
// ARCH-022 Q90: a request waiting for its seeds that a newer admitted request supersedes is cancelled, even when the
// newer one starts at once (Random seeds are computed inline), so it never starts after the newer one.
TEST_F(ClusteringModuleResident, ARequestWaitingForItsSeedsIsCancelledByANewerOne)
{
    const auto release=BlockWorkers();
    Start();for(int i=0;i<3;++i)Tick();
    ASSERT_TRUE(Results.empty());EXPECT_EQ(Module.Stats().GpuRequestsAccepted,0u)<<"it waits for its seeds";
    auto newer=Request;newer.ReplaceRunning=true;newer.Parameters.Initialization=Runtime::KMeansInitialization::Random;
    Correlation=Service->RunKMeans(newer);RunId=0;
    ASSERT_TRUE(Until([&]{return Results.size()==1u;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled)<<Results[0].Message;
    release->store(true); // the newer resident run now computes on the same worker pool
    EXPECT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }))
        << "the newer request runs";
    for(int i=0;i<10;++i)Tick();
    EXPECT_EQ(Module.Stats().GpuRequestsAccepted,1u)<<"the cancelled one never starts";
}
// ARCH-022 Q90: a request waiting for its seeds ends when its capture is released (a world change or a scene document
// replacement), even when the same world is active again before its seeds are ready.
TEST_F(ClusteringModuleResident, ARequestWaitingForItsSeedsEndsWhenTheSceneChanges)
{
    const auto release=BlockWorkers();
    Start();for(int i=0;i<3;++i)Tick();
    ASSERT_TRUE(Results.empty());
    const auto first=World;
    ASSERT_TRUE(Worlds.RequestSetActiveWorld(Worlds.CreateWorld("other")));(void)Worlds.ApplyMaintenance(Events,Jobs.Jobs());
    (void)Events.Pump();
    ASSERT_TRUE(Worlds.RequestSetActiveWorld(first));(void)Worlds.ApplyMaintenance(Events,Jobs.Jobs());(void)Events.Pump();
    release->store(true);
    ASSERT_TRUE(Until([&]{return Results.size()==1u;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::StaleWorld)<<Results[0].Message;
    EXPECT_EQ(Module.Stats().GpuRequestsAccepted,0u);EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
// ARCH-022 Q90: a request rejected for its input (a NaN position) replaces the shared capture but neither cancels nor
// supersedes a valid request waiting for its seeds: that one captures and seeds again and starts.
TEST_F(ClusteringModuleResident, ARejectedRequestLeavesARequestWaitingForItsSeedsToStart)
{
    auto bad=Properties().GetOrAdd<glm::vec3>("bad_positions",glm::vec3(0));
    bad[0].x=std::numeric_limits<float>::quiet_NaN();
    const auto release=BlockWorkers();
    Start();for(int i=0;i<3;++i)Tick();
    const auto waiting=Correlation;
    auto invalid=Request;invalid.Properties.InputPositions.Name="bad_positions";
    (void)Service->RunKMeans(invalid);
    ASSERT_TRUE(Until([&]{return Results.size()==1u;}));
    EXPECT_NE(Results[0].Correlation,waiting)<<"only the rejected request completes";
    EXPECT_FALSE(Results[0].Succeeded());
    release->store(true);
    Correlation=waiting;RunId=0;
    EXPECT_TRUE(Until([&]{return Observation().ReadyToAccept;}))<<"the waiting request starts";
    EXPECT_EQ(Results.size(),1u);
}
// ARCH-022 Q90: no recapture at the Accept's first apply: a write outside the property contract (a span retained from
// before the run, never marked) is not seen, and the Accept publishes.
TEST_F(ClusteringModuleResident, TheAcceptsFirstApplyChecksRevisionsNotValues)
{
    const auto unmarked = Properties().Get<glm::vec3>("v:position").Span(); // marks now, before the run's capture
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    unmarked[0].x += 1; // outside the contract: no revision changes
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_TRUE(Properties().Exists("p:kmeans_label"));
}
TEST_F(ClusteringModuleResident, AdmissionReservesTypedAndPresentationAgainstScalarOwners)
{
    Start();ASSERT_TRUE(Until([&]{return StepRunId()!=0u;})); // started once its seeds are ready (ARCH-022 Q90)
    auto* residency=Cache.PropertyResidency();ASSERT_NE(residency,nullptr);
    const auto typed=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
    const auto presentation=Runtime::MakeGpuPropertyKey(World,Entity,Runtime::GpuPropertyPresentationRef(Request.Properties.OutputLabels));
    ASSERT_TRUE(residency->HasRing(typed));ASSERT_TRUE(residency->HasRing(presentation));
    const auto generation=residency->RingGeneration(presentation);
    Runtime::EditorProcessingContext context;
    context.Scene=&Scene();context.World=World;context.Device=&Device;context.SpatialIndices=&Cache;
    Jobs.Attach(context);
    const auto commands=Runtime::BindEditorProcessingCommands(context);
    // A competing point scalar transaction on the presentation key (ARCH-022 Q90: no engine method starts one alone).
    const auto make=[&]{return Runtime::MakeEditorPointScalarTransactionForTest(commands,Request.StableEntityId,
        Request.Properties.InputPositions,Runtime::GpuPropertyPresentationRef(Request.Properties.OutputLabels),
        std::vector<float>(12,3),residency);};
    auto competing=make();
    EXPECT_FALSE(competing);EXPECT_EQ(residency->RingGeneration(presentation),generation);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    competing=make();
    ASSERT_TRUE(competing);
    Start();ASSERT_TRUE(Until([&]{return Results.size()==2;}));EXPECT_FALSE(Results[1].Succeeded());
    EXPECT_TRUE(Runtime::SnapshotEditorPointScalar(commands,competing).CanAccept);
    Runtime::DiscardEditorPointScalar(commands,competing);
}
TEST_F(ClusteringModuleResident, TerminalPreviewRetriesAreBounded)
{
    Request.Parameters.Execution.PreviewEvery = 1;
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRunId() != 0u;
        }));
    std::vector<Extrinsic::Graphics::GpuPropertyView> leases;
    std::uint64_t publication=0;
    const auto lease = [&]
    {
        const auto key=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
        if (auto front = Cache.PropertyResidency()->Front(key); front && front->Publication != publication)
        {
            publication = front->Publication;
            leases.push_back(*front);
        }
    };
    // One step at a time: each step's preview publishes into a fresh slot, which this test then leases.
    for (std::size_t i = 1; i <= 3; ++i)
    {
        for (int t = 0; t < 3; ++t)
            Tick(); // the owner offers its slot
        ASSERT_TRUE(Control(Runtime::StepRunAction::Step, 1u));
        ASSERT_TRUE(Until(
            [&]
            {
                lease();
                return leases.size() == i;
            }))
            << i;
    }
    ASSERT_TRUE(Control(Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            lease();
            return Results.size() == 1;
        }));
    EXPECT_EQ(leases.size(),3u);
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::GeometryProcessingFailed);
    EXPECT_NE(Results[0].Message.find("600 retries"),std::string::npos);
}

TEST_F(ClusteringModuleResident, MillionRowsRespectPairAndSerialBudgets)
{
    SetPositions(Scene().Raw().get<GS::Vertices>(Entity),std::vector<glm::vec3>(1u<<20,glm::vec3(1,2,3)));
    Request.Parameters.ClusterCount = 8;
    Request.Parameters.Execution.IterationCap = 1;
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    // Fixture checks every recorded submission's pair and lane-depth totals.
    // Fused phases still split at the serial-depth budget and host boundary.
    // ARCH-022 Q79 step 7b: batched Reduce and Update fit one submission's depth (4 Reduce batches and 4 Update
    // batches of 64 pages, about 1 136 instead of 36 352), so the iteration takes two submissions (split by pairs,
    // then the boundary) and the terminal preview a third, instead of 4.
    EXPECT_EQ(Observation().Submissions,3u);
    EXPECT_LE(MaximumDispatches,Runtime::KMeansSubmissionSerialDepth);
}

TEST_F(ClusteringModuleResident, AGpuRunIsAStepRunCountingItsIterations)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    // Hierarchical seeds come from a worker job, not the main thread's start, and the job completes (ARCH-022 Q90).
    EXPECT_EQ(Module.Stats().GpuSeedingJobs,1u);EXPECT_EQ(Module.Stats().GpuSeedingMicros,0u);
    EXPECT_EQ(Jobs.Jobs().Stats().DroppedCompletions,0u);
    ASSERT_NE(StepRunId(),0u);
    auto run=StepRun();
    EXPECT_EQ(run.Owner,"Runtime.ClusteringModule");
    EXPECT_EQ(run.Steps,4u);EXPECT_EQ(run.End,Runtime::StepRunEnd::IterationCap);
    EXPECT_EQ(run.Phase,Runtime::StepRunPhase::Paused)<<"an interactive run waits at its cap for Accept or Discard";
    ASSERT_EQ(run.Metrics->Records().size(),4u);
    EXPECT_EQ(run.Metrics->Records().back().Values[1],1.0)<<"the iteration's squared maximum centroid shift";
    // One 32-byte diagnostics readback per iteration after Init, then the terminal preview's centroids.
    EXPECT_EQ(Observation().CpuStageReadbackBytes,4u*32u+3u*12u);
    for(int i=0;i<10;++i)Tick();
    EXPECT_TRUE(Results.empty());EXPECT_FALSE(Properties().Exists("p:kmeans_label"));EXPECT_FALSE(History.CanUndo());
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Committed;}));
    EXPECT_EQ(History.UndoCount(),1u);
}
TEST_F(ClusteringModuleResident, ARunRestsAtItsBoundaryAndAcceptPreparesTheCurrentCentroidsLabels)
{
    Moved = true;
    Request.Parameters.Execution.IterationCap = 10;
    Request.Parameters.Execution.PreviewEvery = 100;
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    ASSERT_TRUE(Control(Runtime::StepRunAction::Pause)); // the first page is in flight: it counts, then the run rests
    Hold = false;
    ReleaseHeld();
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Paused;}));
    EXPECT_EQ(Observation().Iterations,1u);EXPECT_EQ(StepRun().Steps,1u);
    EXPECT_TRUE(Observation().ReadyToAccept)<<Observation().Message;EXPECT_TRUE(Observation().CanAccept);
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().Previews == 1u;
        }))
        << "its boundary's preview, recorded alone";
    const auto submissions=Observation().Submissions;
    for(int i=0;i<10;++i)Tick();
    EXPECT_EQ(Observation().Submissions,submissions)<<"nothing is recorded while it rests";
    EXPECT_EQ(Observation().Iterations,1u);
    ASSERT_TRUE(Control(Runtime::StepRunAction::Step,1));
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Paused&&Observation().Iterations==2u;}));
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().Previews == 2u;
        }))
        << "the new rest's preview";
    for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Observation().Iterations,2u)<<"one step, one iteration";
    const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    EXPECT_EQ(Results[0].Iterations,2u)<<"Accept counts no iteration";
    // The update moved centroids: a final assignment (Assign, Reduce and its combine), then the terminal preview.
    EXPECT_EQ(PhasesSince(firstPush),(std::vector<std::uint32_t>{1u,3u,6u,4u,5u}));
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Committed;}));
    EXPECT_EQ(StepRun().Steps,2u);
}
TEST_F(ClusteringModuleResident, ACappedBatchPublishesAndACappedInteractiveRunWaits)
{
    Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
    Request.Parameters.Execution.IterationCap = 2;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRunId() != 0u;
        }));
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));EXPECT_TRUE(Results[0].Succeeded())<<Results[0].Message;
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Committed;}));
    EXPECT_EQ(StepRun().End,Runtime::StepRunEnd::IterationCap);
    ASSERT_EQ(History.UndoCount(),1u);ASSERT_TRUE(History.Undo().Succeeded());
    Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::Never;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRunId() != 0u;
        }));
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    for(int i=0;i<10;++i)Tick();
    EXPECT_EQ(Results.size(),1u);EXPECT_EQ(StepRun().End,Runtime::StepRunEnd::IterationCap);
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Paused);
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));EXPECT_EQ(History.UndoCount(),0u);
}
TEST_F(ClusteringModuleResident, AStoppedBatchNeverAcceptsALateConvergence)
{
    Request.Parameters.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
    Converged = true;
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        })); // the first page, reporting convergence, is held
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Stop);
    Hold = false;
    ReleaseHeld();
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    for(int i=0;i<10;++i)Tick();
    EXPECT_TRUE(Results.empty())<<"a stopped run waits for Accept or Discard";
    EXPECT_EQ(StepRun().End,Runtime::StepRunEnd::Stopped);
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
TEST_F(ClusteringModuleResident, APausedRunWithAHeldPageRefusesAcceptAndDiscardsOnceItIsConsumed)
{
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    ASSERT_TRUE(Control(Runtime::StepRunAction::Pause));
    EXPECT_FALSE(Control(Runtime::StepRunAction::Accept))<<"not while its page is in flight";
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    EXPECT_FALSE(Observation().Accepting);
    ASSERT_TRUE(Control(Runtime::StepRunAction::Discard));
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Discarded)<<"terminal at once";
    // Its pump (once the holder's work ran on a worker) discards it; it completes without waiting for its page.
    ASSERT_TRUE(Until([&]{return Results.size()==1;}))<<"the discarded run completes while its page is held";
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled);
    EXPECT_TRUE(StepRun().SegmentInFlight)<<"its page is still recorded";
    Hold = false;
    ReleaseHeld();
    ASSERT_TRUE(Until([&]{return !StepRun().SegmentInFlight;}))<<"and it retires once the page completed";
    EXPECT_EQ(Results.size(),1u);EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
TEST_F(ClusteringModuleResident, TheRunIsAJobsRowWhileRunningRestingAndAcceptingAndCancelledThroughEither)
{
    LendShell();
    Request.Properties.OutputLabels.Name="p:cluster_id"; // a custom output names the row
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    const auto find=[&](auto predicate){auto rows=Rows();const auto row=std::ranges::find_if(rows,predicate);
        return row==rows.end()?std::optional<Runtime::EditorJobRecord>{}:std::optional{*row};};
    const auto holder=find([](const auto& row){return row.Identity.OutputName=="p:cluster_id";});
    ASSERT_TRUE(holder)<<"the running run is a row of the lent workspace";
    EXPECT_FALSE(holder->Identity.Run.IsValid())<<"the run's first job";
    EXPECT_EQ(holder->Identity.RequestedDomain,Runtime::EditorJobDomain::GpuCompute);
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(holder->State));
    ASSERT_TRUE(Control(Runtime::StepRunAction::Pause));
    Hold = false;
    ReleaseHeld();
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Paused;}));
    const auto resting=find([&](const auto& row){return row.Token==holder->Token;});
    ASSERT_TRUE(resting);EXPECT_TRUE(Runtime::IsActiveEditorJobState(resting->State))<<"a resting run stays a row";
    ASSERT_TRUE(Control(Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept&&!Observation().Running&&Observation().Iterations==4u;}));
    Hold=true;(void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        })); // its label readback
    const auto stage=find([&](const auto& row){return row.Identity.Run==holder->Token;});
    ASSERT_TRUE(stage)<<"the Accept stage joins the run";
    EXPECT_EQ(stage->Identity.OutputName,"p:cluster_id");
    EXPECT_EQ(Runtime::CancelEditorJobRun(Runtime::PrepareEditorProcessingCommands(*Shell),*stage),
        Runtime::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled);
    ASSERT_TRUE(Until([&]{return Runtime::IsTerminal(StepRun().Phase);}));
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Discarded);
    Hold = false;
    ReleaseHeld(std::vector<std::byte>(12 * 4));
    for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Results.size(),1u);EXPECT_FALSE(Properties().Exists("p:cluster_id"));
    // Cancelling the holder's row ends a running run.
    Start();ASSERT_TRUE(Until([&]{return StepRunId()!=0u;}));
    const auto second=find([&](const auto& row){return row.Identity.OutputName=="p:cluster_id"&&row.Token!=holder->Token&&
        !row.Identity.Run.IsValid()&&Runtime::IsActiveEditorJobState(row.State);});
    ASSERT_TRUE(second);
    EXPECT_EQ(Runtime::CancelEditorJobRun(Runtime::PrepareEditorProcessingCommands(*Shell),*second),
        Runtime::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(Until([&]{return Results.size()==2;}));
    EXPECT_EQ(Results[1].Status,Runtime::KMeansRunStatus::Cancelled);
    ASSERT_TRUE(Until([&]{return Runtime::IsTerminal(StepRun().Phase);}));
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Discarded);
}
TEST_F(ClusteringModuleResident, ACancelOfTheAcceptStageAloneDiscardsTheRun)
{
    LendShell();
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    Hold=true;(void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        })); // its label readback
    const auto rows=Rows();
    const auto stage = std::ranges::find_if(rows,
        [](const auto& row)
        {
            return row.Identity.Run.IsValid() && Runtime::IsActiveEditorJobState(row.State);
        });
    ASSERT_NE(stage,rows.end());
    // The agent's jobs_cancel {token}: this job only, not its run.
    EXPECT_EQ(Runtime::CancelEditorJob(Runtime::PrepareEditorProcessingCommands(*Shell),stage->Token),
        Runtime::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled);
    ASSERT_TRUE(Until([&]{return Runtime::IsTerminal(StepRun().Phase);}));
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Discarded)<<"a cancel before the publication discards the run";
    Hold = false;
    ReleaseHeld(std::vector<std::byte>(12 * 4));
    for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Results.size(),1u);EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
}
TEST_F(ClusteringModuleResident, ADiscardedRunLeavesNoActiveRow)
{
    LendShell();
    Start();ASSERT_TRUE(Until([&]{return StepRunId()!=0u;}));
    const auto active=[&]{return std::ranges::any_of(Rows(),[](const auto& row){return Runtime::IsActiveEditorJobState(row.State);});};
    EXPECT_TRUE(active());
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled);
    EXPECT_TRUE(Until([&]{return !active();}))<<"the discarded run's holder is cancelled with it";
}
TEST_F(ClusteringModuleResident, AReplacingRequestDiscardsTheRunAndStartsOnceItsPageCompleted)
{
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        })); // A's first page is held
    const auto first=Correlation;const auto firstRun=StepRunId();
    auto replacing=Request;replacing.ReplaceRunning=true;
    replacing.Parameters.ClusterCount = 5;
    replacing.Parameters.Execution.IterationCap = 2; // a new capacity and its own cap
    const auto second=Service->RunKMeans(replacing);
    ASSERT_TRUE(Until([&]{return Results.size()==1;})); // the replaced run's completion, its page still held
    EXPECT_EQ(StepRun().Phase,Runtime::StepRunPhase::Discarded)<<"the running run is discarded";
    EXPECT_EQ(Service->GpuRun(second).StepRun,0u)<<"the replacement waits until the held page completed";
    EXPECT_EQ(Results[0].Correlation,first);EXPECT_EQ(Results[0].Status,Runtime::KMeansRunStatus::Cancelled);
    Hold = false;
    ReleaseHeld();
    Correlation=second;RunId=0;
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept&&StepRunId()!=0u&&StepRun().End!=Runtime::StepRunEnd::None;}));
    EXPECT_NE(StepRunId(),firstRun)<<"a new step run";
    EXPECT_EQ(StepRun().Steps,2u)<<"with its own cap";
    (void)Service->GpuRun(second,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until([&]{return Results.size()==2;}));
    EXPECT_EQ(Results[1].Correlation,second);EXPECT_TRUE(Results[1].Succeeded())<<Results[1].Message;
    EXPECT_EQ(Results[1].ClusterCount,5u);EXPECT_EQ(Results[1].Iterations,2u);
    EXPECT_EQ(Results[1].GpuWorkspacePipelinesCreated,0u)<<"the replaced run's workspace, free once its page completed";
}
TEST_F(ClusteringModuleResident, ASupersededRequestCompletesCancelledAndOnlyTheNewestStartsAfterAnAccept)
{
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    const auto a=Correlation;
    Hold=true;(void)Service->GpuRun(a,Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        })); // A's label readback is held
    auto replacing=Request;replacing.ReplaceRunning=true;
    replacing.Parameters.Execution.IterationCap = 3;
    const auto b = Service->RunKMeans(replacing);
    replacing.Parameters.Execution.IterationCap = 2;
    const auto c = Service->RunKMeans(replacing);
    const auto of=[&](const Runtime::CommandCorrelationId id){
        return std::ranges::count_if(Results,[&](const auto& result){return result.Correlation==id;});};
    ASSERT_TRUE(Until([&]{return of(b)==1;}));
    EXPECT_EQ(std::ranges::find_if(Results,[&](const auto& r){return r.Correlation==b;})->Status,
        Runtime::KMeansRunStatus::Cancelled)<<"superseded before it started";
    for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Service->GpuRun(c).StepRun,0u)<<"C waits for A's acceptance";EXPECT_EQ(of(a),0);
    Hold = false;
    ReleaseHeld(std::vector<std::byte>(12 * 4));
    ASSERT_TRUE(Until([&]{return of(a)==1;}));
    EXPECT_TRUE(std::ranges::find_if(Results,[&](const auto& r){return r.Correlation==a;})->Succeeded())
        <<"A's acceptance settled";
    Correlation=c;RunId=0;
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept&&StepRunId()!=0u&&StepRun().End!=Runtime::StepRunEnd::None;}));
    EXPECT_EQ(StepRun().Steps,2u)<<"C's cap";
    for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(of(b),1)<<"B completed once";
    EXPECT_EQ(Steps.Runs().size(),2u)<<"B never had a run";
}
TEST_F(ClusteringModuleResident, AReplacingRequestForAnotherOutputIsRefusedAsBusy)
{
    Start();ASSERT_TRUE(Until([&]{return StepRunId()!=0u;}));
    auto other=Request;other.ReplaceRunning=true;
    other.Properties.OutputLabels.Name="other_labels";other.Properties.OutputColors.Name="other_colors";
    other.Properties.OutputScalarLabels.reset();
    const auto refused=Service->RunKMeans(other);
    ASSERT_TRUE(Until([&]{return Results.size()==1;}));
    EXPECT_EQ(Results[0].Correlation,refused);EXPECT_FALSE(Results[0].Succeeded());
    EXPECT_NE(StepRun().Phase,Runtime::StepRunPhase::Discarded)<<"another output's run is not replaced";
}
TEST_F(ClusteringModuleResident, AReplacedRunsPreviewGoesAtOnce)
{
    const auto key=Runtime::MakeGpuPropertyKey(World,Entity,Request.Properties.OutputLabels);
    auto* residency=Cache.PropertyResidency();ASSERT_NE(residency,nullptr);
    Request.Parameters.Execution.PreviewEvery = 1;
    Request.Parameters.Execution.IterationCap = 10;
    // The first iteration page after a preview copy is held (on the submitting worker, under the mock's lock).
    HoldWhen = [&](std::uint64_t bytes, const std::vector<std::uint32_t>&, std::size_t)
    {
        return bytes != 0u && PreviewCopySubmissions.load() >= 1u;
    };
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRunId() != 0u;
        }));
    for (int i = 0; i < 3; ++i)
        Tick(); // the owner offers its slot
    ASSERT_TRUE(Control(Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending() && residency->Front(key);
        }))
        << "a preview is shown";
    SetHoldWhen({});
    auto replacing=Request;replacing.ReplaceRunning=true;
    const auto second=Service->RunKMeans(replacing);
    EXPECT_TRUE(Until([&]{return !residency->Front(key);}))<<"the replaced run's preview goes, its page still in flight";
    EXPECT_EQ(Service->GpuRun(second).StepRun,0u);
    ReleaseHeld();
    Correlation=second;RunId=0;
    ASSERT_TRUE(Until([&]{return StepRunId()!=0u;}))<<"the replacement starts once the page completed";
}
TEST_F(ClusteringModuleResident, AHeldRequestStartsOnlyInTheWorldItWasAdmittedIn)
{
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    auto replacing=Request;replacing.ReplaceRunning=true;
    const auto second=Service->RunKMeans(replacing);
    for(int i=0;i<3;++i)Tick(); // admitted and held
    const auto other=Worlds.CreateWorld("other");
    ASSERT_TRUE(Worlds.RequestSetActiveWorld(other));(void)Worlds.ApplyMaintenance(Events,Jobs.Jobs());
    ASSERT_EQ(Worlds.ActiveWorld(),other);
    Hold = false;
    ReleaseHeld();
    const auto find=[&]{return std::ranges::find_if(Results,[&](const auto& r){return r.Correlation==second;});};
    ASSERT_TRUE(Until([&]{return find()!=Results.end();}));
    EXPECT_EQ(find()->Status,Runtime::KMeansRunStatus::StaleWorld);
    EXPECT_EQ(find()->World,World)<<"reported in the world it was admitted in";
    for(int i=0;i<5;++i)Tick();
    EXPECT_EQ(Steps.Runs().size(),1u)<<"it never ran";
}
TEST_F(ClusteringModuleResident, AnInadmissibleReplacementDiscardsNeitherTheRunNorAHeldRequest)
{
    Hold = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HeldPending();
        }));
    auto replacing=Request;replacing.ReplaceRunning=true;
    auto invalid=replacing;invalid.Parameters.ClusterCount=0;
    const auto of=[&](const Runtime::CommandCorrelationId id){
        return std::ranges::count_if(Results,[&](const auto& result){return result.Correlation==id;});};
    const auto bad=Service->RunKMeans(invalid);
    ASSERT_TRUE(Until([&]{return of(bad)==1;}));
    EXPECT_FALSE(Results.back().Succeeded());
    EXPECT_NE(StepRun().Phase,Runtime::StepRunPhase::Discarded)<<"the running run stays";
    const auto held=Service->RunKMeans(replacing); // a valid one replaces it and waits
    ASSERT_TRUE(Until([&]{return StepRun().Phase==Runtime::StepRunPhase::Discarded;}));
    const auto worse=Service->RunKMeans(invalid);
    ASSERT_TRUE(Until([&]{return of(worse)==1;}));
    EXPECT_EQ(of(held),0)<<"the held request is not superseded by an inadmissible one";
    Hold = false;
    ReleaseHeld();
    Correlation=held;RunId=0;
    ASSERT_TRUE(Until([&]{return StepRunId()!=0u;}))<<"the held request starts";
}
// ARCH-022 Q79 step 6: a request on the cloud the previous request captured, while its positions and deletion mask
// keep their revisions, reuses that capture (no scan, no copy), also when it replaces the running run; a write to
// either, a resize, another positions property, the positions' recreation, another entity or another world captures
// afresh.
TEST_F(ClusteringModuleResident, ARequestOnAnUnchangedCloudReusesTheCaptureAndEveryInputChangeCapturesAfresh)
{
    const auto reuses=[&]{return Module.Stats().CaptureReuses;};
    const auto run=[&](Runtime::KMeansGpuAction end){
        const auto before=Results.size();Start();
        EXPECT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
        (void)Service->GpuRun(Correlation,end);
        EXPECT_TRUE(Until([&]{return Results.size()==before+1;}));
        return Results.back();
    };
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(reuses(),0u);
    auto replacing=Request;replacing.ReplaceRunning=true;
    Correlation=Service->RunKMeans(replacing);RunId=0;
    ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    EXPECT_EQ(reuses(),1u)<<"the replacing request reuses the replaced run's capture";
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
    ASSERT_TRUE(Until([&]{return Results.size()==2;}));
    const auto fresh=[&](const char* change,std::uint32_t live){
        SCOPED_TRACE(change);
        const auto before=reuses();
        const auto completed=run(Runtime::KMeansGpuAction::Accept);
        EXPECT_TRUE(completed.Succeeded())<<completed.Message;
        EXPECT_EQ(completed.LabelCount,live)<<"the capture's live rows";
        EXPECT_EQ(reuses(),before)<<"captured afresh";
        (void)run(Runtime::KMeansGpuAction::Discard);
        EXPECT_EQ(reuses(),before+1)<<"and that capture is kept for the next request";
    };
    Properties().Get<glm::vec3>(std::string{PN::kPosition}).Vector()[3]=glm::vec3(5,5,5);
    fresh("a position write",12);
    {
        auto other=Request;other.Properties.InputPositions.Name="p:other_input";
        Properties().GetOrAdd<glm::vec3>("p:other_input",glm::vec3(4,5,6));
        std::swap(Request,other);fresh("another positions property",12);std::swap(Request,other);
        fresh("back to the first positions",12);
    }
    Properties().GetOrAdd<bool>("v:deleted",false)[4]=true;
    fresh("a new deletion mask",11);
    Properties().Get<bool>("v:deleted")[5]=true;
    fresh("a deletion-mask write",10);
    Properties().Resize(13);
    fresh("a resize",11);
    {
        const auto positions=Properties().Get<glm::vec3>(std::string{PN::kPosition}).Vector();
        auto old=Properties().Get<glm::vec3>(std::string{PN::kPosition});Properties().Remove(old);
        Properties().GetOrAdd<glm::vec3>(std::string{PN::kPosition},glm::vec3{0}).Vector()=positions;
    }
    fresh("the positions removed and added again with the same values",11);
    const auto values=Properties().Get<glm::vec3>(std::string{PN::kPosition}).Vector();
    Scene().Raw().destroy(Entity);Entity=Scene().Create();
    SetPositions(Scene().Raw().emplace<GS::Vertices>(Entity),values);
    Request.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entity);
    fresh("another entity",13);
    World=Worlds.CreateWorld("other");ASSERT_TRUE(Worlds.RequestSetActiveWorld(World));
    (void)Worlds.ApplyMaintenance(Events,Jobs.Jobs());ASSERT_EQ(Worlds.ActiveWorld(),World);
    Tick(); // the world change is delivered (it releases the capture) before the next request
    Entity=Scene().Create();SetPositions(Scene().Raw().emplace<GS::Vertices>(Entity),values);
    Request.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entity);
    fresh("another world",13);
}
// ARCH-022 Q79 step 6: a capture is made only in the active world, so a change of the active world (a document switched
// or closed) releases it; coming back captures afresh although the cloud kept its revisions.
TEST_F(ClusteringModuleResident, AnActiveWorldChangeReleasesTheCapture)
{
    const auto run=[&]{
        const auto before=Results.size();Start();
        EXPECT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
        (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);
        EXPECT_TRUE(Until([&]{return Results.size()==before+1;}));
        return Module.Stats().CaptureReuses;
    };
    (void)run();ASSERT_EQ(run(),1u)<<"kept while the world stays active";
    EXPECT_EQ(Module.Stats().CachedCapturePoints,12u);
    const auto switchTo=[&](Runtime::WorldHandle world){
        ASSERT_TRUE(Worlds.RequestSetActiveWorld(world));(void)Worlds.ApplyMaintenance(Events,Jobs.Jobs());
        ASSERT_EQ(Worlds.ActiveWorld(),world);Tick();
    };
    const auto first=World;
    switchTo(Worlds.CreateWorld("other"));
    EXPECT_EQ(Module.Stats().CachedCapturePoints,0u)<<"released";
    switchTo(first);
    EXPECT_EQ(run(),1u)<<"released by the world change, so captured afresh";
    EXPECT_EQ(run(),2u);
}
// ARCH-022 Q79 step 6 on the CPU reference path (it consumes the same snapshot): a run on a reused capture gives the
// fresh run's result bit for bit, a run after a position write the reference result of the new positions, and the
// undo of a run whose capture was shared still validates exactly against its own capture.
TEST_F(ClusteringModuleResident, CpuRunsOnAReusedCaptureEqualTheReferenceAndSeeAPositionWrite)
{
    namespace GK = Geometry::KMeans;
    std::vector<glm::vec3> points;
    for(int i=0;i<12;++i)
        points.push_back(glm::vec3(i/4==1?10.f:0.f,i/4==2?10.f:0.f,0.f)+0.25f*float(i%4)*glm::vec3(1,1,1));
    Properties().Get<glm::vec3>(std::string{PN::kPosition}).Vector()=points;
    Request.Backend=Runtime::ClusteringBackend::CpuReference;
    Request.Parameters.Execution.AutoAccept =
        Runtime::StepRunAutoAccept::End; // a batch: accepted at its end (ARCH-023)
    const auto reference=[&]{
        GK::KMeansParams params{};params.ClusterCount=Request.Parameters.ClusterCount;
        params.MaxIterations = Request.Parameters.Execution.IterationCap;
        params.Seed = Request.Parameters.Seed;
        params.Init=Request.Parameters.Initialization==Runtime::KMeansInitialization::Hierarchical
            ?GK::Initialization::Hierarchical:GK::Initialization::Random;
        auto result=GK::Cluster(std::span<const glm::vec3>{points},params);
        EXPECT_TRUE(result.has_value());return result?*result:GK::KMeansResult{};
    };
    const auto labels=[&]{
        const auto property=Properties().Get<std::uint32_t>("p:kmeans_label");
        return property?property.Vector():std::vector<std::uint32_t>{};
    };
    const auto run=[&]{
        const auto before=Results.size();Start();
        EXPECT_TRUE(Until([&]{return Results.size()==before+1;}));
        EXPECT_TRUE(Results.back().Succeeded())<<Results.back().Message;
        return std::pair{Results.back(),labels()};
    };
    const auto expected=reference();
    const auto [first,firstLabels]=run();
    EXPECT_EQ(Module.Stats().CaptureReuses,0u);
    EXPECT_EQ(firstLabels,expected.Labels);EXPECT_EQ(first.Centroids,expected.Centroids);
    const auto [second,secondLabels]=run();
    EXPECT_EQ(Module.Stats().CaptureReuses,1u)<<"the unchanged cloud's capture";
    EXPECT_EQ(secondLabels,firstLabels);EXPECT_EQ(second.Centroids,first.Centroids);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(second.Inertia),std::bit_cast<std::uint32_t>(first.Inertia));
    ASSERT_TRUE(History.Undo().Succeeded());EXPECT_EQ(labels(),firstLabels);
    ASSERT_TRUE(History.Redo().Succeeded());EXPECT_EQ(labels(),secondLabels);
    std::swap(points[0],points[4]);
    Properties().Get<glm::vec3>(std::string{PN::kPosition}).Vector()=points;
    const auto moved=reference();
    ASSERT_NE(moved.Labels,expected.Labels)<<"the write changes the result, so a stale capture would show";
    const auto [third,thirdLabels]=run();
    EXPECT_EQ(Module.Stats().CaptureReuses,1u)<<"captured afresh";
    EXPECT_EQ(thirdLabels,moved.Labels);EXPECT_EQ(third.Centroids,moved.Centroids);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(third.Inertia),std::bit_cast<std::uint32_t>(moved.Inertia));
    ASSERT_TRUE(History.Undo().Succeeded());EXPECT_EQ(labels(),secondLabels);
    EXPECT_FALSE(History.Undo().Succeeded())<<"the second run's capture holds the old positions";
}
// ARCH-022 Q79 step 7b: Update tiles hold at most 64 centroids (one combine lane each) and together cover every
// centroid once per point chunk, also when K exceeds 64 at a small N (128 points, 128 clusters: two tiles).
TEST_F(ClusteringModuleResident, UpdateTilesHoldAtMost64CentroidsAndCoverEveryCentroid)
{
    std::vector<glm::vec3> points;
    for(int i=0;i<128;++i)points.push_back(glm::vec3(float(i),0,0));
    SetPositions(Scene().Raw().get<GS::Vertices>(Entity),points);
    Request.Parameters.ClusterCount = 128;
    Request.Parameters.Execution.IterationCap = 1;
    const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    std::map<std::uint32_t,std::vector<std::uint32_t>> covered; // point chunk -> centroids, from Update pass A
    for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i){
        const auto& push=Device.CommandContext.PushConstantPayloads[i];if(push.size()!=128u)continue;
        std::uint32_t phase{},first{},rows{},innerFirst{};
        std::memcpy(&phase,push.data()+104,4);std::memcpy(&first,push.data()+108,4);
        std::memcpy(&rows,push.data()+112,4);std::memcpy(&innerFirst,push.data()+116,4);
        if(phase!=2)continue;
        EXPECT_LE(rows,64u);
        for(std::uint32_t c=first;c<first+rows;++c)covered[innerFirst].push_back(c);
    }
    ASSERT_FALSE(covered.empty());
    std::vector<std::uint32_t> every(128);std::iota(every.begin(),every.end(),0u);
    for(auto& [chunk,centroids]:covered){std::ranges::sort(centroids);EXPECT_EQ(centroids,every)<<"chunk "<<chunk;}
}
// ARCH-022 Q79 step 7b: a submission budget of one page still advances (every batch then holds one page), and
// K = 1024 records within the batch scratch (pages and tile rows at most 64, checked by the fixture).
TEST_F(ClusteringModuleResident, OnePageSubmissionsAndAThousandClustersRecordWithinTheBatchBounds)
{
    Runtime::KMeansPagingForTesting={.PagePairs=4,.SubmissionPairs=4};
    const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    std::size_t batches=0;
    for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i){
        const auto& push=Device.CommandContext.PushConstantPayloads[i];if(push.size()!=128u)continue;
        std::uint32_t phase{},pages{},clusters{};
        std::memcpy(&phase,push.data()+104,4);std::memcpy(&pages,push.data()+120,4);std::memcpy(&clusters,push.data()+100,4);
        if(phase==3){EXPECT_EQ(pages,1u);++batches;}
        if(phase==2){EXPECT_EQ(clusters,1u);++batches;}
    }
    EXPECT_GT(batches,0u);
    (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);ASSERT_TRUE(Until([&]{return Results.size()==1;}));

    Runtime::KMeansPagingForTesting={};
    std::vector<glm::vec3> points;
    for(int i=0;i<2048;++i)points.push_back(glm::vec3(float(i%1024),float(i/1024),0));
    SetPositions(Scene().Raw().get<GS::Vertices>(Entity),points);
    Request.Parameters.ClusterCount = 1024;
    Request.Parameters.Execution.IterationCap = 1;
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}))<<Observation().Message;
}
// ARCH-022 Q79 step 7b: a batch's pass A runs one workgroup per page (Update: per tile centroid and page) and its
// combine a single workgroup; the dispatch grids match the pushed page counts, with several pages per batch.
TEST_F(ClusteringModuleResident, BatchedDispatchesRunOneWorkgroupPerPage)
{
    Runtime::KMeansPagingForTesting={.PagePairs=16,.SubmissionPairs=1u<<24};
    std::vector<glm::vec3> points;
    for(int i=0;i<128;++i)points.push_back(glm::vec3(float(i%4)*10.0f,float(i),0));
    SetPositions(Scene().Raw().get<GS::Vertices>(Entity),points);
    Request.Parameters.ClusterCount = 4;
    Request.Parameters.Execution.IterationCap = 1;
    const auto firstPush=Device.CommandContext.PushConstantPayloads.size();
    const auto firstDispatch=Device.CommandContext.DispatchRecords.size();
    Start();ASSERT_TRUE(Until([&]{return Observation().ReadyToAccept;}));
    // Every k-means Record pushes 128 bytes and then dispatches once, so the two logs pair up in order.
    std::vector<std::vector<std::byte>> pushes;
    for(std::size_t i=firstPush;i<Device.CommandContext.PushConstantPayloads.size();++i)
        if(Device.CommandContext.PushConstantPayloads[i].size()==128u)pushes.push_back(Device.CommandContext.PushConstantPayloads[i]);
    const auto& dispatches=Device.CommandContext.DispatchRecords;
    ASSERT_EQ(dispatches.size()-firstDispatch,pushes.size());
    std::uint32_t largestReduce=0,largestUpdate=0;
    for(std::size_t i=0;i<pushes.size();++i){
        std::uint32_t clusters{},phase{},rows{},columns{};
        std::memcpy(&clusters,pushes[i].data()+100,4);std::memcpy(&phase,pushes[i].data()+104,4);
        std::memcpy(&rows,pushes[i].data()+112,4);std::memcpy(&columns,pushes[i].data()+120,4);
        const auto& d=dispatches[firstDispatch+i];
        SCOPED_TRACE(i);
        if(phase==3){EXPECT_EQ(std::tuple(d.X,d.Y,d.Z),std::tuple(columns,1u,1u));largestReduce=std::max(largestReduce,columns);}
        if(phase==2){EXPECT_EQ(std::tuple(d.X,d.Y,d.Z),std::tuple(rows,clusters,1u));largestUpdate=std::max(largestUpdate,clusters);}
        if(phase==6||phase==7)EXPECT_EQ(std::tuple(d.X,d.Y,d.Z),std::tuple(1u,1u,1u));
    }
    EXPECT_GT(largestReduce,1u)<<"a Reduce batch of several pages";
    EXPECT_GT(largestUpdate,1u)<<"an Update batch of several point chunks";
}

// ARCH-023 L1 at the second owner: an interval preview is counted once its copy is submitted. With the label ring's
// free slots held elsewhere after the first preview, the previews at iterations 4 and 6 are dropped (the residency
// counts them) and the step run's copy count stays 1; the terminal labels wait for a slot and the run ends as before.
// Predictions: PreviewCopies 1 (counting a copy that found no free slot gives 3), Previews 2 (one interval copy and the
// terminal labels), at least 2 dropped previews. Mutation: counting a Preview that made no copy.
TEST_F(ClusteringModuleResident, ABusyLabelRingDropsLivePreviewsWithoutCountingThem)
{
    Request.Parameters.Execution.IterationCap = 8;
    Request.Parameters.Execution.PreviewEvery = 2;
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    auto typed = Request.Properties.OutputLabels;
    typed.ValueKind = Geometry::PropertyValueKind::UInt32;
    const auto key = Runtime::MakeGpuPropertyKey(World, Entity, typed);
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRunId() != 0u;
        }));
    for (int i = 0; i < 3; ++i)
        Tick(); // the owner offers its slot
    ASSERT_TRUE(Control(Runtime::StepRunAction::Step, 2u));
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().Previews == 1u;
        },
        false)) // publish the completed copy before the owner reserves its next mailbox slot
        << "the copy after iteration 2 published";
    ASSERT_LT(Observation().Iterations, 4u);
    const auto dropped = residency->Stats().DroppedPreviews;
    const auto front = residency->Front(key);
    ASSERT_TRUE(front);
    // Every free slot of the ring (depth 3: the front and two more), held elsewhere.
    std::vector<decltype(residency->AcquireBack(key, front->Layout, 3u))> held;
    for (int i = 0; i < 2; ++i)
        if (auto slot = residency->AcquireBack(key, front->Layout, 3u))
            held.push_back(std::move(slot));
    ASSERT_EQ(held.size(), 2u);
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRun().Phase == Runtime::StepRunPhase::Paused;
        }));
    ASSERT_TRUE(Control(Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().Iterations == 8u;
        }));
    for (int i = 0; i < 5; ++i)
        Tick();
    EXPECT_FALSE(Observation().ReadyToAccept) << "the terminal labels wait for a slot";
    EXPECT_EQ(Observation().Previews, 1u);
    EXPECT_EQ(StepRun().PreviewCopies, 1u) << "the dropped previews are not counted";
    EXPECT_GE(residency->Stats().DroppedPreviews - dropped, 2u) << "4 and 6 were dropped";
    held.clear();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    EXPECT_EQ(Observation().Previews, 2u) << "the interval copy and the terminal labels";
    EXPECT_EQ(StepRun().PreviewCopies, 1u) << "the terminal labels are the result, not a live preview";
}

namespace
{
    // ARCH-023 B8: the CPU reference as a step run, on the hand-worked cloud of PointCloud_KMeans.LloydSteps* (three
    // Lloyd iterations to convergence; a stop after one keeps labels [0,0,1,1] for the moved centroids).
    class ClusteringModuleCpuStepRun : public ClusteringModuleResident
    {
    protected:
        void UseHandWorkedCloud()
        {
            SetPositions(Scene().Raw().get<GS::Vertices>(Entity),
                std::vector<glm::vec3>{{0, 0, 0}, {1, 0, 0}, {4, 0, 0}, {6, 0, 0}});
            Request.Backend = Runtime::ClusteringBackend::CpuReference;
            Request.Parameters.ClusterCount = 2;
            Request.Parameters.Initialization = Runtime::KMeansInitialization::Hierarchical;
            Request.Parameters.Execution = {.IterationCap = 8u,
                .PreviewEvery = 0u,
                .PreviewIntervalMs = 0u,
                .AutoAccept = Runtime::StepRunAutoAccept::Never,
                .Start = Runtime::StepRunStart::Ready};
        }
        std::uint64_t CpuRun()
        {
            return Service->StepRun(Correlation);
        }
        Runtime::StepRunObservation Cpu()
        {
            const auto run = Steps.Observe(CpuRun());
            EXPECT_TRUE(run);
            return run ? *run : Runtime::StepRunObservation{};
        }
        bool CpuControl(const Runtime::StepRunAction action, const std::uint32_t count = 1u)
        {
            std::string refusal;
            const bool done = Steps.Control(CpuRun(), action, count, refusal);
            EXPECT_TRUE(done) << refusal;
            return done;
        }
        bool Rests(const std::uint32_t steps)
        {
            const auto run = Cpu();
            return run.Phase == Runtime::StepRunPhase::Paused && !run.SegmentInFlight && run.Steps == steps;
        }
        std::vector<std::uint32_t> Labels()
        {
            const auto property = Properties().Get<std::uint32_t>("p:kmeans_label");
            return property ? property.Vector() : std::vector<std::uint32_t>{};
        }
    };
}

TEST_F(ClusteringModuleCpuStepRun, ReadyDoesNoWorkStepStopAcceptsThePartialResultWithOneUndoEntry)
{
    UseHandWorkedCloud();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return CpuRun() != 0u;
        }))
        << "the CPU request started a step run";
    for (int i = 0; i < 10; ++i)
        Tick();
    auto run = Cpu();
    EXPECT_EQ(run.Phase, Runtime::StepRunPhase::Ready);
    EXPECT_EQ(run.Steps, 0u) << "a ready run computes nothing";
    EXPECT_TRUE(run.CanRestart);
    EXPECT_EQ(run.Owner, "Runtime.ClusteringModule");
    EXPECT_TRUE(Results.empty());
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(Until(
        [&]
        {
            return Rests(1u);
        }));
    run = Cpu();
    ASSERT_EQ(run.Metrics->Records().size(), 1u);
    EXPECT_EQ(run.Metrics->Records()[0].Values[0], 12.1875) << "step 1's inertia (hand-worked)";
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Stop));
    ASSERT_TRUE(Until(
        [&]
        {
            return Cpu().End == Runtime::StepRunEnd::Stopped;
        }));
    for (int i = 0; i < 5; ++i)
        Tick();
    EXPECT_TRUE(Results.empty()) << "a stopped run waits for Accept";
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].ActualBackend, Runtime::ClusteringBackend::CpuReference);
    EXPECT_EQ(Results[0].Iterations, 1u);
    EXPECT_FALSE(Results[0].Converged);
    EXPECT_EQ(Results[0].StepRun, CpuRun()) << "the result names its run";
    EXPECT_EQ(Labels(), (std::vector<std::uint32_t>{0u, 0u, 1u, 1u})) << "the labels of the moved centroids";
    EXPECT_EQ(Cpu().Phase, Runtime::StepRunPhase::Committed);
    EXPECT_EQ(History.UndoCount(), 1u);
    std::string refusal;
    EXPECT_FALSE(Steps.Control(CpuRun(), Runtime::StepRunAction::Accept, 1u, refusal)) << "one Accept";
}

TEST_F(ClusteringModuleCpuStepRun, StepsOfOneEqualTheRunAndARestartAfterConvergenceComputesAgain)
{
    UseHandWorkedCloud();
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Run;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return CpuRun() != 0u && Cpu().End == Runtime::StepRunEnd::Converged && Rests(3u);
        }));
    EXPECT_EQ(Cpu().Steps, 3u) << "the CPU counts one step per Lloyd iteration, as the reference converges";
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Restart)) << "after convergence, before Accept";
    ASSERT_TRUE(Until(
        [&]
        {
            return Cpu().Generation == 1u && Cpu().End == Runtime::StepRunEnd::Converged && Rests(3u);
        }))
        << "a running run restarts running and converges again";
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].Iterations, 3u);
    EXPECT_TRUE(Results[0].Converged);
    EXPECT_EQ(Results[0].Inertia, 2.5f);
    const auto whole = Labels();
    EXPECT_EQ(whole, (std::vector<std::uint32_t>{0u, 0u, 1u, 1u}));
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));

    // The same run in steps of one, from ready.
    Request.Parameters.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return CpuRun() != 0u;
        }));
    for (std::uint32_t step = 1; step <= 3; ++step)
    {
        ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Step, 1u));
        ASSERT_TRUE(Until(
            [&]
            {
                return Rests(step);
            }))
            << step;
    }
    EXPECT_EQ(Cpu().End, Runtime::StepRunEnd::Converged);
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 2u;
        }));
    ASSERT_TRUE(Results[1].Succeeded()) << Results[1].Message;
    EXPECT_EQ(Results[1].Iterations, Results[0].Iterations);
    EXPECT_EQ(Results[1].Centroids, Results[0].Centroids);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(Results[1].Inertia), std::bit_cast<std::uint32_t>(Results[0].Inertia));
    EXPECT_EQ(Labels(), whole);
}

TEST_F(ClusteringModuleCpuStepRun, AStaleSourceRefusesAcceptAndADiscardPublishesNothing)
{
    UseHandWorkedCloud();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return CpuRun() != 0u;
        }));
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(Until(
        [&]
        {
            return Rests(1u);
        }));
    Properties().Get<glm::vec3>(std::string{PN::kPosition})[0].x = 0.5f; // the source changes before Accept
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results[0].Status, Runtime::KMeansRunStatus::StaleSource) << Results[0].Message;
    EXPECT_EQ(Cpu().Phase, Runtime::StepRunPhase::Discarded);
    std::string refusal;
    EXPECT_FALSE(Steps.Control(CpuRun(), Runtime::StepRunAction::Accept, 1u, refusal)) << "a stale run accepts nothing";
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    EXPECT_FALSE(History.CanUndo());

    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return CpuRun() != 0u;
        }));
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Step, 2u));
    ASSERT_TRUE(Until(
        [&]
        {
            return Rests(2u);
        }));
    ASSERT_TRUE(CpuControl(Runtime::StepRunAction::Discard));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 2u;
        }));
    EXPECT_EQ(Results[1].Status, Runtime::KMeansRunStatus::Cancelled) << Results[1].Message;
    EXPECT_EQ(Results[1].StepRun, CpuRun());
    EXPECT_FALSE(Properties().Exists("p:kmeans_label"));
    EXPECT_FALSE(History.CanUndo());
}

// ARCH-023: a GPU run whose final labels are prepared (it ended at its cap) restarts before Accept on its own
// workspace and transaction: a new generation computes again and its Accept publishes once.
TEST_F(ClusteringModuleResident, AGpuRunRestartsAfterItsFinalLabelsBeforeAccept)
{
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept && StepRun().End == Runtime::StepRunEnd::IterationCap;
        }));
    const auto submissions = Observation().Submissions;
    ASSERT_TRUE(Control(Runtime::StepRunAction::Restart)) << "final labels prepared, not yet accepted";
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRun().Generation == 1u;
        }))
        << StepRun().Diagnostic;
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept && StepRun().Steps == 4u;
        }));
    EXPECT_GT(Observation().Submissions, submissions) << "the new generation recorded its own pages";
    EXPECT_TRUE(Results.empty());
    EXPECT_FALSE(History.CanUndo());
    (void)Service->GpuRun(Correlation, Runtime::KMeansGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].Iterations, 4u);
    EXPECT_EQ(History.UndoCount(), 1u);
}

// ARCH-023 query-review B2: K-Means pages follow the compute lane alone once a page of the run waited for its inputs'
// transfers. The run's first submission, and the first after the restart's reseed upload (any submission that follows
// a transfer-queue upload), use AfterIssuedTransfers; the iterations between use LaneOrderOnly. Predictions, before
// any run: first entry AfterIssuedTransfers; at least one upload-following submission (the reseed's; the Start uploads
// precede the first entry), all AfterIssuedTransfers;
// LaneOrderOnly pages present. Mutations: InputOrdered never set (no LaneOrderOnly page); not reset by the reseed
// (the page after the reseed upload is LaneOrderOnly).
TEST_F(ClusteringModuleResident, KMeansPagesWaitForIssuedTransfersOnlyAfterStartAndAReseed)
{
    using O = Extrinsic::RHI::ComputeInputOrder;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept && StepRun().End == Runtime::StepRunEnd::IterationCap;
        }));
    ASSERT_TRUE(Control(Runtime::StepRunAction::Restart));
    ASSERT_TRUE(Until(
        [&]
        {
            return StepRun().Generation == 1u && Observation().ReadyToAccept && StepRun().Steps == 4u;
        }));
    std::scoped_lock lock{Gpu};
    ASSERT_FALSE(OrderLog.empty());
    EXPECT_EQ(OrderLog.front().second, O::AfterIssuedTransfers) << "the run's first page follows the Start uploads";
    std::size_t afterUploads = 0u, laneOrder = 0u;
    for (std::size_t i = 0; i < OrderLog.size(); ++i)
    {
        laneOrder += OrderLog[i].second == O::LaneOrderOnly;
        if (i > 0u && OrderLog[i].first != OrderLog[i - 1u].first)
        {
            ++afterUploads;
            EXPECT_EQ(OrderLog[i].second, O::AfterIssuedTransfers) << "submission " << i << " follows an upload";
        }
    }
    EXPECT_GE(afterUploads, 1u) << "the restart's reseed upload";
    EXPECT_GT(laneOrder, 0u) << "iterations after the first page follow the lane alone";
}
