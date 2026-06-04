#include <array>
#include <cstdint>
#include <limits>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.VisualizationPackets;
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

    const std::array<Graphics::DebugLinePacket, 1> debugLines{{
        Graphics::DebugLinePacket{
            .Start = {0.f, 0.f, 0.f},
            .End = {1.f, 0.f, 0.f},
            .Color = {1.f, 0.f, 0.f, 1.f},
            .Width = 100.f,
        },
    }};
    const std::array<Graphics::DebugPointPacket, 2> debugPoints{{
        Graphics::DebugPointPacket{
            .Position = {0.f, 1.f, 0.f},
            .Color = {0.f, 1.f, 0.f, 1.f},
            .Radius = 0.00001f,
        },
        Graphics::DebugPointPacket{
            .Position = {std::numeric_limits<float>::infinity(), 0.f, 0.f},
            .Color = {1.f, 1.f, 1.f, 1.f},
            .Radius = 0.1f,
        },
    }};
    const std::array<Graphics::DebugTrianglePacket, 1> debugTriangles{{
        Graphics::DebugTrianglePacket{
            .A = {0.f, 0.f, 0.f},
            .B = {0.f, 1.f, 0.f},
            .C = {1.f, 0.f, 0.f},
            .Color = {0.f, 0.f, 1.f, 1.f},
        },
    }};
    const std::array<Graphics::TransformGizmoRenderPacket, 2> transformGizmos{{
        Graphics::TransformGizmoRenderPacket{
            .StableId = 77u,
            .Transform = glm::mat4{1.f},
            .AxisLength = 2.f,
            .ShowTranslate = true,
        },
        Graphics::TransformGizmoRenderPacket{
            .StableId = 88u,
            .Transform = glm::mat4{1.f},
            .AxisLength = -1.f,
        },
    }};
    const std::array<Graphics::ScalarAttributePacket, 1> visualizationScalars{{
        Graphics::ScalarAttributePacket{
            .Name = "temperature",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 12u,
            .RangeMin = 0.f,
            .RangeMax = 1.f,
            .ScalarBufferBDA = 0x1000u,
        },
    }};
    const std::array<Graphics::FragmentBakeAtlasPacket, 2> visualizationBakes{{
        Graphics::FragmentBakeAtlasPacket{
            .Name = "labels_uv_bake",
            .SourceAttributeName = "kmeans_label_rgba",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 128u,
            .AtlasHeight = 128u,
            .TexcoordBufferBDA = 0x2000u,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "labels_htex_bake",
            .SourceAttributeName = "kmeans_label_rgba",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::RecreateHtex,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 128u,
            .AtlasHeight = 128u,
        },
    }};

    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .Transforms = transforms,
        .Lights = lights,
        .Visualizations = {},
        .VisualizationScalars = visualizationScalars,
        .VisualizationFragmentBakeAtlases = visualizationBakes,
        .DebugLines = debugLines,
        .DebugPoints = debugPoints,
        .DebugTriangles = debugTriangles,
        .TransformGizmos = transformGizmos,
    });

    const glm::mat4 view = glm::lookAt(glm::vec3{0.f, 0.f, -5.f}, glm::vec3{0.f, 0.f, 0.f}, glm::vec3{0.f, 1.f, 0.f});
    const glm::mat4 projection = glm::perspective(glm::radians(60.f), 1280.f / 720.f, 0.1f, 100.f);
    const Graphics::RenderWorld world = renderer->ExtractRenderWorld(Graphics::RenderFrameInput{
        .Alpha = 0.25,
        .Viewport = {1280u, 720u},
        .HasPendingPick = true,
        .Pick = Graphics::PickPixelRequest{.X = 640u, .Y = 360u, .Pending = true},
        .Camera = Graphics::CameraViewInput{
            .View = view,
            .Projection = projection,
            .Position = {0.f, 0.f, -5.f},
            .Forward = {0.f, 0.f, 1.f},
            .Up = {0.f, 1.f, 0.f},
            .NearPlane = 0.1f,
            .FarPlane = 100.f,
            .Valid = true,
        },
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
    EXPECT_EQ(world.InvalidSnapshotRecordCount, 3u);
    EXPECT_TRUE(world.HasPendingPick);
    EXPECT_TRUE(world.DebugOverlayEnabled);
    EXPECT_TRUE(world.PickRequest.Pending);
    EXPECT_EQ(world.PickRequest.X, 640u);
    EXPECT_EQ(world.PickRequest.Y, 360u);
    EXPECT_TRUE(world.PickRequest.HasRay);
    EXPECT_TRUE(world.Camera.Valid);
    EXPECT_TRUE(world.Camera.HasPickRay);
    EXPECT_TRUE(world.Camera.FrustumPlanes[static_cast<std::uint32_t>(Graphics::FrustumPlaneIndex::Left)].Valid);
    EXPECT_NEAR(glm::length(world.PickRequest.RayDirection), 1.f, 0.0001f);
    EXPECT_FALSE(world.Selection.HasHovered);
    EXPECT_TRUE(world.Selection.SelectedStableIds.empty());
    EXPECT_FALSE(world.Shadows.Enabled);
    EXPECT_TRUE(world.DebugPrimitives.HasTransientDebug);
    EXPECT_EQ(world.DebugPrimitives.LineCount, 1u);
    EXPECT_EQ(world.DebugPrimitives.PointCount, 1u);
    EXPECT_EQ(world.DebugPrimitives.TriangleCount, 1u);
    ASSERT_EQ(world.DebugPrimitives.Lines.size(), 1u);
    ASSERT_EQ(world.DebugPrimitives.Points.size(), 1u);
    ASSERT_EQ(world.DebugPrimitives.Triangles.size(), 1u);
    EXPECT_FLOAT_EQ(world.DebugPrimitives.Lines[0].Width, 32.f);
    EXPECT_FLOAT_EQ(world.DebugPrimitives.Points[0].Radius, 0.0001f);
    EXPECT_EQ(world.DebugPrimitives.Triangles[0].Color, glm::vec4(0.f, 0.f, 1.f, 1.f));
    EXPECT_TRUE(world.Gizmos.HasGizmos);
    ASSERT_EQ(world.Gizmos.TransformGizmos.size(), 1u);
    EXPECT_EQ(world.Gizmos.TransformGizmoCount, 1u);
    EXPECT_EQ(world.Gizmos.TransformGizmos[0].StableId, 77u);
    EXPECT_FLOAT_EQ(world.Gizmos.TransformGizmos[0].AxisLength, 2.f);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    ASSERT_EQ(world.Visualization.FragmentBakeAtlases.size(), 2u);
    EXPECT_EQ(world.Visualization.Scalars[0].Name, "temperature");
    EXPECT_EQ(world.Visualization.FragmentBakeAtlases[0].Mapping,
              Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords);
    EXPECT_EQ(world.Visualization.FragmentBakeAtlases[1].Mapping,
              Graphics::VisualizationFragmentBakeMapping::RecreateHtex);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 3u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 3u);
    EXPECT_EQ(world.Visualization.Diagnostics.HtexRecreateRequestCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.UvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.HtexBakeAtlasDescriptorCount, 1u);
    EXPECT_TRUE(world.PostProcess.Enabled);
    EXPECT_FALSE(world.PostProcess.RequiresReadback);
    EXPECT_EQ(world.Viewport.Width, 1280u);
    EXPECT_EQ(world.Viewport.Height, 720u);

    renderer->Shutdown();
}

TEST(RenderWorldContract, BeginFrameRetainsRuntimeSnapshotSlotsUntilOverwrite)
{
    Tests::MockDevice device;
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const auto firstInstance = renderer->GetGpuWorld().AllocateInstance(7u);
    const auto secondInstance = renderer->GetGpuWorld().AllocateInstance(8u);
    ASSERT_TRUE(firstInstance.IsValid());
    ASSERT_TRUE(secondInstance.IsValid());

    const std::array<Graphics::TransformSyncRecord, 1> firstTransforms{{
        Graphics::TransformSyncRecord{
            .StableId = 7u,
            .Instance = firstInstance,
        },
    }};
    const std::array<Graphics::TransformSyncRecord, 1> secondTransforms{{
        Graphics::TransformSyncRecord{
            .StableId = 8u,
            .Instance = secondInstance,
        },
    }};
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = firstTransforms}, 0u);
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = secondTransforms}, 1u);

    const Graphics::RenderWorld firstWorld = renderer->ExtractRenderWorld({}, 0u);
    ASSERT_EQ(firstWorld.Renderables.size(), 1u);
    EXPECT_EQ(firstWorld.Renderables[0].StableId, 7u);
    const Graphics::RenderWorld secondWorld = renderer->ExtractRenderWorld({}, 1u);
    ASSERT_EQ(secondWorld.Renderables.size(), 1u);
    EXPECT_EQ(secondWorld.Renderables[0].StableId, 8u);

    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Graphics::RenderWorld retainedFirstWorld = renderer->ExtractRenderWorld({}, 0u);
    ASSERT_EQ(retainedFirstWorld.Renderables.size(), 1u);
    EXPECT_EQ(retainedFirstWorld.Renderables[0].StableId, 7u);
    EXPECT_FALSE(retainedFirstWorld.PickRequest.Pending);
    EXPECT_FALSE(retainedFirstWorld.PickRequest.HasRay);
    EXPECT_FALSE(retainedFirstWorld.Camera.Valid);
    EXPECT_FALSE(retainedFirstWorld.Selection.HasHovered);
    EXPECT_FALSE(retainedFirstWorld.Shadows.Enabled);

    const Graphics::RenderWorld retainedSecondWorld = renderer->ExtractRenderWorld({}, 1u);
    ASSERT_EQ(retainedSecondWorld.Renderables.size(), 1u);
    EXPECT_EQ(retainedSecondWorld.Renderables[0].StableId, 8u);

    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{}, 0u);
    const Graphics::RenderWorld emptyWorld = renderer->ExtractRenderWorld({}, 0u);
    EXPECT_TRUE(emptyWorld.Renderables.empty());
    EXPECT_TRUE(emptyWorld.Lights.empty());
    EXPECT_FALSE(emptyWorld.PickRequest.Pending);
    EXPECT_FALSE(emptyWorld.PickRequest.HasRay);
    EXPECT_FALSE(emptyWorld.Camera.Valid);
    EXPECT_FALSE(emptyWorld.Selection.HasHovered);
    EXPECT_FALSE(emptyWorld.Shadows.Enabled);
    EXPECT_FALSE(emptyWorld.DebugPrimitives.HasTransientDebug);
    EXPECT_FALSE(emptyWorld.Gizmos.HasGizmos);
    EXPECT_TRUE(emptyWorld.Gizmos.TransformGizmos.empty());
    EXPECT_TRUE(emptyWorld.DebugPrimitives.Lines.empty());
    EXPECT_TRUE(emptyWorld.DebugPrimitives.Points.empty());
    EXPECT_TRUE(emptyWorld.DebugPrimitives.Triangles.empty());
    EXPECT_EQ(emptyWorld.DebugPrimitives.LineCount, 0u);
    EXPECT_EQ(emptyWorld.DebugPrimitives.PointCount, 0u);
    EXPECT_EQ(emptyWorld.DebugPrimitives.TriangleCount, 0u);
    EXPECT_FALSE(emptyWorld.Visualization.HasVisualizationPackets);
    EXPECT_TRUE(emptyWorld.Visualization.Scalars.empty());
    EXPECT_TRUE(emptyWorld.Visualization.FragmentBakeAtlases.empty());
    EXPECT_EQ(emptyWorld.Visualization.Diagnostics.InputPacketCount, 0u);
    EXPECT_FALSE(emptyWorld.PostProcess.Enabled);
    EXPECT_EQ(emptyWorld.InvalidSnapshotRecordCount, 0u);

    const Graphics::RenderWorld stillRetainedSecondWorld = renderer->ExtractRenderWorld({}, 1u);
    ASSERT_EQ(stillRetainedSecondWorld.Renderables.size(), 1u);
    EXPECT_EQ(stillRetainedSecondWorld.Renderables[0].StableId, 8u);

    renderer->Shutdown();
}


