// Simulates minimization while a real Engine drives GPU completions after one operational frame. This does not test
// native window-manager behavior. Include after Glfw, Core.Config.Window, Engine and JobService.
#pragma once

namespace Intrinsic::Tests
{
    template <class Advance>
    void RunWithMinimizedGpuCompletions(
        Extrinsic::Runtime::Engine& engine, Advance&& advance, const bool beginShutdown = false)
    {
        // These targets are built only with GLFW. No RTTI: check the factory's runtime override before casting.
        ASSERT_EQ(engine.GetEngineConfig().Window.Backend, Extrinsic::Core::Config::WindowBackend::Configured);
        ASSERT_NE(engine.GetWindow().GetNativeHandle(), nullptr);
        auto& window = static_cast<Extrinsic::Platform::Backends::Glfw::Window&>(engine.GetWindow());
        struct Restore
        {
            Extrinsic::Runtime::Engine& Engine;
            Extrinsic::Platform::Backends::Glfw::Window& Window;
            Extrinsic::Runtime::GpuQueueParticipantHandle Participant{};
            ~Restore()
            {
                Engine.Jobs().UnregisterGpuQueueParticipant(Participant);
                Window.SetMinimizedForTesting(false);
            }
        } restore{engine, window};
        bool minimized = false;
        std::uint64_t frame = 0u;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        restore.Participant = engine.Jobs().RegisterGpuQueueParticipant({.DebugName = "minimized GPU method probe",
            .DrainCompletedTransfers = [&]
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    ADD_FAILURE() << "Minimized method did not finish within 30 seconds.";
                    engine.RequestExit();
                    return;
                }
                if (!minimized)
                {
                    if (!engine.GetDevice().IsOperational())
                        return;
                    window.SetMinimizedForTesting(true);
                    minimized = window.IsMinimized();
                    frame = engine.GetDevice().GetGlobalFrameNumber();
                    EXPECT_TRUE(minimized);
                    return;
                }
                EXPECT_EQ(engine.GetDevice().GetGlobalFrameNumber(), frame);
                advance();
            }});
        // If completion draining itself is removed, CTest's timeout bounds the otherwise idle loop.
        engine.Run();
        EXPECT_TRUE(minimized);
        EXPECT_EQ(engine.GetDevice().GetGlobalFrameNumber(), frame);
        if (beginShutdown)
        {
            EXPECT_TRUE(window.IsMinimized());
            engine.BeginShutdown();
            EXPECT_EQ(engine.GetDevice().GetGlobalFrameNumber(), frame);
        }
    }
}
