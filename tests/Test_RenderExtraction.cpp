#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <type_traits>

import Runtime.RenderExtraction;
import Runtime.SceneManager;
import Graphics.Camera;
import Graphics.RenderPipeline;

TEST(RenderExtraction, FrameContext_DefaultStateIsUnprepared)
{
    Runtime::FrameContext frame{};
    EXPECT_EQ(frame.FrameNumber, 0u);
    EXPECT_EQ(frame.PreviousFrameNumber, Runtime::InvalidFrameNumber);
    EXPECT_EQ(frame.SlotIndex, 0u);
    EXPECT_EQ(frame.FramesInFlight, Runtime::DefaultFrameContexts);
    EXPECT_FALSE(frame.Prepared);
    EXPECT_FALSE(frame.Submitted);
    EXPECT_FALSE(frame.Viewport.IsValid());
    EXPECT_EQ(frame.GetPreparedRenderWorld(), nullptr);
}

TEST(RenderExtraction, FrameContext_ResetPreparedStateClearsOwnedRenderWorld)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = Runtime::RenderWorld{
        .Alpha = 0.5,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 800, .Height = 600}),
        .World = sceneManager.CreateReadonlySnapshot(),
    };
    frame.Prepared = true;
    frame.Submitted = true;

    ASSERT_NE(frame.GetPreparedRenderWorld(), nullptr);
    EXPECT_EQ(frame.GetPreparedRenderWorld()->World.CommittedTick, 1u);

    frame.ResetPreparedState();

    EXPECT_EQ(frame.GetPreparedRenderWorld(), nullptr);
    EXPECT_FALSE(frame.Prepared);
    EXPECT_FALSE(frame.Submitted);
}

TEST(RenderExtraction, SanitizeFrameContextCount_ClampsToSupportedBounds)
{
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(0u), Runtime::MinFrameContexts);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(1u), Runtime::MinFrameContexts);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(2u), 2u);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(3u), 3u);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(99u), Runtime::MaxFrameContexts);
}

TEST(RenderExtraction, FrameContextRing_DefaultsToDoubleBuffering)
{
    Runtime::FrameContextRing ring;
    EXPECT_EQ(ring.GetFramesInFlight(), Runtime::DefaultFrameContexts);

    const Runtime::FrameContext& frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 1600, .Height = 900});

    EXPECT_EQ(frame0.FrameNumber, 0u);
    EXPECT_EQ(frame0.PreviousFrameNumber, Runtime::InvalidFrameNumber);
    EXPECT_EQ(frame0.SlotIndex, 0u);
    EXPECT_EQ(frame0.FramesInFlight, Runtime::DefaultFrameContexts);
    EXPECT_TRUE(frame0.Viewport.IsValid());
}

TEST(RenderExtraction, FrameContextRing_ReusesBoundedSlotsByFrameNumberModulo)
{
    Runtime::FrameContextRing ring(3u);

    Runtime::FrameContext& frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    const Runtime::FrameContext* frame0Address = &frame0;

    Runtime::FrameContext& frame1 =
        ring.BeginFrame(1u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    Runtime::FrameContext& frame2 =
        ring.BeginFrame(2u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    frame0.Prepared = true;
    frame0.Submitted = true;
    frame0.PreparedRenderWorld = Runtime::RenderWorld{
        .Alpha = 0.25,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 640, .Height = 480}),
    };

    Runtime::FrameContext& frame3 =
        ring.BeginFrame(3u, Runtime::RenderViewport{.Width = 640, .Height = 480});

    EXPECT_EQ(frame0.SlotIndex, 0u);
    EXPECT_EQ(frame1.SlotIndex, 1u);
    EXPECT_EQ(frame2.SlotIndex, 2u);
    EXPECT_EQ(frame3.SlotIndex, 0u);
    EXPECT_EQ(frame3.PreviousFrameNumber, 0u);
    EXPECT_EQ(frame3.FramesInFlight, 3u);
    EXPECT_EQ(&frame3, frame0Address);
    EXPECT_FALSE(frame3.Prepared);
    EXPECT_FALSE(frame3.Submitted);
    EXPECT_EQ(frame3.GetPreparedRenderWorld(), nullptr);
}

TEST(RenderExtraction, MakeRenderFrameInput_SanitizesAlphaAndCapturesSnapshotGeneration)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    camera.AspectRatio = 16.0f / 9.0f;

    const Runtime::RenderFrameInput input = Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1920, .Height = 1080},
        4.0);

    EXPECT_TRUE(input.IsValid());
    EXPECT_DOUBLE_EQ(input.Alpha, 1.0);
    EXPECT_EQ(input.World.CommittedTick, 2u);
    EXPECT_EQ(input.View.Camera.Position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(input.View.CameraPosition, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(input.View.Viewport.Width, 1920u);
    EXPECT_EQ(input.View.Viewport.Height, 1080u);
}

TEST(RenderExtraction, WorldSnapshot_DetectsAuthoritativeCommitDrift)
{
    Runtime::SceneManager sceneManager;

    const Runtime::WorldSnapshot snapshot = sceneManager.CreateReadonlySnapshot();
    EXPECT_TRUE(snapshot.IsValid());
    EXPECT_TRUE(snapshot.IsCurrent());
    EXPECT_FALSE(snapshot.HasCommitDrift());
    EXPECT_EQ(snapshot.GetCurrentCommittedTick(), 0u);

    sceneManager.CommitFixedTick();

    EXPECT_FALSE(snapshot.IsCurrent());
    EXPECT_TRUE(snapshot.HasCommitDrift());
    EXPECT_EQ(snapshot.CommittedTick, 0u);
    EXPECT_EQ(snapshot.GetCurrentCommittedTick(), 1u);
}

TEST(RenderExtraction, ExtractRenderWorld_CopiesImmutableFrameInputs)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(4.0f, 5.0f, 6.0f);
    camera.AspectRatio = 4.0f / 3.0f;

    const Runtime::RenderFrameInput input = Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1280, .Height = 720},
        0.25);

    camera.Position = glm::vec3(99.0f);

    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(input);

    EXPECT_TRUE(renderWorld.IsValid());
    EXPECT_DOUBLE_EQ(renderWorld.Alpha, 0.25);
    EXPECT_EQ(renderWorld.World.CommittedTick, 1u);
    EXPECT_EQ(renderWorld.View.Camera.Position, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(renderWorld.View.CameraForward, renderWorld.View.Camera.GetForward());
    EXPECT_EQ(renderWorld.View.Viewport.Width, 1280u);
    EXPECT_EQ(renderWorld.View.Viewport.Height, 720u);
    EXPECT_EQ(renderWorld.View.ViewProjectionMatrix,
              renderWorld.View.ProjectionMatrix * renderWorld.View.ViewMatrix);
}

TEST(RenderExtraction, ExtractedRenderWorld_TracksWhenAuthoritativeSceneAdvancesPastExtraction)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 800, .Height = 600},
        0.5));

    ASSERT_TRUE(renderWorld.IsValid());
    EXPECT_TRUE(renderWorld.World.IsCurrent());
    EXPECT_FALSE(renderWorld.World.HasCommitDrift());

    sceneManager.CommitFixedTick();

    EXPECT_FALSE(renderWorld.World.IsCurrent());
    EXPECT_TRUE(renderWorld.World.HasCommitDrift());
    EXPECT_EQ(renderWorld.World.CommittedTick, 1u);
    EXPECT_EQ(renderWorld.World.GetCurrentCommittedTick(), 2u);
}

TEST(RenderExtraction, RenderPassContext_ExposesReadonlySceneSnapshot)
{
    using SceneRef = decltype(std::declval<Graphics::RenderPassContext>().Scene);
    static_assert(std::is_const_v<std::remove_reference_t<SceneRef>>);
    SUCCEED();
}

TEST(RenderExtraction, FrameContext_OwnsPreparedRenderWorldCopy)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(7.0f, 8.0f, 9.0f);

    Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1024, .Height = 768},
        0.75));

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = renderWorld;
    frame.Prepared = true;

    renderWorld.Alpha = 0.0;
    renderWorld.View.Camera.Position = glm::vec3(0.0f);
    renderWorld.View.CameraPosition = glm::vec3(0.0f);

    const Runtime::RenderWorld* owned = frame.GetPreparedRenderWorld();
    ASSERT_NE(owned, nullptr);
    EXPECT_DOUBLE_EQ(owned->Alpha, 0.75);
    EXPECT_EQ(owned->View.Camera.Position, glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_EQ(owned->View.CameraPosition, glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_EQ(owned->World.CommittedTick, 1u);
}

TEST(RenderExtraction, MakeRenderViewPacket_CapturesDerivedCameraState)
{
    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(-2.0f, 3.0f, 5.0f);
    camera.Fov = 60.0f;
    camera.AspectRatio = 21.0f / 9.0f;
    camera.Near = 0.25f;
    camera.Far = 250.0f;
    camera.Orientation = glm::normalize(glm::quat(glm::vec3(0.2f, -0.35f, 0.1f)));
    Graphics::UpdateMatrices(camera, camera.AspectRatio);

    const Runtime::RenderViewPacket view =
        Runtime::MakeRenderViewPacket(camera, Runtime::RenderViewport{.Width = 3440, .Height = 1440});

    EXPECT_TRUE(view.IsValid());
    EXPECT_EQ(view.Camera.Position, camera.Position);
    EXPECT_EQ(view.CameraPosition, camera.Position);
    EXPECT_EQ(view.CameraForward, camera.GetForward());
    EXPECT_EQ(view.ViewMatrix, camera.ViewMatrix);
    EXPECT_EQ(view.ProjectionMatrix, camera.ProjectionMatrix);
    EXPECT_EQ(view.ViewProjectionMatrix, camera.ProjectionMatrix * camera.ViewMatrix);
    EXPECT_EQ(view.NearPlane, camera.Near);
    EXPECT_EQ(view.FarPlane, camera.Far);
    EXPECT_EQ(view.AspectRatio, camera.AspectRatio);
    EXPECT_EQ(view.VerticalFieldOfViewDegrees, camera.Fov);
    EXPECT_EQ(view.Viewport.Width, 3440u);
    EXPECT_EQ(view.Viewport.Height, 1440u);
}
