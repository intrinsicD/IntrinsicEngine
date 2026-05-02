#include <array>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;

TEST(RenderWorldContract, ExposesRendererOwnedImmutableRuntimeSnapshots)
{
    Tests::MockDevice device;
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const auto instance = renderer->GetGpuWorld().AllocateInstance(77u);
    ASSERT_TRUE(instance.IsValid());

    RHI::GpuBounds bounds{};
    bounds.WorldSphere = {1.f, 2.f, 3.f, 4.f};

    const std::array<Graphics::TransformSyncRecord, 2> transforms{{
        Graphics::TransformSyncRecord{
            .StableId = 77u,
            .Instance = instance,
            .Model = glm::mat4{1.f},
            .RenderFlags = RHI::GpuRender_Visible | RHI::GpuRender_Surface | RHI::GpuRender_Opaque,
            .Bounds = bounds,
            .MaterialSlot = 9u,
            .HasMaterialSlot = true,
        },
        Graphics::TransformSyncRecord{
            .StableId = 999u,
            .Instance = {},
            .Model = glm::mat4{1.f},
        },
    }};

    const std::array<Graphics::LightSnapshot, 1> lights{{
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Point,
            .Position = {1.f, 2.f, 3.f},
            .Range = 8.f,
            .Direction = {0.f, -1.f, 0.f},
            .Intensity = 2.f,
            .Color = {0.5f, 0.25f, 1.f},
        },
    }};

    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .Transforms = transforms,
        .Lights = lights,
        .Visualizations = {},
    });

    const Graphics::RenderWorld world = renderer->ExtractRenderWorld(Graphics::RenderFrameInput{
        .Alpha = 0.25,
        .Viewport = {1280u, 720u},
        .HasPendingPick = true,
        .DebugOverlayEnabled = true,
    });

    ASSERT_EQ(world.Renderables.size(), 1u);
    EXPECT_EQ(world.Renderables[0].StableId, 77u);
    EXPECT_EQ(world.Renderables[0].Instance.Index, instance.Index);
    EXPECT_EQ(world.Renderables[0].RenderFlags,
              RHI::GpuRender_Visible | RHI::GpuRender_Surface | RHI::GpuRender_Opaque);
    EXPECT_EQ(world.Renderables[0].MaterialSlot, 9u);
    EXPECT_TRUE(world.Renderables[0].HasMaterialSlot);
    EXPECT_EQ(world.Renderables[0].Bounds.WorldSphere.w, 4.f);

    ASSERT_EQ(world.Lights.size(), 1u);
    EXPECT_EQ(world.Lights[0].LightType, Graphics::LightSnapshot::Type::Point);
    EXPECT_EQ(world.Lights[0].Range, 8.f);
    EXPECT_EQ(world.InvalidSnapshotRecordCount, 1u);
    EXPECT_TRUE(world.HasPendingPick);
    EXPECT_TRUE(world.DebugOverlayEnabled);
    EXPECT_TRUE(world.PickRequest.Pending);
    EXPECT_FALSE(world.Selection.HasHovered);
    EXPECT_TRUE(world.Selection.SelectedStableIds.empty());
    EXPECT_FALSE(world.Shadows.Enabled);
    EXPECT_TRUE(world.DebugPrimitives.HasTransientDebug);
    EXPECT_TRUE(world.PostProcess.Enabled);
    EXPECT_FALSE(world.PostProcess.RequiresReadback);
    EXPECT_EQ(world.Viewport.Width, 1280u);
    EXPECT_EQ(world.Viewport.Height, 720u);

    renderer->Shutdown();
}

TEST(RenderWorldContract, BeginFrameClearsPreviousRuntimeSnapshots)
{
    Tests::MockDevice device;
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const auto instance = renderer->GetGpuWorld().AllocateInstance(7u);
    ASSERT_TRUE(instance.IsValid());

    const std::array<Graphics::TransformSyncRecord, 1> transforms{{
        Graphics::TransformSyncRecord{
            .StableId = 7u,
            .Instance = instance,
        },
    }};
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = transforms});
    EXPECT_EQ(renderer->ExtractRenderWorld({}).Renderables.size(), 1u);

    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Graphics::RenderWorld emptyWorld = renderer->ExtractRenderWorld({});
    EXPECT_TRUE(emptyWorld.Renderables.empty());
    EXPECT_TRUE(emptyWorld.Lights.empty());
    EXPECT_FALSE(emptyWorld.PickRequest.Pending);
    EXPECT_FALSE(emptyWorld.Selection.HasHovered);
    EXPECT_FALSE(emptyWorld.Shadows.Enabled);
    EXPECT_FALSE(emptyWorld.DebugPrimitives.HasTransientDebug);
    EXPECT_FALSE(emptyWorld.PostProcess.Enabled);
    EXPECT_EQ(emptyWorld.InvalidSnapshotRecordCount, 0u);

    renderer->Shutdown();
}



