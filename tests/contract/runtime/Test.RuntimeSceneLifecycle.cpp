#include <memory>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;

namespace Runtime = Extrinsic::Runtime;
namespace ECS = Extrinsic::ECS;
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

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }
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

    engine.SetMeshPrimitiveViewSettings(
        stableId,
        Runtime::MeshPrimitiveViewSettings{.EnableEdgeView = true});
    engine.SetVisualizationAdapterBinding(
        stableId,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0x5CE11u,
            .BufferBDA = 0xCAFE1000u,
        });
    ASSERT_TRUE(engine.GetMeshPrimitiveViewSettings(stableId).AnyEnabled());
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

    engine.Shutdown();
}
