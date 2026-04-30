#include <gtest/gtest.h>

import Extrinsic.Platform.Backend.Glfw;

TEST(GlfwPlatformSmoke, BackendModuleInitializesOrSkipsWhenDisplayUnavailable)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; smoke test is opt-in for window-capable hosts.";
    }

    SUCCEED();
}

