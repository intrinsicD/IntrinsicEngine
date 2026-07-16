#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define INTRINSIC_BUG082_HAS_ASAN 1
#  endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#  define INTRINSIC_BUG082_HAS_ASAN 1
#endif

import Extrinsic.Platform.Backend.Glfw;

namespace
{
    constexpr int kSkipExitCode = 77;
    constexpr int kTeardownFailureExitCode = 78;

    int g_GlfwTerminateCalls = 0;
    bool g_RequireEngineStaticTeardown = false;

    struct Bug082SyntheticEngineLeak
    {
        std::array<unsigned char, 4096> Payload{};
    };

    [[gnu::noinline]] void AllocateBug082SyntheticEngineLeak()
    {
        auto* leaked = new Bug082SyntheticEngineLeak{};
        asm volatile("" : : "r"(leaked) : "memory");
    }

    void VerifyEngineStaticTeardown()
    {
        if (!g_RequireEngineStaticTeardown)
            return;

        if (g_GlfwTerminateCalls != 1)
        {
            std::fprintf(
                stderr,
                "BUG082_GLFW_STATIC_TEARDOWN_FAILED: terminate_calls=%d\n",
                g_GlfwTerminateCalls);
            std::_Exit(kTeardownFailureExitCode);
        }

        std::fprintf(
            stderr,
            "BUG082_GLFW_STATIC_TEARDOWN: terminate_calls=%d\n",
            g_GlfwTerminateCalls);
    }

    int RunEngineStaticLifetime()
    {
#if !defined(INTRINSIC_BUG082_HAS_ASAN)
        std::fprintf(stderr, "BUG082_SKIP: address sanitizer is not active\n");
        return kSkipExitCode;
#else
        if (std::getenv("DISPLAY") == nullptr)
        {
            std::fprintf(stderr, "BUG082_SKIP: DISPLAY is unavailable\n");
            return kSkipExitCode;
        }

        if (std::atexit(VerifyEngineStaticTeardown) != 0)
        {
            std::fprintf(stderr, "BUG082_GLFW_STATIC_TEARDOWN_FAILED: atexit registration failed\n");
            return kTeardownFailureExitCode;
        }

        if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        {
            std::fprintf(stderr, "BUG082_SKIP: GLFW could not initialize on the configured display\n");
            return kSkipExitCode;
        }

        // GLFWLifetime's function-local static is registered after the checker
        // above, so reverse atexit order runs its destructor (and the wrapped
        // glfwTerminate) before VerifyEngineStaticTeardown.
        g_RequireEngineStaticTeardown = true;
        return 0;
#endif
    }

    int RunSyntheticEngineLeak()
    {
#if !defined(INTRINSIC_BUG082_HAS_ASAN)
        std::fprintf(stderr, "BUG082_SKIP: address sanitizer is not active\n");
        return kSkipExitCode;
#else
        AllocateBug082SyntheticEngineLeak();
        std::fprintf(stderr, "BUG082_SYNTHETIC_ENGINE_LEAK_ALLOCATED: bytes=4096\n");
        return 0;
#endif
    }
}

extern "C" void __real_glfwTerminate();

extern "C" void __wrap_glfwTerminate()
{
    ++g_GlfwTerminateCalls;
    __real_glfwTerminate();
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <engine-static-lifetime|synthetic-engine-leak>\n", argv[0]);
        return 2;
    }

    const std::string_view mode{argv[1]};
    if (mode == "engine-static-lifetime")
        return RunEngineStaticLifetime();
    if (mode == "synthetic-engine-leak")
        return RunSyntheticEngineLeak();

    std::fprintf(stderr, "unknown BUG-082 probe mode: %s\n", argv[1]);
    return 2;
}
