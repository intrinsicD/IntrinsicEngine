#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>

import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Engine;

using Extrinsic::Core::Config::WindowConfig;
using Extrinsic::Core::Config::RenderConfig;
using Extrinsic::Core::Config::EngineConfig;
using Extrinsic::Core::Config::GraphicsBackend;

// -----------------------------------------------------------------------------
// WindowConfig defaults
// -----------------------------------------------------------------------------

TEST(CoreConfig, WindowConfigDefaultValues)
{
    WindowConfig cfg{};
    EXPECT_EQ(cfg.Title, "Extrinsic");
    EXPECT_EQ(cfg.Width, 1920);
    EXPECT_EQ(cfg.Height, 1080);
    EXPECT_TRUE(cfg.Resizable);
}

TEST(CoreConfig, WindowConfigIsAggregateInitialisable)
{
    WindowConfig cfg{
        .Title = "MyApp",
        .Width = 640,
        .Height = 480,
        .Resizable = false,
    };
    EXPECT_EQ(cfg.Title, "MyApp");
    EXPECT_EQ(cfg.Width, 640);
    EXPECT_EQ(cfg.Height, 480);
    EXPECT_FALSE(cfg.Resizable);
}

TEST(CoreConfig, WindowConfigIsCopyable)
{
    WindowConfig a{.Title = "A", .Width = 800, .Height = 600, .Resizable = false};
    WindowConfig b = a;
    EXPECT_EQ(b.Title, "A");
    EXPECT_EQ(b.Width, 800);
    EXPECT_EQ(b.Height, 600);
    EXPECT_FALSE(b.Resizable);
}

// -----------------------------------------------------------------------------
// RenderConfig defaults
// -----------------------------------------------------------------------------

TEST(CoreConfig, RenderConfigDefaultValues)
{
    RenderConfig cfg{};
    EXPECT_EQ(cfg.Backend, GraphicsBackend::Vulkan);
    EXPECT_TRUE(cfg.EnableValidation);
    EXPECT_TRUE(cfg.EnableVSync);
    EXPECT_EQ(cfg.FramesInFlight, 2u);
}

TEST(CoreConfig, RenderConfigIsAggregateInitialisable)
{
    RenderConfig cfg{
        .Backend = GraphicsBackend::Vulkan,
        .EnableValidation = false,
        .EnableVSync = false,
        .FramesInFlight = 3u,
    };
    EXPECT_FALSE(cfg.EnableValidation);
    EXPECT_FALSE(cfg.EnableVSync);
    EXPECT_EQ(cfg.FramesInFlight, 3u);
}

TEST(CoreConfig, GraphicsBackendUnderlyingIsUint8)
{
    static_assert(std::is_same_v<std::underlying_type_t<GraphicsBackend>, std::uint8_t>);
    EXPECT_EQ(static_cast<std::uint8_t>(GraphicsBackend::Vulkan), 0u);
}

// -----------------------------------------------------------------------------
// EngineConfig composition
// -----------------------------------------------------------------------------

TEST(CoreConfig, EngineConfigAggregatesRenderAndWindow)
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

TEST(CoreConfig, EngineConfigOverrides)
{
    EngineConfig cfg{};
    cfg.Window.Title = "Editor";
    cfg.Window.Width = 1280;
    cfg.Render.FramesInFlight = 3u;
    cfg.Render.EnableValidation = false;

    EXPECT_EQ(cfg.Window.Title, "Editor");
    EXPECT_EQ(cfg.Window.Width, 1280);
    EXPECT_EQ(cfg.Render.FramesInFlight, 3u);
    EXPECT_FALSE(cfg.Render.EnableValidation);

    // Untouched fields retain their defaults.
    EXPECT_EQ(cfg.Window.Height, 1080);
    EXPECT_TRUE(cfg.Render.EnableVSync);
}
