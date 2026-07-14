#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SelectedEntityAnalysisModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    using namespace std::chrono_literals;

    struct MutateSelectedNormal
    {
        std::uint32_t StableEntityId{0u};
    };

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

    [[nodiscard]] ECS::EntityHandle AddAnalysisMesh(
        ECS::Scene::Registry& scene,
        const std::size_t vertexCount)
    {
        const ECS::EntityHandle entity = scene.Create();
        entt::registry& raw = scene.Raw();

        auto& vertices = raw.emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(vertexCount);
        auto positions = vertices.Properties.GetOrAdd<glm::vec3>(
            "v:position", glm::vec3{0.0f});
        auto normals = vertices.Properties.GetOrAdd<glm::vec3>(
            "v:analysis_normal", glm::vec3{0.0f, 0.0f, 1.0f});
        auto colors = vertices.Properties.GetOrAdd<glm::vec4>(
            "v:analysis_color", glm::vec4{1.0f});
        auto texcoords = vertices.Properties.GetOrAdd<glm::vec2>(
            "v:texcoord", glm::vec2{0.0f});
        for (std::size_t i = 0u; i < vertexCount; ++i)
        {
            positions.Vector()[i] =
                glm::vec3{static_cast<float>(i), 0.0f, 0.0f};
            normals.Vector()[i] = glm::vec3{0.0f, 0.0f, 1.0f};
            colors.Vector()[i] = glm::vec4{1.0f};
            texcoords.Vector()[i] = glm::vec2{
                static_cast<float>(i) /
                    static_cast<float>(vertexCount),
                0.5f};
        }
        normals.Vector().front() = glm::vec3{0.0f};
        colors.Vector().back().x =
            std::numeric_limits<float>::quiet_NaN();

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(1u);
        (void)edges.Properties.GetOrAdd<std::uint32_t>("e:v0", 0u);
        (void)edges.Properties.GetOrAdd<std::uint32_t>("e:v1", 1u);

        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(1u);
        (void)halfedges.Properties.GetOrAdd<std::uint32_t>(
            "h:to_vertex", 0u);
        (void)halfedges.Properties.GetOrAdd<std::uint32_t>("h:next", 0u);
        (void)halfedges.Properties.GetOrAdd<std::uint32_t>("h:face", 0u);

        auto& faces = raw.emplace<GS::Faces>(entity);
        faces.Properties.Resize(1u);
        (void)faces.Properties.GetOrAdd<std::uint32_t>("f:halfedge", 0u);

        raw.emplace<Runtime::VertexChannelBindingSet>(
            entity,
            Runtime::VertexChannelBindingSet{
                .Normal = Runtime::VertexChannelSourceBinding{
                    .Enabled = true,
                    .SourceType = Runtime::AttributeSourceType::Vec3,
                    .SourceProperty = "v:analysis_normal",
                },
                .Color = Runtime::VertexChannelSourceBinding{
                    .Enabled = true,
                    .SourceType = Runtime::AttributeSourceType::Vec4,
                    .SourceProperty = "v:analysis_color",
                },
                .BindingGeneration = 7u,
            });
        return entity;
    }

    [[nodiscard]] bool AllConsumersReady(
        Runtime::SelectedEntityAnalysisService& service,
        const Runtime::WorldHandle world,
        const std::uint32_t stableEntityId)
    {
        for (const Runtime::SelectedEntityAnalysisConsumer consumer : {
                 Runtime::SelectedEntityAnalysisConsumer::Inspector,
                 Runtime::SelectedEntityAnalysisConsumer::MeshDomainWindow,
                 Runtime::SelectedEntityAnalysisConsumer::GraphDomainWindow,
                 Runtime::SelectedEntityAnalysisConsumer::PointCloudDomainWindow})
        {
            if (service.Query(world, stableEntityId, consumer).State !=
                Runtime::SelectedEntityAnalysisState::Ready)
            {
                return false;
            }
        }
        return true;
    }

    class AnalysisSuccessApp final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Service = engine.Services().Find<
                Runtime::SelectedEntityAnalysisService>();
            if (Service == nullptr || !Service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            World = engine.ActiveWorld();
            Entity = AddAnalysisMesh(engine.GetScene(), 4096u);
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);

            First = Service->Request(
                World,
                StableEntityId,
                Runtime::SelectedEntityAnalysisConsumer::Inspector);
            (void)engine.GetSelectionController()
                .SetSelectedByStableEntityId(
                    engine.GetScene(), StableEntityId);
            Runtime::SandboxEditorModelBuildRequest buildRequest{};
            buildRequest.Hierarchy = false;
            buildRequest.Selection = false;
            buildRequest.Document = false;
            buildRequest.SceneFile = false;
            buildRequest.FileImport = false;
            buildRequest.AssetImportQueue = false;
            buildRequest.RenderGraph = false;
            buildRequest.RenderRecipe = false;
            buildRequest.CameraRender = false;
            buildRequest.Visualization = false;
            const Runtime::SandboxEditorPanelFrame pendingFrame =
                Runtime::BuildSandboxEditorPanelFrame(
                    Runtime::SandboxEditorContext{
                        .Scene = &engine.GetScene(),
                        .ActiveWorld = World,
                        .SelectedEntityAnalysis = Service,
                        .Selection = &engine.GetSelectionController(),
                    },
                    buildRequest);
            PendingFrameStats = pendingFrame.ModelBuildStats;
            PendingFrameAnalysisState =
                pendingFrame.Inspector.PropertyCatalog.AnalysisState;
            Duplicate = Service->Request(
                World,
                StableEntityId,
                Runtime::SelectedEntityAnalysisConsumer::Inspector);
            (void)Service->Request(
                World,
                StableEntityId,
                Runtime::SelectedEntityAnalysisConsumer::MeshDomainWindow);
            (void)Service->Request(
                World,
                StableEntityId,
                Runtime::SelectedEntityAnalysisConsumer::GraphDomainWindow);
            (void)Service->Request(
                World,
                StableEntityId,
                Runtime::SelectedEntityAnalysisConsumer::PointCloudDomainWindow);
            ImmediateStats = Service->Stats();
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++Ticks;
            if (Service != nullptr &&
                AllConsumersReady(*Service, World, StableEntityId))
            {
                Ready = Service->Query(
                    World,
                    StableEntityId,
                    Runtime::SelectedEntityAnalysisConsumer::Inspector);
                CacheHit = Service->Request(
                    World,
                    StableEntityId,
                    Runtime::SelectedEntityAnalysisConsumer::Inspector);
                FinalStats = Service->Stats();
                engine.RequestExit();
                return;
            }
            if (Ticks > 300u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::SelectedEntityAnalysisService* Service{};
        Runtime::SelectedEntityAnalysisView First{};
        Runtime::SelectedEntityAnalysisView Duplicate{};
        Runtime::SelectedEntityAnalysisView Ready{};
        Runtime::SelectedEntityAnalysisView CacheHit{};
        Runtime::SelectedEntityAnalysisStats ImmediateStats{};
        Runtime::SelectedEntityAnalysisStats FinalStats{};
        Runtime::SandboxEditorModelBuildStats PendingFrameStats{};
        Runtime::SelectedEntityAnalysisState PendingFrameAnalysisState{
            Runtime::SelectedEntityAnalysisState::Missing};
        Runtime::WorldHandle World{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class AnalysisRevisionApp final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Service = engine.Services().Find<
                Runtime::SelectedEntityAnalysisService>();
            if (Service == nullptr || !Service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            World = engine.ActiveWorld();
            Entity = AddAnalysisMesh(engine.GetScene(), 8192u);
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);

            engine.Commands().RegisterHandler<MutateSelectedNormal>(
                [](Runtime::CommandContext& context,
                   const MutateSelectedNormal& command)
                    -> Runtime::CommandOutcome
                {
                    const ECS::EntityHandle entity =
                        Runtime::SelectionController::ToEntityHandle(
                            command.StableEntityId);
                    entt::registry& raw = context.ActiveWorld.Raw();
                    if (entity == ECS::InvalidEntityHandle ||
                        !raw.valid(entity) || !raw.all_of<GS::Vertices>(entity))
                    {
                        return Runtime::CommandOutcome::Fail(
                            "analysis test entity is stale");
                    }
                    auto normals = raw.get<GS::Vertices>(entity)
                                       .Properties.Get<glm::vec3>(
                                           "v:analysis_normal");
                    normals.Vector().front() = glm::vec3{1.0f, 0.0f, 0.0f};
                    Dirty::MarkVertexNormalsDirty(raw, entity);
                    return Runtime::CommandOutcome::Ok();
                });

            Extrinsic::Core::Tasks::Scheduler::Dispatch(
                [this]
                {
                    BlockerStarted.store(true, std::memory_order_release);
                    while (!ReleaseBlocker.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(1ms);
                });
            Initial = Service->Request(
                World,
                StableEntityId,
                Runtime::SelectedEntityAnalysisConsumer::Inspector);
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++Ticks;
            const Runtime::SelectedEntityAnalysisStats stats =
                Service != nullptr ? Service->Stats()
                                   : Runtime::SelectedEntityAnalysisStats{};
            if (!MutationQueued && stats.JobsSubmitted == 1u &&
                BlockerStarted.load(std::memory_order_acquire))
            {
                (void)engine.Commands().Enqueue(MutateSelectedNormal{
                    .StableEntityId = StableEntityId});
                MutationQueued = true;
            }

            if (MutationQueued && !ReplacementRequested)
            {
                const Runtime::SelectedEntityAnalysisView current =
                    Service->Query(
                        World,
                        StableEntityId,
                        Runtime::SelectedEntityAnalysisConsumer::Inspector);
                if (current.State ==
                    Runtime::SelectedEntityAnalysisState::Stale)
                {
                    Stale = current;
                    Replacement = Service->Request(
                        World,
                        StableEntityId,
                        Runtime::SelectedEntityAnalysisConsumer::Inspector);
                    ReplacementRequested = true;
                    ReleaseBlocker.store(true, std::memory_order_release);
                }
            }

            if (ReplacementRequested)
            {
                std::this_thread::sleep_for(1ms);
                const Runtime::SelectedEntityAnalysisView current =
                    Service->Query(
                        World,
                        StableEntityId,
                        Runtime::SelectedEntityAnalysisConsumer::Inspector);
                if (current.State ==
                    Runtime::SelectedEntityAnalysisState::Ready)
                {
                    Ready = current;
                    FinalStats = Service->Stats();
                    engine.RequestExit();
                    return;
                }
            }

            if (Ticks > 2000u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override
        {
            ReleaseBlocker.store(true, std::memory_order_release);
        }

        Runtime::SelectedEntityAnalysisService* Service{};
        Runtime::SelectedEntityAnalysisView Initial{};
        Runtime::SelectedEntityAnalysisView Stale{};
        Runtime::SelectedEntityAnalysisView Replacement{};
        Runtime::SelectedEntityAnalysisView Ready{};
        Runtime::SelectedEntityAnalysisStats FinalStats{};
        Runtime::WorldHandle World{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::atomic<bool> BlockerStarted{false};
        std::atomic<bool> ReleaseBlocker{false};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool MutationQueued{false};
        bool ReplacementRequested{false};
        bool TimedOut{false};
    };
}

TEST(SelectedEntityAnalysisModule,
     RequestsAreNonBlockingDeduplicatedCachedAndBoundedAtUiBuild)
{
    auto app = std::make_unique<AnalysisSuccessApp>();
    AnalysisSuccessApp* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.EmplaceModule<Runtime::SelectedEntityAnalysisModule>(1u);
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_EQ(appPtr->First.State,
              Runtime::SelectedEntityAnalysisState::Pending);
    EXPECT_EQ(appPtr->Duplicate.Key.RequestIdentity,
              appPtr->First.Key.RequestIdentity);
    EXPECT_EQ(appPtr->ImmediateStats.SnapshotsCaptured, 0u);
    EXPECT_EQ(appPtr->ImmediateStats.JobsSubmitted, 0u);
    EXPECT_EQ(appPtr->ImmediateStats.RequestDeduplications, 1u);
    EXPECT_EQ(appPtr->PendingFrameAnalysisState,
              Runtime::SelectedEntityAnalysisState::Pending);
    EXPECT_EQ(appPtr->PendingFrameStats.VertexChannelResolverScans, 0u);
    EXPECT_EQ(appPtr->PendingFrameStats.VertexChannelScratchAllocations, 0u);
    EXPECT_EQ(appPtr->PendingFrameStats.UvDiagnosticsTexcoordElementsScanned,
              0u);

    ASSERT_EQ(appPtr->Ready.State,
              Runtime::SelectedEntityAnalysisState::Ready);
    EXPECT_EQ(appPtr->Ready.Key.BindingGeneration, 7u);
    EXPECT_EQ(appPtr->Ready.Result.Normal.Resolver.Status,
              Runtime::AttributeBindStatus::Bound);
    EXPECT_EQ(appPtr->Ready.Result.Normal.Resolver.FallbackCount, 1u);
    EXPECT_EQ(appPtr->Ready.Result.Color.Resolver.NonFiniteCount, 1u);
    EXPECT_TRUE(appPtr->Ready.Result.Uv.TexcoordsFinite);
    EXPECT_EQ(appPtr->Ready.Result.Uv.ElementsScanned, 4096u);
    EXPECT_EQ(appPtr->CacheHit.Key.RequestIdentity,
              appPtr->Ready.Key.RequestIdentity);
    EXPECT_EQ(appPtr->FinalStats.SnapshotsCaptured, 4u);
    EXPECT_EQ(appPtr->FinalStats.JobsSubmitted, 4u);
    EXPECT_EQ(appPtr->FinalStats.ResultsApplied, 4u);
    EXPECT_EQ(appPtr->FinalStats.ReadyCacheHits, 1u);
    EXPECT_LE(appPtr->FinalStats.MaxUiBuildApplied, 1u);

    engine.Shutdown();
}

TEST(SelectedEntityAnalysisModule,
     GeometryRevisionCancelsPendingWorkAndRejectsItsIdentity)
{
    auto app = std::make_unique<AnalysisRevisionApp>();
    AnalysisRevisionApp* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(1u), std::move(app));
    engine.EmplaceModule<Runtime::SelectedEntityAnalysisModule>(1u);
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_EQ(appPtr->Stale.State,
              Runtime::SelectedEntityAnalysisState::Stale);
    EXPECT_GT(appPtr->Stale.Key.GeometryRevision,
              appPtr->Initial.Key.GeometryRevision);
    EXPECT_NE(appPtr->Replacement.Key.RequestIdentity,
              appPtr->Initial.Key.RequestIdentity);
    EXPECT_EQ(appPtr->Ready.State,
              Runtime::SelectedEntityAnalysisState::Ready);
    EXPECT_EQ(appPtr->Ready.Key.GeometryRevision,
              appPtr->Stale.Key.GeometryRevision);
    EXPECT_EQ(appPtr->Ready.Result.Normal.Resolver.FallbackCount, 0u);
    EXPECT_GE(appPtr->FinalStats.GeometryRevisionAdvances, 1u);
    EXPECT_GE(appPtr->FinalStats.JobsCancelledForRevision, 1u);
    EXPECT_EQ(appPtr->FinalStats.ResultsApplied, 1u);

    engine.Shutdown();
}
