#include <cstdint>
#include <atomic>
#include <memory>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;

namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;
namespace ECS = Extrinsic::ECS;
namespace G = Extrinsic::Graphics::Components;
namespace Sel = Extrinsic::ECS::Components::Selection;

namespace
{
    class StubApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

    class DerivedJobFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Runtime::DerivedJobDesc desc{
                .Key = Runtime::DerivedJobKey{
                    .EntityId = 77u,
                    .Domain = Runtime::ProgressiveGeometryDomain::Point,
                    .OutputSemantic = Runtime::ProgressiveSlotSemantic::PointColor,
                    .OutputName = "engine_frame_probe",
                },
                .Name = "Engine.Frame.DerivedJob",
                .Kind = Core::Dag::TaskKind::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Execute =
                    [this]() -> Runtime::DerivedJobWorkerResult
                    {
                        WorkerRuns.fetch_add(1u, std::memory_order_relaxed);
                        return Runtime::DerivedJobOutput{
                            .PayloadToken = 42u,
                            .NormalizedProgress = 1.0f,
                            .ProgressDeterminate = true,
                            .Diagnostic = "ready",
                        };
                    },
                .ApplyOnMainThread =
                    [this](Runtime::DerivedJobApplyContext& context)
                        -> Core::Result
                    {
                        EXPECT_EQ(context.Output.PayloadToken, 42u);
                        ApplyRuns.fetch_add(1u, std::memory_order_relaxed);
                        return Core::Ok();
                    },
            };
            Handle = engine.SubmitDerivedJob(std::move(desc));
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++Frames;
            if (ApplyRuns.load(std::memory_order_relaxed) > 0u || Frames >= 8u)
                engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::DerivedJobHandle Handle{};
        std::atomic<std::uint32_t> WorkerRuns{0u};
        std::atomic<std::uint32_t> ApplyRuns{0u};
        std::uint32_t Frames{0u};
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig NullWindowHeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config = HeadlessConfig();
        config.Window.Backend = Extrinsic::Core::Config::WindowBackend::Null;
        return config;
    }
}

TEST(RuntimeDerivedJobEngineWiring, RunFrameAppliesSubmittedDerivedJob)
{
    auto application = std::make_unique<DerivedJobFrameApplication>();
    DerivedJobFrameApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.Initialize();

    EXPECT_TRUE(app->Handle.IsValid());
    EXPECT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable";

    engine.Run();

    const Runtime::DerivedJobQueueSnapshot snapshot =
        engine.GetDerivedJobQueueSnapshot();
    ASSERT_EQ(snapshot.Entries.size(), 1u);
    EXPECT_EQ(snapshot.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    EXPECT_EQ(app->WorkerRuns.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(app->ApplyRuns.load(std::memory_order_relaxed), 1u);
    EXPECT_GT(snapshot.Diagnostics.ApplyMainThreadCalls, 0u);

    engine.Shutdown();
}

TEST(RuntimeRenderExtraction, VisualizationAdapterBindingRevisionTracksMutations)
{
    Runtime::RenderExtractionCache cache{};

    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 0u);

    cache.ClearVisualizationAdapterBinding(7u);
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 0u);

    cache.SetVisualizationAdapterBinding(
        7u,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0xA11CEu,
            .BufferBDA = 0xCAFE1000u,
        });
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 1u);

    cache.SetVisualizationAdapterBinding(
        7u,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0xB0Bu,
            .BufferBDA = 0xCAFE2000u,
        });
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 2u);

    cache.ClearVisualizationAdapterBinding(9u);
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 2u);

    cache.ClearVisualizationAdapterBinding(7u);
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 3u);
}

TEST(RuntimeSceneLifecycle, NewSceneDocumentClearsSceneSelectionAndExtractionSidecars)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const ECS::EntityHandle entity = scene.Create();
    scene.Raw().emplace<Sel::SelectableTag>(entity);
    const std::uint32_t stableId = Runtime::SelectionController::ToStableEntityId(entity);

    engine.GetSelectionController().SetSelectedEntity(scene, entity);
    engine.GetSelectionController().RequestHoverPick(4u, 5u);
    ASSERT_TRUE(engine.GetSelectionController().ConsumePendingPick().has_value());
    engine.GetSelectionController().ConsumeHit(scene, stableId);
    engine.GetSelectionController().RequestClickPick(6u, 7u);
    ASSERT_TRUE(engine.GetSelectionController().ConsumePendingPick().has_value());
    ASSERT_EQ(engine.GetSelectionController().InFlightPickCount(), 1u);

    Runtime::MeshPrimitiveViewSettings primitiveSettings{};
    primitiveSettings.EnableEdgeView = true;
    primitiveSettings.EnableVertexView = true;
    primitiveSettings.VertexRenderMode =
        Runtime::MeshVertexViewRenderMode::SurfaceAlignedCircle;
    primitiveSettings.VertexPointRadiusPx = 4.0f;
    engine.SetMeshPrimitiveViewSettings(stableId, primitiveSettings);
    engine.SetVisualizationAdapterBinding(
        stableId,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0x5CE11u,
            .BufferBDA = 0xCAFE1000u,
        });
    const std::uint64_t bindingRevisionBeforeReset =
        engine.GetVisualizationAdapterBindingRevision();
    EXPECT_EQ(bindingRevisionBeforeReset, 1u);
    ASSERT_TRUE(engine.GetMeshPrimitiveViewSettings(stableId).AnyEnabled());
    ASSERT_TRUE(scene.Raw().all_of<G::RenderEdges>(entity));
    ASSERT_TRUE(scene.Raw().all_of<G::RenderPoints>(entity));
    const G::RenderPoints& translatedPoints =
        scene.Raw().get<G::RenderPoints>(entity);
    EXPECT_EQ(translatedPoints.Type, G::RenderPoints::RenderType::Surfel);
    ASSERT_TRUE(std::holds_alternative<float>(translatedPoints.SizeSource));
    EXPECT_FLOAT_EQ(std::get<float>(translatedPoints.SizeSource), 4.0f);
    ASSERT_TRUE(engine.GetVisualizationAdapterBinding(stableId).has_value());

    const auto reset = engine.NewSceneDocument();
    ASSERT_TRUE(reset.has_value()) << static_cast<int>(reset.error());

    EXPECT_FALSE(scene.Raw().valid(entity));
    EXPECT_EQ(engine.GetSelectionController().SelectedCount(), 0u);
    EXPECT_FALSE(engine.GetSelectionController().HasHovered());
    EXPECT_FALSE(engine.GetSelectionController().HasPendingPick());
    EXPECT_EQ(engine.GetSelectionController().InFlightPickCount(), 0u);
    EXPECT_FALSE(engine.GetMeshPrimitiveViewSettings(stableId).AnyEnabled());
    EXPECT_FALSE(engine.GetVisualizationAdapterBinding(stableId).has_value());
    EXPECT_GT(engine.GetVisualizationAdapterBindingRevision(),
              bindingRevisionBeforeReset);

    engine.Shutdown();
}
