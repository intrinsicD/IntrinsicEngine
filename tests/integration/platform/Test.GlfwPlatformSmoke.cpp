#include <chrono>
#include <memory>
#include <string>
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
// granted change without its events fails.
TEST(GlfwPlatformSmoke, NativeFocusChangesEmitWindowFocusEvents)
{
    namespace Glfw = Extrinsic::Platform::Backends::Glfw;
    using Extrinsic::Platform::WindowFocusEvent;
    if (!Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW could not initialize (no display); native focus needs a window-capable host.";

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
