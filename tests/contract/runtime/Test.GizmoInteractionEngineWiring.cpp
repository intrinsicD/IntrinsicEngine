// RUNTIME-084 Slice B — runtime composition coverage for transform-gizmo
// packet submission. SceneInteractionModule owns selection/input/gizmo state
// and graphics only receives copied TransformGizmoRenderPacket values through
// runtime snapshots.

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;

namespace
{
    namespace Tf = Extrinsic::ECS::Components::Transform;

    using Extrinsic::ECS::EntityHandle;
    using Extrinsic::Runtime::Engine;

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }

    EntityHandle MakeTransformEntity(Engine& engine, const glm::vec3 position)
    {
        EntityHandle entity = engine.Worlds().Get(engine.ActiveWorld())->Create();
        engine.Worlds().Get(engine.ActiveWorld())->Raw().emplace<Tf::Component>(entity, Tf::Component{
            .Position = position,
            .Scale = glm::vec3{1.f},
        });
        return entity;
    }

    class SelectGizmoEntityApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Engine& engine) override
        {
            Entity = MakeTransformEntity(engine, glm::vec3{2.f, 3.f, 4.f});
        }

        void OnSimTick(Engine& /*engine*/, double /*fixedDt*/) override {}

        void OnVariableTick(Engine& engine, double /*alpha*/, double /*dt*/) override
        {
            ++VariableTicks;
            auto& selection =
                *engine.Services().Find<
                    Extrinsic::Runtime::SelectionController>();
            auto& interaction =
                *engine.Services().Find<
                    Extrinsic::Runtime::SceneInteractionModule>();
            SelectionApplied =
                selection.SetSelectedEntity(
                    *engine.Worlds().Get(engine.ActiveWorld()),
                    Entity);
            interaction.Interaction().SetMode(
                Extrinsic::Runtime::GizmoMode::Translate);
            engine.RequestExit();
        }

        void OnShutdown(Engine& /*engine*/) override {}

        EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
        bool         SelectionApplied{false};
        std::uint32_t VariableTicks{0u};
    };
}

TEST(GizmoInteractionEngineWiring, ExtractionSubmitsTransformGizmoPackets)
{
    Engine engine(HeadlessConfig(), std::make_unique<SelectGizmoEntityApplication>());
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneInteractionModule>();
    engine.Initialize();

    const EntityHandle entity = MakeTransformEntity(engine, glm::vec3{2.f, 3.f, 4.f});
    auto& selection =
        *engine.Services().Find<
            Extrinsic::Runtime::SelectionController>();
    ASSERT_TRUE(selection.SetSelectedEntity(
        *engine.Worlds().Get(engine.ActiveWorld()), entity));

    std::vector<EntityHandle> selected{entity};
    Extrinsic::Runtime::TransformGizmoRenderPacketBuilder builder{};
    const auto packets = builder.Build(*engine.Worlds().Get(engine.ActiveWorld()),
                                       selected,
                                       Extrinsic::Runtime::GizmoMode::Translate,
                                       Extrinsic::Runtime::GizmoOrientation::Global,
                                       1.25f);
    ASSERT_EQ(packets.size(), 1u);

    Extrinsic::Runtime::RenderExtractionCache extraction{};
    extraction.SubmitSceneInteractionSnapshot(
        Extrinsic::Runtime::
            RuntimeSceneInteractionRenderSnapshot{
                .World = engine.ActiveWorld(),
                .SelectedRenderIds =
                    std::vector<std::uint32_t>(
                        selection.SelectedStableIds().begin(),
                        selection.SelectedStableIds().end()),
                .GizmoDrawPackets =
                    std::vector<
                        Extrinsic::Graphics::
                            TransformGizmoRenderPacket>(
                        packets.begin(), packets.end()),
            });
    (void)extraction.ExtractAndSubmit(*engine.Worlds().Get(engine.ActiveWorld()),
                                      engine.GetRenderer(),
                                      &engine.GetGpuAssetCache(),
                                      0u,
                                      engine.ActiveWorld());

    Extrinsic::Graphics::RenderFrameInput input{};
    input.Viewport = engine.GetWindow().GetFramebufferExtent();
    const Extrinsic::Graphics::RenderWorld world =
        engine.GetRenderer().ExtractRenderWorld(input, 0u);

    EXPECT_TRUE(world.Gizmos.HasGizmos);
    ASSERT_EQ(world.Gizmos.TransformGizmoCount, 1u);
    EXPECT_EQ(world.Gizmos.TransformGizmos[0].StableId,
              Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    EXPECT_NEAR(world.Gizmos.TransformGizmos[0].AxisLength, 1.25f, 1.0e-4f);
    EXPECT_NEAR(world.Gizmos.TransformGizmos[0].Transform[3].x, 2.f, 1.0e-4f);
    EXPECT_NEAR(world.Gizmos.TransformGizmos[0].Transform[3].y, 3.f, 1.0e-4f);
    EXPECT_NEAR(world.Gizmos.TransformGizmos[0].Transform[3].z, 4.f, 1.0e-4f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GizmoInteractionEngineWiring, RunFramePublishesSelectedEntityGizmoPacket)
{
    auto app = std::make_unique<SelectGizmoEntityApplication>();
    SelectGizmoEntityApplication* appRaw = app.get();
    Engine engine(HeadlessConfig(), std::move(app));
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneInteractionModule>();
    engine.Initialize();

    if (engine.GetWindow().ShouldClose())
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame gizmo wiring requires a display";
    }

    engine.Run();
    ASSERT_EQ(appRaw->VariableTicks, 1u);
    ASSERT_NE(appRaw->Entity, Extrinsic::ECS::InvalidEntityHandle);
    ASSERT_TRUE(appRaw->SelectionApplied);

    Extrinsic::Graphics::RenderFrameInput input{};
    input.Viewport = engine.GetWindow().GetFramebufferExtent();
    const Extrinsic::Graphics::RenderWorld world =
        engine.GetRenderer().ExtractRenderWorld(input, 0u);

    EXPECT_TRUE(world.Gizmos.HasGizmos);
    ASSERT_EQ(world.Gizmos.TransformGizmoCount, 1u);
    EXPECT_EQ(world.Gizmos.TransformGizmos[0].StableId,
              Extrinsic::Runtime::StableEntityLookup::ToRenderId(appRaw->Entity));

    engine.Shutdown();
}
