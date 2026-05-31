// RUNTIME-089 Slice B — integration coverage that the runtime-owned
// SelectionController snapshot mirrors into RenderWorld::Selection through
// RenderExtractionCache::ExtractAndSubmit + IRenderer::ExtractRenderWorld,
// without graphics ever reading live ECS. Mirrors the headless-Engine /
// standalone-cache harness used by the other *Extraction tests.

#include <cstdint>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::SelectionController;

namespace Sel = Extrinsic::ECS::Components::Selection;

namespace
{
    class StubApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Extrinsic::Runtime::Engine& /*engine*/) override {}
        void OnSimTick(Extrinsic::Runtime::Engine& /*engine*/, double /*fixedDt*/) override {}
        void OnVariableTick(Extrinsic::Runtime::Engine& /*engine*/,
                            double /*alpha*/,
                            double /*dt*/) override {}
        void OnShutdown(Extrinsic::Runtime::Engine& /*engine*/) override {}
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        return config;
    }

    [[nodiscard]] EntityHandle MakeSelectable(Registry& scene)
    {
        const EntityHandle entity = scene.Create();
        scene.Raw().emplace<Sel::SelectableTag>(entity);
        return entity;
    }

    [[nodiscard]] std::uint32_t StableId(EntityHandle entity)
    {
        return SelectionController::ToStableEntityId(entity);
    }

    // Run one extraction + render-world snapshot against the headless renderer
    // and return the produced RenderWorld so tests can inspect `.Selection`.
    [[nodiscard]] Extrinsic::Graphics::RenderWorld ExtractWorld(
        Extrinsic::Runtime::Engine&             engine,
        Extrinsic::Runtime::RenderExtractionCache& extraction,
        Registry&                               scene,
        const SelectionController*              selection)
    {
        (void)extraction.ExtractAndSubmit(scene,
                                          engine.GetRenderer(),
                                          &engine.GetGpuAssetCache(),
                                          selection);
        return engine.GetRenderer().ExtractRenderWorld(Extrinsic::Graphics::RenderFrameInput{});
    }
}

// A click-selected entity appears in RenderWorld::Selection::SelectedStableIds
// with no hover, and the styling defaults are preserved.
TEST(SelectionSnapshotExtraction, SelectedEntityMirrorsIntoRenderWorldSelection)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeSelectable(scene);

    SelectionController controller;
    controller.RequestClickPick(4u, 7u);
    ASSERT_TRUE(controller.ConsumePendingPick().has_value());
    controller.ConsumeHit(scene, StableId(entity));
    ASSERT_TRUE(controller.IsSelected(entity));

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const Extrinsic::Graphics::RenderWorld world = ExtractWorld(engine, extraction, scene, &controller);

    ASSERT_EQ(world.Selection.SelectedStableIds.size(), 1u);
    EXPECT_EQ(world.Selection.SelectedStableIds[0], StableId(entity));
    EXPECT_FALSE(world.Selection.HasHovered);
    // Outline styling keeps SelectionSnapshot recipe defaults.
    EXPECT_EQ(world.Selection.OutlineMode, 0u);
    EXPECT_FLOAT_EQ(world.Selection.OutlineWidth, 2.0f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// A hovered entity populates HoveredStableId/HasHovered without entering the
// selected set.
TEST(SelectionSnapshotExtraction, HoveredEntityMirrorsIntoRenderWorldSelection)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeSelectable(scene);

    SelectionController controller;
    controller.RequestHoverPick(11u, 13u);
    ASSERT_TRUE(controller.ConsumePendingPick().has_value());
    controller.ConsumeHit(scene, StableId(entity));
    ASSERT_TRUE(controller.HasHovered());
    ASSERT_EQ(controller.SelectedCount(), 0u);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const Extrinsic::Graphics::RenderWorld world = ExtractWorld(engine, extraction, scene, &controller);

    EXPECT_TRUE(world.Selection.HasHovered);
    EXPECT_EQ(world.Selection.HoveredStableId, StableId(entity));
    EXPECT_TRUE(world.Selection.SelectedStableIds.empty());

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// Multiple additive selections mirror in insertion order.
TEST(SelectionSnapshotExtraction, AdditiveSelectionMirrorsAllSelectedIds)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();
    const EntityHandle first  = MakeSelectable(scene);
    const EntityHandle second = MakeSelectable(scene);

    SelectionController controller;
    controller.RequestClickPick(1u, 1u);
    ASSERT_TRUE(controller.ConsumePendingPick().has_value());
    controller.ConsumeHit(scene, StableId(first));
    controller.RequestClickPick(2u, 2u, Extrinsic::Runtime::SelectionPickMode::Add);
    ASSERT_TRUE(controller.ConsumePendingPick().has_value());
    controller.ConsumeHit(scene, StableId(second));
    ASSERT_EQ(controller.SelectedCount(), 2u);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const Extrinsic::Graphics::RenderWorld world = ExtractWorld(engine, extraction, scene, &controller);

    ASSERT_EQ(world.Selection.SelectedStableIds.size(), 2u);
    EXPECT_EQ(world.Selection.SelectedStableIds[0], StableId(first));
    EXPECT_EQ(world.Selection.SelectedStableIds[1], StableId(second));

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// Without a wired controller the selection snapshot is empty, so existing
// extraction callers are unaffected.
TEST(SelectionSnapshotExtraction, NullControllerLeavesSelectionEmpty)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const Extrinsic::Graphics::RenderWorld world = ExtractWorld(engine, extraction, scene, nullptr);

    EXPECT_TRUE(world.Selection.SelectedStableIds.empty());
    EXPECT_FALSE(world.Selection.HasHovered);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// Clearing the selection in a later extraction drops the previously-mirrored
// ids — the snapshot tracks live controller state, not a sticky set.
TEST(SelectionSnapshotExtraction, ClearedSelectionMirrorsEmptyOnNextExtraction)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeSelectable(scene);

    SelectionController controller;
    controller.RequestClickPick(3u, 3u);
    ASSERT_TRUE(controller.ConsumePendingPick().has_value());
    controller.ConsumeHit(scene, StableId(entity));

    Extrinsic::Runtime::RenderExtractionCache extraction;
    Extrinsic::Graphics::RenderWorld world = ExtractWorld(engine, extraction, scene, &controller);
    ASSERT_EQ(world.Selection.SelectedStableIds.size(), 1u);

    controller.ClearSelection(scene);
    world = ExtractWorld(engine, extraction, scene, &controller);
    EXPECT_TRUE(world.Selection.SelectedStableIds.empty());

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
