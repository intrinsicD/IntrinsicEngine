// RUNTIME-084 Slice B / UI-078 — runtime composition coverage for transform-gizmo
// packet submission. Graphics only receives copied TransformGizmoRenderPacket
// values through runtime snapshots; since the editor gizmo is drawn by the
// Sandbox's ImGuizmo frontend, SceneInteractionModule publishes none itself.

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
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
            Dragging = interaction.Interaction().IsDragging();
            engine.RequestExit();
        }

        void Shutdown() override {}

        EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
        bool         SelectionApplied{false};
        bool         Dragging{true};
        std::uint32_t VariableTicks{0u};
    };
}

TEST(GizmoInteractionEngineWiring, ExtractionForwardsExplicitTransformGizmoPackets)
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

    const std::vector<Extrinsic::Graphics::TransformGizmoRenderPacket> packets{{
        .StableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity),
        .Transform = glm::translate(glm::mat4{1.f}, glm::vec3{2.f, 3.f, 4.f}),
        .AxisLength = 1.25f,
    }};

    Extrinsic::Runtime::RenderExtractionCache extraction{};
    extraction.SubmitSceneInteractionSnapshot(
        Extrinsic::Runtime::
            RuntimeSceneInteractionRenderSnapshot{
                .World = engine.ActiveWorld(),
                .SelectedRenderIds =
                    std::vector<std::uint32_t>(
                        selection.SelectedStableIds().begin(),
                        selection.SelectedStableIds().end()),
                .GizmoDrawPackets = packets,
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

// UI-078: selecting an entity neither starts a session nor publishes a
// gizmo packet; only the editor frontend draws (ImGuizmo) and drags.
TEST(GizmoInteractionEngineWiring, SelectionAloneStartsNoSessionAndPublishesNoGizmoPacket)
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
    EXPECT_FALSE(appRaw->Dragging);

    Extrinsic::Graphics::RenderFrameInput input{};
    input.Viewport = engine.GetWindow().GetFramebufferExtent();
    const Extrinsic::Graphics::RenderWorld world =
        engine.GetRenderer().ExtractRenderWorld(input, 0u);

    EXPECT_EQ(world.Gizmos.TransformGizmoCount, 0u);

    engine.Shutdown();
}
