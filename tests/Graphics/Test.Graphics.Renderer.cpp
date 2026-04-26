#include <gtest/gtest.h>

import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.RHI.FrameHandle;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

TEST(GraphicsRenderer, NullRendererExecutesRenderGraphPath)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Alpha = 0.25,
        .Viewport = {.Width = 1280u, .Height = 720u},
    };
    auto world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);

    renderer->ExecuteFrame(frame, world);
    renderer->Resize(1920u, 1080u);
    renderer->ExecuteFrame(frame, world);

    EXPECT_EQ(renderer->EndFrame(frame), 0u);
    renderer->Shutdown();
}
