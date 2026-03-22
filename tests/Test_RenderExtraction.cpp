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

    const Runtime::FrameContext frame0 =
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

    const Runtime::FrameContext frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    const Runtime::FrameContext frame1 =
        ring.BeginFrame(1u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    const Runtime::FrameContext frame2 =
        ring.BeginFrame(2u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    const Runtime::FrameContext frame3 =
        ring.BeginFrame(3u, Runtime::RenderViewport{.Width = 640, .Height = 480});

    EXPECT_EQ(frame0.SlotIndex, 0u);
    EXPECT_EQ(frame1.SlotIndex, 1u);
    EXPECT_EQ(frame2.SlotIndex, 2u);
    EXPECT_EQ(frame3.SlotIndex, 0u);
    EXPECT_EQ(frame3.PreviousFrameNumber, 0u);
    EXPECT_EQ(frame3.FramesInFlight, 3u);
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
    EXPECT_EQ(input.Camera.Position, glm::vec3(1.0f, 2.0f, 3.0f));
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
    EXPECT_EQ(renderWorld.Camera.Position, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(renderWorld.Viewport.Width, 1280u);
    EXPECT_EQ(renderWorld.Viewport.Height, 720u);
}

TEST(RenderExtraction, RenderPassContext_ExposesReadonlySceneSnapshot)
{
    using SceneRef = decltype(std::declval<Graphics::RenderPassContext>().Scene);
    static_assert(std::is_const_v<std::remove_reference_t<SceneRef>>);
    SUCCEED();
}
