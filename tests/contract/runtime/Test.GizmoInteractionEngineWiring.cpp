// RUNTIME-084 Slice B — runtime composition coverage for transform-gizmo
// packet submission. SceneInteractionModule owns selection/input/gizmo state
// and graphics only receives copied TransformGizmoRenderPacket values through
// runtime snapshots.

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

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

    class SelectGizmoEntityApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Entity       = MakeTransformEntity(engine, glm::vec3{2.f, 3.f, 4.f});
        }


        void Frame(double /*alpha*/, double /*dt*/) override
        {
            auto& engine = Kernel();
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

        void Shutdown() override {}

        EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
        bool         SelectionApplied{false};
        std::uint32_t VariableTicks{0u};
    };
}

TEST(GizmoInteractionEngineWiring, ExtractionSubmitsTransformGizmoPackets)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<SelectGizmoEntityApplication>());
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneInteractionModule>();
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneDocumentModule>();
    engine.EmplaceModule<
        Extrinsic::Runtime::AssetWorkflowModule>();
    engine.Initialize();

    const EntityHandle entity = MakeTransformEntity(engine, glm::vec3{2.f, 3.f, 4.f});
    auto& selection =
        *engine.Services().Find<
            Extrinsic::Runtime::SelectionController>();
    ASSERT_TRUE(selection.SetSelectedEntity(
        *engine.Worlds().Get(engine.ActiveWorld()), entity));

    std::vector<EntityHandle> selected{entity};
    Extrinsic::Runtime::TransformGizmoRenderPacketBuilder builder{};
    const Extrinsic::Runtime::GizmoInteraction gizmo{
        Extrinsic::Runtime::GizmoConfig{.AxisLength = 1.25f}};
    const auto packets = builder.Build(*engine.Worlds().Get(engine.ActiveWorld()),
                                       selected,
                                       gizmo);
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
                                      &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine),
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

// UI-078 slice 1: a multi-selection publishes one group gizmo on the shared
// world-pivot frame; the hit test resolves on that frame and an accepted
// preview moves the published gizmo with it.
TEST(GizmoInteractionEngineWiring, GroupSelectionPublishesOneSharedFrameGizmo)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<SelectGizmoEntityApplication>());
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneInteractionModule>();
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneDocumentModule>();
    engine.EmplaceModule<
        Extrinsic::Runtime::AssetWorkflowModule>();
    engine.Initialize();
    auto& registry = *engine.Worlds().Get(engine.ActiveWorld());

    // Parent at x=-3, child local +1 (world -2) and a root at +2: world pivot 0.
    const EntityHandle parent = MakeTransformEntity(engine, glm::vec3{-3.f, 0.f, 0.f});
    const EntityHandle child = MakeTransformEntity(engine, glm::vec3{1.f, 0.f, 0.f});
    registry.Raw().emplace<Extrinsic::ECS::Components::Hierarchy::Component>(
        child, Extrinsic::ECS::Components::Hierarchy::Component{.Parent = parent});
    const EntityHandle root = MakeTransformEntity(engine, glm::vec3{2.f, 0.f, 0.f});
    const std::vector<EntityHandle> selected{root, child};

    Extrinsic::Runtime::GizmoInteraction gizmo{};
    Extrinsic::Runtime::TransformGizmoRenderPacketBuilder builder{};
    auto packets = builder.Build(registry, selected, gizmo);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_NEAR(packets[0].Transform[3].x, 0.f, 1.0e-4f);

    // Ortho camera: world x in [-4,4] over 800 px, so pivot -> (400,300) and
    // the +X handle -> (500,300).
    Extrinsic::Graphics::CameraViewInput cameraInput{};
    cameraInput.View = glm::lookAt(glm::vec3{0.f, 0.f, 5.f}, glm::vec3{0.f}, glm::vec3{0.f, 1.f, 0.f});
    cameraInput.Projection = glm::ortho(-4.f, 4.f, -3.f, 3.f, 0.1f, 100.f);
    cameraInput.Position = {0.f, 0.f, 5.f};
    cameraInput.Forward = {0.f, 0.f, -1.f};
    cameraInput.Up = {0.f, 1.f, 0.f};
    cameraInput.NearPlane = 0.1f;
    cameraInput.FarPlane = 100.f;
    cameraInput.Valid = true;
    const Extrinsic::Core::Extent2D viewport{.Width = 800, .Height = 600};
    const auto hit = gizmo.HitTest(registry,
                                   Extrinsic::Graphics::BuildCameraViewSnapshot(cameraInput, viewport),
                                   glm::vec2{450.f, 300.f}, viewport, selected);
    ASSERT_TRUE(hit.Hit);
    EXPECT_EQ(hit.Axis, Extrinsic::Runtime::GizmoAxis::X);
    EXPECT_EQ(Extrinsic::Runtime::StableEntityLookup::ToRenderId(hit.Entity), packets[0].StableId);

    ASSERT_TRUE(gizmo.Begin(registry, selected, Extrinsic::Runtime::GizmoMode::Translate,
                            Extrinsic::Runtime::GizmoOrientation::Global,
                            Extrinsic::Runtime::GizmoPivotMode::WorldOrigins).Succeeded());
    ASSERT_TRUE(gizmo.Preview(registry, glm::translate(glm::mat4{1.f}, glm::vec3{0.f, 1.f, 0.f}) *
                                            gizmo.SessionFrame().Matrix).Succeeded());
    packets = builder.Build(registry, selected, gizmo);
    ASSERT_EQ(packets.size(), 1u);

    Extrinsic::Runtime::RenderExtractionCache extraction{};
    extraction.SubmitSceneInteractionSnapshot(
        Extrinsic::Runtime::RuntimeSceneInteractionRenderSnapshot{
            .World = engine.ActiveWorld(),
            .GizmoDrawPackets =
                std::vector<Extrinsic::Graphics::TransformGizmoRenderPacket>(packets.begin(), packets.end()),
        });
    (void)extraction.ExtractAndSubmit(registry,
                                      engine.GetRenderer(),
                                      &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine),
                                      0u,
                                      engine.ActiveWorld());
    Extrinsic::Graphics::RenderFrameInput input{};
    input.Viewport = engine.GetWindow().GetFramebufferExtent();
    const Extrinsic::Graphics::RenderWorld world = engine.GetRenderer().ExtractRenderWorld(input, 0u);
    ASSERT_EQ(world.Gizmos.TransformGizmoCount, 1u);
    EXPECT_EQ(world.Gizmos.TransformGizmos[0].Transform, gizmo.AcceptedGizmoMatrix());
    EXPECT_NEAR(world.Gizmos.TransformGizmos[0].Transform[3].y, 1.f, 1.0e-4f);

    gizmo.DragCancel(registry);
    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GizmoInteractionEngineWiring, RunFramePublishesSelectedEntityGizmoPacket)
{
    auto app = std::make_unique<SelectGizmoEntityApplication>();
    SelectGizmoEntityApplication* appRaw = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(app));
    engine.EmplaceModule<
        Extrinsic::Runtime::SceneInteractionModule>();
    engine.Initialize();

    if (engine.GetWindow().ShouldClose())
    {
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame gizmo wiring "
                        "requires a display";
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
