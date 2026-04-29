#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;

using namespace Extrinsic::Core::Config;

TEST(CoreConfig, EngineDefaults)
{
    EngineConfig cfg{};

    EXPECT_EQ(cfg.Render.Backend, GraphicsBackend::Vulkan);
    EXPECT_TRUE(cfg.Render.EnableValidation);
    EXPECT_TRUE(cfg.Render.EnableVSync);
    EXPECT_EQ(cfg.Render.FramesInFlight, 2u);

    EXPECT_EQ(cfg.Window.Title, "Extrinsic");
    EXPECT_EQ(cfg.Window.Width, 1920);
    EXPECT_EQ(cfg.Window.Height, 1080);
    EXPECT_TRUE(cfg.Window.Resizable);
}
