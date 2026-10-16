#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;

TEST(GlfwPlatformSmoke, BackendModuleInitializesOrSkipsWhenDisplayUnavailable)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; smoke test is opt-in for window-capable hosts.";
    }

    SUCCEED();
}

// UI-078: a real window-system focus change reaches the production GLFW focus
// callback as WindowFocusEvent{true} on the window gaining focus and {false} on
// the one losing it. Focus is requested from the window manager (glfwFocusWindow);
// no callback is invoked by hand. A window manager that does not grant the
// request (GLFW_FOCUSED never follows it) is a host limitation and skips; a
// granted change without its events fails. Native Wayland grants no focus
// change without a user-input activation token, so it skips there.
TEST(GlfwPlatformSmoke, NativeFocusChangesEmitWindowFocusEvents)
{
    namespace Glfw = Extrinsic::Platform::Backends::Glfw;
    using Extrinsic::Platform::WindowFocusEvent;
    if (!Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW could not initialize (no display); native focus needs a window-capable host.";
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        GTEST_SKIP() << "Native Wayland: programmatic focus (xdg_activation) needs a user-input token; "
                     << "native focus evidence comes from X11.";

    Extrinsic::Core::Config::WindowConfig config{.Title = "GlfwFocusSmoke A", .Width = 320, .Height = 240};
    auto a = std::make_unique<Glfw::Window>(config);
    config.Title = "GlfwFocusSmoke B";
    auto b = std::make_unique<Glfw::Window>(config);
    auto* nativeA = static_cast<GLFWwindow*>(a->GetNativeHandle());
    auto* nativeB = static_cast<GLFWwindow*>(b->GetNativeHandle());
    if (nativeA == nullptr || nativeB == nullptr)
        GTEST_SKIP() << "GLFW initialized but could not create two windows on this host.";

    std::vector<bool> eventsA{};
    std::vector<bool> eventsB{};
    const auto pump = [&](const std::chrono::milliseconds duration, const auto& done)
    {
        const auto end = std::chrono::steady_clock::now() + duration;
        do
        {
            a->WaitForEventsTimeout(0.02);
            for (auto* window : {a.get(), b.get()})
                for (const auto& event : window->DrainEvents())
                    if (const auto* focus = std::get_if<WindowFocusEvent>(&event))
                        (window == a.get() ? eventsA : eventsB).push_back(focus->Focused);
        } while (!done() && std::chrono::steady_clock::now() < end);
    };
    const auto focused = [](GLFWwindow* window) { return glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE; };

    // Let the window manager map both windows, then start from B focused so
    // that every request below (A, B, A) is a real focus transition.
    pump(std::chrono::milliseconds{500}, [] { return false; });
    glfwFocusWindow(nativeB);
    pump(std::chrono::milliseconds{3000}, [&] { return focused(nativeB); });
    if (!focused(nativeB))
    {
        GTEST_SKIP() << "The window manager did not grant the initial programmatic focus request within 3 s "
                     << "(GLFW_FOCUSED stayed false); native focus evidence is unavailable on this host.";
    }

    struct Request
    {
        GLFWwindow* Target;
        GLFWwindow* Other;
        const char* Name;
    };
    for (const Request request : {Request{nativeA, nativeB, "A"}, Request{nativeB, nativeA, "B"},
                                  Request{nativeA, nativeB, "A again"}})
    {
        SCOPED_TRACE(request.Name);
        const bool otherHadFocus = focused(request.Other);
        eventsA.clear();
        eventsB.clear();
        std::vector<bool>& target = request.Target == nativeA ? eventsA : eventsB;
        std::vector<bool>& other = request.Target == nativeA ? eventsB : eventsA;
        glfwFocusWindow(request.Target);
        pump(std::chrono::milliseconds{3000}, [&]
        {
            return focused(request.Target) && !target.empty() && target.back() &&
                   (!otherHadFocus || (!other.empty() && !other.back()));
        });
        if (!focused(request.Target))
        {
            GTEST_SKIP() << "The window manager did not grant the programmatic focus request for window "
                         << request.Name << " within 3 s (GLFW_FOCUSED stayed false); native focus evidence "
                         << "is unavailable on this host.";
        }
        ASSERT_FALSE(target.empty()) << "window " << request.Name << " gained focus without a WindowFocusEvent";
        EXPECT_TRUE(target.back());
        EXPECT_FALSE(focused(request.Other));
        if (otherHadFocus)
        {
            ASSERT_FALSE(other.empty()) << "the other window lost focus without a WindowFocusEvent";
            EXPECT_FALSE(other.back());
        }
    }
}


TEST(GlfwPlatformSmoke, OwningWindowIsNeitherCopyableNorMovable)
{
    using Window = Extrinsic::Platform::Backends::Glfw::Window;
    EXPECT_FALSE(std::is_copy_constructible_v<Window>);
    EXPECT_FALSE(std::is_copy_assignable_v<Window>);
    EXPECT_FALSE(std::is_move_constructible_v<Window>);
    EXPECT_FALSE(std::is_move_assignable_v<Window>);
}

namespace
{
    [[nodiscard]] std::unique_ptr<Extrinsic::Platform::Backends::Glfw::Window>
    CreateMappedMinimizeTestWindow()
    {
        namespace Glfw = Extrinsic::Platform::Backends::Glfw;
        if (!Glfw::CanInitialize())
            return nullptr;
        Extrinsic::Core::Config::WindowConfig config{
            .Title = "GlfwMinimizeSmoke", .Width = 320, .Height = 240};
        auto window = std::make_unique<Glfw::Window>(config);
        if (window->GetNativeHandle() == nullptr)
            return nullptr;
        // Map the window before making window-manager iconification requests.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        do { window->WaitForEventsTimeout(0.02); }
        while (std::chrono::steady_clock::now() < deadline);
        return window;
    }

    [[nodiscard]] bool WaitForNativeIconified(
        Extrinsic::Platform::Backends::Glfw::Window& window, const bool expected)
    {
        auto* native = static_cast<GLFWwindow*>(window.GetNativeHandle());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do
        {
            window.WaitForEventsTimeout(0.02);
            if ((glfwGetWindowAttrib(native, GLFW_ICONIFIED) == GLFW_TRUE) == expected)
                return true;
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
}

TEST(GlfwPlatformSmoke, NativeIconifyUpdatesMinimizedState)
{
    auto window = CreateMappedMinimizeTestWindow();
    if (!window)
        GTEST_SKIP() << "No native GLFW window is available; minimization requires a display.";
    EXPECT_FALSE(window->IsMinimized());
    glfwIconifyWindow(static_cast<GLFWwindow*>(window->GetNativeHandle()));
    if (!WaitForNativeIconified(*window, true))
        GTEST_SKIP() << "The window manager did not grant native iconification within 3 seconds.";
    const auto extent = window->GetFramebufferExtent();
    EXPECT_TRUE(window->IsMinimized()) << "GLFW_ICONIFIED is true with framebuffer "
        << extent.Width << 'x' << extent.Height;
}

TEST(GlfwPlatformSmoke, NativeRestoreClearsMinimizedState)
{
    auto window = CreateMappedMinimizeTestWindow();
    if (!window)
        GTEST_SKIP() << "No native GLFW window is available; restoration requires a display.";
    auto* native = static_cast<GLFWwindow*>(window->GetNativeHandle());
    glfwIconifyWindow(native);
    if (!WaitForNativeIconified(*window, true))
        GTEST_SKIP() << "The window manager did not grant native iconification within 3 seconds.";
    glfwRestoreWindow(native);
    if (!WaitForNativeIconified(*window, false))
        GTEST_SKIP() << "The window manager did not grant native restoration within 3 seconds.";
    EXPECT_FALSE(window->IsMinimized());
}

TEST(GlfwPlatformSmoke, NativeResizeCallbackDoesNotBorrowDrainedQueueStorage)
{
    namespace Glfw = Extrinsic::Platform::Backends::Glfw;
    using namespace Extrinsic::Platform;
    if (!Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW cannot initialize; native resize requires a display.";
    Extrinsic::Core::Config::WindowConfig config{
        .Title = "GlfwCallbackStorageSmoke", .Width = 320, .Height = 240};
    Glfw::Window window(config);
    auto* native = static_cast<GLFWwindow*>(window.GetNativeHandle());
    if (native == nullptr)
        GTEST_SKIP() << "GLFW initialized but could not create the native test window.";
    const auto mappedAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    do { window.WaitForEventsTimeout(0.02); }
    while (std::chrono::steady_clock::now() < mappedAt);
    (void)window.DrainEvents();
    int originalWidth = 0, originalHeight = 0;
    glfwGetFramebufferSize(native, &originalWidth, &originalHeight);
    bool observedResize = false;
    window.Listen([&](const Event& event)
    {
        const auto* resize = std::get_if<WindowResizeEvent>(&event);
        if (resize == nullptr)
            return;
        observedResize = true;
        const auto expected = *resize;
        auto drained = window.DrainEvents();
        ASSERT_FALSE(drained.empty());
        // The drained queue remains alive, so overwriting its last element tests
        // aliasing deterministically without dereferencing freed storage.
        drained.back() = WindowCloseEvent{};
        const auto* afterDrain = std::get_if<WindowResizeEvent>(&event);
        ASSERT_NE(afterDrain, nullptr) << "callback argument aliases the drained queue";
        EXPECT_EQ(afterDrain->Width, expected.Width);
        EXPECT_EQ(afterDrain->Height, expected.Height);
    });
    glfwSetWindowSize(native, 480, 360);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do { window.WaitForEventsTimeout(0.02); }
    while (!observedResize && std::chrono::steady_clock::now() < deadline);
    int width = 0, height = 0;
    glfwGetFramebufferSize(native, &width, &height);
    if (!observedResize && width == originalWidth && height == originalHeight)
        GTEST_SKIP() << "The host did not change the framebuffer size after the native resize request.";
    EXPECT_TRUE(observedResize) << "native framebuffer changed without a resize callback";
}
