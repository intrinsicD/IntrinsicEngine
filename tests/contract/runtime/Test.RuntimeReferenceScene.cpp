#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;

namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    namespace E = ECS::Components;
    namespace G = Graphics::Components;
    namespace GS = ECS::Components::GeometrySources;

    [[nodiscard]] std::size_t EntityCount(
        ECS::Scene::Registry& scene)
    {
        std::size_t count = 0u;
        scene.Raw().view<ECS::EntityHandle>().each(
            [&count](const ECS::EntityHandle)
            {
                ++count;
            });
        return count;
    }

    void ExpectReferenceTriangleEntityContract(
        ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity)
    {
        ASSERT_TRUE(scene.IsValid(entity));
        auto& raw = scene.Raw();

        ASSERT_TRUE(raw.all_of<E::MetaData>(entity));
        EXPECT_EQ(raw.get<E::MetaData>(entity).EntityName,
                  "ReferenceTriangle");
        EXPECT_TRUE((raw.all_of<E::Transform::Component,
                                E::Transform::WorldMatrix,
                                E::Hierarchy::Component>(entity)));

        ASSERT_TRUE(raw.all_of<E::StableId>(entity));
        EXPECT_TRUE(E::IsValid(raw.get<E::StableId>(entity)));
        EXPECT_TRUE(raw.all_of<E::Selection::SelectableTag>(entity));

        ASSERT_TRUE(raw.all_of<G::RenderSurface>(entity));
        EXPECT_EQ(raw.get<G::RenderSurface>(entity).Domain,
                  G::RenderSurface::SourceDomain::Vertex);

        ASSERT_TRUE(raw.all_of<G::VisualizationConfig>(entity));
        const auto& visualization =
            raw.get<G::VisualizationConfig>(entity);
        EXPECT_EQ(visualization.Source,
                  G::VisualizationConfig::ColorSource::UniformColor);
        EXPECT_EQ(visualization.Color, glm::vec4(1.0f));
        EXPECT_FALSE(raw.all_of<E::ProceduralGeometryRef>(entity));

        const GS::ConstSourceView view =
            GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        EXPECT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        EXPECT_EQ(view.VerticesAlive(), 3u);
        EXPECT_EQ(view.EdgesAlive(), 3u);
        EXPECT_EQ(view.HalfedgesTotal(), 6u);
        EXPECT_EQ(view.FacesAlive(), 1u);

        auto texcoords =
            view.VertexSource->Properties.Get<glm::vec2>(
                "v:texcoord");
        ASSERT_TRUE(texcoords);
        ASSERT_EQ(texcoords.Vector().size(), 3u);
        EXPECT_EQ(texcoords.Vector()[0], glm::vec2(0.0f, 0.0f));
        EXPECT_EQ(texcoords.Vector()[1], glm::vec2(1.0f, 0.0f));
        EXPECT_EQ(texcoords.Vector()[2], glm::vec2(0.5f, 1.0f));
    }

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig(
        const bool referenceEnabled)
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.Window.Backend =
            Core::Config::WindowBackend::Null;
        config.ReferenceScene.Enabled = referenceEnabled;
        config.ReferenceScene.Selector =
            Core::Config::ReferenceSceneSelector::Triangle;
        return config;
    }

    class StubApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(
            Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

    class AppOwnedReferenceApplication final
        : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            ++InitializeCalls;
            if (!engine.GetEngineConfig().ReferenceScene.Enabled ||
                Population.has_value())
            {
                return;
            }

            OwningWorld = engine.ActiveWorld();
            ECS::Scene::Registry* scene =
                engine.Worlds().Get(OwningWorld);
            ASSERT_NE(scene, nullptr);
            Population = Runtime::BootstrapReferenceScene(
                engine.GetEngineConfig().ReferenceScene.Selector,
                *scene);
            ++BootstrapCalls;
            LastSeed = Population->Camera;
        }

        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(
            Runtime::Engine&, double, double) override {}

        void OnShutdown(Runtime::Engine& engine) override
        {
            ++ShutdownCalls;
            if (!Population.has_value())
                return;

            if (ECS::Scene::Registry* scene =
                    engine.Worlds().Get(OwningWorld);
                scene != nullptr)
            {
                Runtime::TeardownReferenceScene(
                    *scene, *Population);
                TeardownRemovedPopulation =
                    EntityCount(*scene) == 0u;
            }
            Population.reset();
            OwningWorld = {};
        }

        std::optional<Runtime::ReferenceScenePopulation> Population{};
        std::optional<Graphics::CameraViewInput> LastSeed{};
        Runtime::WorldHandle OwningWorld{};
        std::uint32_t InitializeCalls{0u};
        std::uint32_t BootstrapCalls{0u};
        std::uint32_t ShutdownCalls{0u};
        bool TeardownRemovedPopulation{false};
    };
}

static_assert(noexcept(Runtime::TeardownReferenceScene(
    std::declval<ECS::Scene::Registry&>(),
    std::declval<const Runtime::ReferenceScenePopulation&>())));

TEST(ReferenceSceneBootstrap,
     TriangleCreatesVisibleSelectablePopulationAndDataOnlySeed)
{
    ECS::Scene::Registry scene;
    const Runtime::ReferenceScenePopulation population =
        Runtime::BootstrapReferenceScene(
            Core::Config::ReferenceSceneSelector::Triangle,
            scene);

    ASSERT_EQ(population.Entities.size(), 1u);
    ExpectReferenceTriangleEntityContract(
        scene, population.Entities.front().Entity);

    ASSERT_TRUE(population.Camera.has_value());
    const Graphics::CameraViewInput& seed = *population.Camera;
    EXPECT_TRUE(seed.Valid);
    EXPECT_EQ(seed.Position, glm::vec3(0.0f, 0.0f, 3.0f));
    EXPECT_EQ(seed.Forward, glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_EQ(seed.Up, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(seed.NearPlane, 0.1f);
    EXPECT_FLOAT_EQ(seed.FarPlane, 100.0f);
}

TEST(ReferenceSceneBootstrap,
     AuthoredTriangleRoundTripsFullRenderableContract)
{
    ECS::Scene::Registry source;
    const Runtime::ReferenceScenePopulation population =
        Runtime::BootstrapReferenceScene(
            Core::Config::ReferenceSceneSelector::Triangle,
            source);
    ASSERT_EQ(population.Entities.size(), 1u);

    auto document = Runtime::SerializeSceneDocument(source);
    ASSERT_TRUE(document.has_value())
        << static_cast<int>(document.error());

    ECS::Scene::Registry loaded;
    auto loadedResult =
        Runtime::DeserializeSceneDocument(loaded, *document);
    ASSERT_TRUE(loadedResult.has_value())
        << static_cast<int>(loadedResult.error());
    EXPECT_EQ(loadedResult->Stats.Entities, 1u);
    EXPECT_EQ(loadedResult->Stats.MeshEntities, 1u);
    EXPECT_EQ(loadedResult->Stats.RenderHintEntities, 1u);

    auto view = loaded.Raw().view<
        E::MetaData,
        G::RenderSurface,
        G::VisualizationConfig,
        GS::Vertices,
        GS::Edges,
        GS::Halfedges,
        GS::Faces>();
    ASSERT_EQ(view.size_hint(), 1u);
    ExpectReferenceTriangleEntityContract(
        loaded, *view.begin());
}

TEST(ReferenceSceneBootstrap, TeardownIsNoexceptAndIdempotent)
{
    ECS::Scene::Registry scene;
    const Runtime::ReferenceScenePopulation population =
        Runtime::BootstrapReferenceScene(
            Core::Config::ReferenceSceneSelector::Triangle,
            scene);
    ASSERT_EQ(EntityCount(scene), 1u);

    Runtime::TeardownReferenceScene(scene, population);
    EXPECT_EQ(EntityCount(scene), 0u);
    Runtime::TeardownReferenceScene(scene, population);
    EXPECT_EQ(EntityCount(scene), 0u);
}

TEST(ReferenceSceneOwnership,
     GenericEngineDoesNotInterpretEnabledReferenceConfig)
{
    Runtime::Engine engine(
        HeadlessConfig(true),
        std::make_unique<StubApplication>());
    engine.Initialize();

    EXPECT_EQ(EntityCount(*engine.Worlds().Get(engine.ActiveWorld())), 0u);
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::CameraControllerRegistry>(),
        nullptr);

    engine.Shutdown();
}

TEST(ReferenceSceneOwnership,
     AppBootstrapRunsOncePerInitializationWithoutCameraModule)
{
    auto app = std::make_unique<AppOwnedReferenceApplication>();
    AppOwnedReferenceApplication* appPtr = app.get();
    Runtime::Engine engine(
        HeadlessConfig(true), std::move(app));

    engine.Initialize();
    ASSERT_EQ(appPtr->InitializeCalls, 1u);
    ASSERT_EQ(appPtr->BootstrapCalls, 1u);
    ASSERT_TRUE(appPtr->Population.has_value());
    ASSERT_TRUE(appPtr->LastSeed.has_value());
    EXPECT_TRUE(appPtr->LastSeed->Valid);
    EXPECT_EQ(EntityCount(*engine.Worlds().Get(engine.ActiveWorld())), 1u);
    EXPECT_EQ(
        engine.Services()
            .Find<Runtime::CameraControllerRegistry>(),
        nullptr);

    Runtime::RenderExtractionCache extraction;
    const Runtime::RuntimeRenderExtractionStats stats =
        extraction.ExtractAndSubmit(
            *engine.Worlds().Get(engine.ActiveWorld()),
            engine.GetRenderer(),
            &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    extraction.Shutdown(engine.GetRenderer());

    engine.Shutdown();
    EXPECT_EQ(appPtr->ShutdownCalls, 1u);
    EXPECT_TRUE(appPtr->TeardownRemovedPopulation);
    EXPECT_FALSE(appPtr->Population.has_value());

    engine.Initialize();
    EXPECT_EQ(appPtr->InitializeCalls, 2u);
    EXPECT_EQ(appPtr->BootstrapCalls, 2u);
    EXPECT_EQ(EntityCount(*engine.Worlds().Get(engine.ActiveWorld())), 1u);
    engine.Shutdown();
    EXPECT_EQ(appPtr->ShutdownCalls, 2u);
}

TEST(ReferenceSceneOwnership, DisabledAppBootstrapCreatesNothing)
{
    auto app = std::make_unique<AppOwnedReferenceApplication>();
    AppOwnedReferenceApplication* appPtr = app.get();
    Runtime::Engine engine(
        HeadlessConfig(false), std::move(app));
    engine.Initialize();

    EXPECT_EQ(appPtr->InitializeCalls, 1u);
    EXPECT_EQ(appPtr->BootstrapCalls, 0u);
    EXPECT_EQ(EntityCount(*engine.Worlds().Get(engine.ActiveWorld())), 0u);

    engine.Shutdown();
    EXPECT_EQ(appPtr->ShutdownCalls, 1u);
}

TEST(ReferenceSceneOwnership,
     TeardownUsesStoredOriginalWorldAndDoesNotMutateReplacement)
{
    Runtime::WorldRegistry worlds;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    const Runtime::WorldHandle original =
        worlds.CreateWorld("Original");
    const Runtime::WorldHandle replacement =
        worlds.CreateWorld("Replacement");
    ECS::Scene::Registry* originalScene = worlds.Get(original);
    ECS::Scene::Registry* replacementScene =
        worlds.Get(replacement);
    ASSERT_NE(originalScene, nullptr);
    ASSERT_NE(replacementScene, nullptr);

    const Runtime::ReferenceScenePopulation population =
        Runtime::BootstrapReferenceScene(
            Core::Config::ReferenceSceneSelector::Triangle,
            *originalScene);
    const ECS::EntityHandle replacementSentinel =
        replacementScene->Create();

    ASSERT_TRUE(
        worlds.RequestSetActiveWorld(replacement).has_value());
    (void)worlds.ApplyMaintenance(events, jobs);

    ECS::Scene::Registry* storedOriginal = worlds.Get(original);
    ASSERT_NE(storedOriginal, nullptr);
    Runtime::TeardownReferenceScene(
        *storedOriginal, population);

    EXPECT_EQ(EntityCount(*storedOriginal), 0u);
    EXPECT_TRUE(replacementScene->IsValid(replacementSentinel));
    EXPECT_EQ(EntityCount(*replacementScene), 1u);
}

TEST(ReferenceSceneOwnership,
     RetiredOriginalWorldMakesStoredTeardownASafeNoOp)
{
    Runtime::WorldRegistry worlds;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    const Runtime::WorldHandle original =
        worlds.CreateWorld("Original");
    const Runtime::WorldHandle replacement =
        worlds.CreateWorld("Replacement");
    ECS::Scene::Registry* originalScene = worlds.Get(original);
    ECS::Scene::Registry* replacementScene =
        worlds.Get(replacement);
    ASSERT_NE(originalScene, nullptr);
    ASSERT_NE(replacementScene, nullptr);

    const Runtime::ReferenceScenePopulation population =
        Runtime::BootstrapReferenceScene(
            Core::Config::ReferenceSceneSelector::Triangle,
            *originalScene);
    const ECS::EntityHandle replacementSentinel =
        replacementScene->Create();

    ASSERT_TRUE(
        worlds.RequestSetActiveWorld(replacement).has_value());
    (void)worlds.ApplyMaintenance(events, jobs);
    ASSERT_TRUE(
        worlds.RequestDestroyWorld(original).has_value());
    (void)worlds.ApplyMaintenance(events, jobs);
    (void)worlds.ApplyMaintenance(events, jobs);
    ASSERT_EQ(worlds.Get(original), nullptr);

    if (ECS::Scene::Registry* retired = worlds.Get(original);
        retired != nullptr)
    {
        Runtime::TeardownReferenceScene(*retired, population);
    }

    EXPECT_TRUE(replacementScene->IsValid(replacementSentinel));
    EXPECT_EQ(EntityCount(*replacementScene), 1u);
}

TEST(ReferenceSceneOwnership,
     AppOwnedTriangleRemainsVisibleToSandboxEditorModels)
{
    auto app = std::make_unique<AppOwnedReferenceApplication>();
    Runtime::Engine engine(
        HeadlessConfig(true), std::move(app));
    engine.Initialize();

    auto view = engine.Worlds().Get(engine.ActiveWorld())->Raw().view<
        E::MetaData,
        G::RenderSurface,
        GS::Vertices,
        GS::Edges,
        GS::Halfedges,
        GS::Faces>();
    ASSERT_EQ(view.size_hint(), 1u);
    const ECS::EntityHandle entity = *view.begin();

    Runtime::SelectionController& selection =
        engine.GetSelectionController();
    ASSERT_TRUE(selection.SetSelectedEntity(
        *engine.Worlds().Get(engine.ActiveWorld()), entity));

    const Runtime::SandboxEditorContext context{
        .Scene = &*engine.Worlds().Get(engine.ActiveWorld()),
        .Selection = &selection,
        .ImGuiAdapterAvailable = true,
        .AssetImportCommandsAvailable = false,
        .CameraRenderCommandsAvailable = false,
        .VisualizationCommandsAvailable = true,
    };
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    ASSERT_EQ(frame.Hierarchy.size(), 1u);
    EXPECT_EQ(frame.Hierarchy[0].Name, "ReferenceTriangle");
    EXPECT_TRUE(frame.Hierarchy[0].Selectable);
    EXPECT_TRUE(frame.Hierarchy[0].Selected);
    EXPECT_TRUE(frame.Hierarchy[0].HasDurableStableId);
    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_EQ(frame.Inspector.Entity.Name,
              "ReferenceTriangle");
    EXPECT_EQ(frame.Inspector.Geometry.Domain,
              GS::Domain::Mesh);
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderSurface);
    EXPECT_EQ(frame.Inspector.Geometry.VertexCount, 3u);
    EXPECT_EQ(frame.Inspector.Geometry.FaceCount, 1u);

    engine.Shutdown();
}
