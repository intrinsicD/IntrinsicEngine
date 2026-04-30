#include <gtest/gtest.h>

#include <string>
#include <vector>

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;

namespace
{
    using Extrinsic::Core::Config::WindowConfig;
    using Extrinsic::Platform::Backends::Null::NullWindow;
}

TEST(NullPlatform, QueuesDrainsAndAppliesEventsDeterministically)
{
    WindowConfig config;
    config.Width = 320;
    config.Height = 240;

    NullWindow window{config};
    std::vector<Extrinsic::Platform::Event> callbacks;
    window.Listen([&callbacks](const Extrinsic::Platform::Event& event)
    {
        callbacks.push_back(event);
    });

    window.QueueResize(640, 480);
    window.QueueKey(Extrinsic::Platform::Input::Key::Space, true);
    window.QueueScroll(2.0, -1.0);
    window.QueueCursor(12.5, 24.25);

    const Extrinsic::Platform::IWindow& observedWindow = window;
    EXPECT_FALSE(observedWindow.GetInput().IsKeyPressed(Extrinsic::Platform::Input::Key::Space));

    window.PollEvents();

    EXPECT_EQ(window.GetWindowExtent().Width, 640);
    EXPECT_EQ(window.GetWindowExtent().Height, 480);
    EXPECT_EQ(window.GetFramebufferExtent().Width, 640);
    EXPECT_EQ(window.GetFramebufferExtent().Height, 480);
    EXPECT_TRUE(window.WasResized());
    EXPECT_TRUE(window.ConsumeInputActivity());
    EXPECT_FALSE(window.ConsumeInputActivity());

    EXPECT_TRUE(observedWindow.GetInput().IsKeyPressed(Extrinsic::Platform::Input::Key::Space));
    EXPECT_TRUE(observedWindow.GetInput().IsKeyJustPressed(Extrinsic::Platform::Input::Key::Space));
    EXPECT_FLOAT_EQ(observedWindow.GetInput().GetScrollDelta().x, 2.0f);
    EXPECT_FLOAT_EQ(observedWindow.GetInput().GetScrollDelta().y, -1.0f);
    EXPECT_FLOAT_EQ(observedWindow.GetInput().GetMousePosition().x, 12.5f);
    EXPECT_FLOAT_EQ(observedWindow.GetInput().GetMousePosition().y, 24.25f);

    const auto drained = window.DrainEvents();
    EXPECT_EQ(drained.size(), 4u);
    EXPECT_EQ(callbacks.size(), 4u);
    EXPECT_TRUE(window.DrainEvents().empty());

    window.AcknowledgeResize();
    EXPECT_FALSE(window.WasResized());

    window.PollEvents();
    EXPECT_TRUE(observedWindow.GetInput().IsKeyPressed(Extrinsic::Platform::Input::Key::Space));
    EXPECT_FALSE(observedWindow.GetInput().IsKeyJustPressed(Extrinsic::Platform::Input::Key::Space));
    EXPECT_FLOAT_EQ(observedWindow.GetInput().GetScrollDelta().x, 0.0f);
    EXPECT_FLOAT_EQ(observedWindow.GetInput().GetScrollDelta().y, 0.0f);
}

TEST(NullPlatform, SupportsHeadlessClipboardCursorMinimizeAndClose)
{
    WindowConfig config;
    config.Width = 32;
    config.Height = 16;

    NullWindow window{config};
    EXPECT_FALSE(window.IsMinimized());
    EXPECT_FALSE(window.ShouldClose());

    window.SetClipboardText("deterministic clipboard");
    EXPECT_EQ(window.GetClipboardText(), "deterministic clipboard");

    window.SetCursorMode(Extrinsic::Platform::CursorMode::Disabled);
    EXPECT_EQ(window.GetCursorMode(), Extrinsic::Platform::CursorMode::Disabled);

    window.QueueResize(0, 0);
    window.QueueClose();
    window.PollEvents();

    EXPECT_TRUE(window.IsMinimized());
    EXPECT_TRUE(window.ShouldClose());
}


