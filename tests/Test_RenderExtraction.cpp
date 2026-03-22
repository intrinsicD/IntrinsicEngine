#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Runtime.RenderExtraction;
import Runtime.SceneManager;
import Graphics.Camera;

TEST(RenderExtraction, FrameContext_DefaultStateIsUnprepared)
{
    Runtime::FrameContext frame{};
    EXPECT_EQ(frame.FrameNumber, 0u);
    EXPECT_FALSE(frame.Prepared);
    EXPECT_FALSE(frame.Submitted);
    EXPECT_FALSE(frame.Viewport.IsValid());
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
