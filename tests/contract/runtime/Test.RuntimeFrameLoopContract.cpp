#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

import Extrinsic.Runtime.FrameLoop;

namespace
{
    using Trace = std::vector<std::string>;

    class FakePlatformHooks final : public Extrinsic::Runtime::IRuntimePlatformFrameHooks
    {
    public:
        explicit FakePlatformHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        void PollEvents() override { m_Trace.emplace_back("platform:poll_events"); }
        [[nodiscard]] bool ShouldClose() const override
        {
            m_Trace.emplace_back("platform:should_close");
            return CloseRequested;
        }
        [[nodiscard]] bool IsMinimized() const override
        {
            m_Trace.emplace_back("platform:is_minimized");
            return Minimized;
        }
        void WaitForEventsTimeout(double seconds) override
        {
            WaitSeconds = seconds;
            m_Trace.emplace_back("platform:wait_for_events_timeout");
        }

        bool CloseRequested{false};
        bool Minimized{false};
        double WaitSeconds{0.0};

    private:
        Trace& m_Trace;
    };

    class FakeRendererHooks final : public Extrinsic::Runtime::IRuntimeRenderFrameHooks
    {
    public:
        explicit FakeRendererHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        [[nodiscard]] bool BeginFrame() override
        {
            m_Trace.emplace_back("renderer:begin_frame");
            return BeginFrameSucceeds;
        }
        void ExtractRenderWorld() override { m_Trace.emplace_back("renderer:extract_render_world"); }
        void PrepareFrame() override { m_Trace.emplace_back("renderer:prepare_frame"); }
        void ExecuteFrame() override { m_Trace.emplace_back("renderer:execute_frame"); }
        [[nodiscard]] std::uint64_t EndFrame() override
        {
            m_Trace.emplace_back("renderer:end_frame");
            return CompletedGpuValue;
        }

        bool BeginFrameSucceeds{true};
        std::uint64_t CompletedGpuValue{42};

    private:
        Trace& m_Trace;
    };

    class FakeTransferHooks final : public Extrinsic::Runtime::IRuntimeTransferFrameHooks
    {
    public:
        explicit FakeTransferHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        void CollectCompletedTransfers() override { m_Trace.emplace_back("transfer:collect_completed"); }

    private:
        Trace& m_Trace;
    };

    class FakeAssetHooks final : public Extrinsic::Runtime::IRuntimeAssetFrameHooks
    {
    public:
        explicit FakeAssetHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        void TickAssets() override { m_Trace.emplace_back("assets:tick"); }

    private:
        Trace& m_Trace;
    };

    class FakeStreamingHooks final : public Extrinsic::Runtime::IRuntimeStreamingFrameHooks
    {
    public:
        explicit FakeStreamingHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        void DrainCompletions() override { m_Trace.emplace_back("streaming:drain_completions"); }
        void ApplyMainThreadResults() override { m_Trace.emplace_back("streaming:apply_main_thread_results"); }
        void SubmitFrameWork() override { m_Trace.emplace_back("streaming:submit_frame_work"); }
        void PumpBackground(std::uint32_t maxLaunches) override
        {
            PumpLaunches = maxLaunches;
            m_Trace.emplace_back("streaming:pump_background");
        }

        std::uint32_t PumpLaunches{0};

    private:
        Trace& m_Trace;
    };

    class FakeShutdownHooks final : public Extrinsic::Runtime::IRuntimeShutdownHooks
    {
    public:
        explicit FakeShutdownHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        void StopRunning() override { m_Trace.emplace_back("shutdown:stop_running"); }
        void WaitDeviceIdle() override { m_Trace.emplace_back("shutdown:wait_device_idle"); }
        void ShutdownApplication() override { m_Trace.emplace_back("shutdown:application"); }
        void ShutdownStreaming() override { m_Trace.emplace_back("shutdown:streaming_shutdown_and_drain"); }
        void DestroyScene() override { m_Trace.emplace_back("shutdown:destroy_scene"); }
        void DestroyAssets() override { m_Trace.emplace_back("shutdown:destroy_assets"); }
        void DestroyStreamingState() override { m_Trace.emplace_back("shutdown:destroy_streaming_state"); }
        void DestroyFrameGraph() override { m_Trace.emplace_back("shutdown:destroy_frame_graph"); }
        void ShutdownRenderer() override { m_Trace.emplace_back("shutdown:renderer"); }
        void ShutdownDevice() override { m_Trace.emplace_back("shutdown:device"); }
        void DestroyWindow() override { m_Trace.emplace_back("shutdown:destroy_window"); }
        void ShutdownScheduler() override { m_Trace.emplace_back("shutdown:scheduler"); }
        void MarkUninitialized() override { m_Trace.emplace_back("shutdown:mark_uninitialized"); }

    private:
        Trace& m_Trace;
    };
}

TEST(RuntimeFrameLoopContract, PlatformBeginFramePollsBeforeMinimizedWait)
{
    Trace trace;
    FakePlatformHooks platform(trace);
    platform.Minimized = true;

    const Extrinsic::Runtime::RuntimePlatformFrameResult result =
        Extrinsic::Runtime::ExecuteRuntimePlatformBeginFrameContract(platform, 0.25);

    EXPECT_FALSE(result.ContinueFrame);
    EXPECT_FALSE(result.ShouldClose);
    EXPECT_TRUE(result.Minimized);
    EXPECT_DOUBLE_EQ(platform.WaitSeconds, 0.25);
    EXPECT_EQ(trace, (Trace{
                         "platform:poll_events",
                         "platform:should_close",
                         "platform:is_minimized",
                         "platform:wait_for_events_timeout",
                     }));
}

TEST(RuntimeFrameLoopContract, RenderFrameOrdersPromotedRendererPhases)
{
    Trace trace;
    FakeRendererHooks renderer(trace);
    renderer.CompletedGpuValue = 99;

    const Extrinsic::Runtime::RuntimeRenderFrameResult result =
        Extrinsic::Runtime::ExecuteRuntimeRenderFrameContract(renderer);

    EXPECT_TRUE(result.BeganFrame);
    EXPECT_TRUE(result.CompletedFrame);
    EXPECT_EQ(result.CompletedGpuValue, 99u);
    EXPECT_EQ(trace, (Trace{
                         "renderer:begin_frame",
                         "renderer:extract_render_world",
                         "renderer:prepare_frame",
                         "renderer:execute_frame",
                         "renderer:end_frame",
                     }));
}

TEST(RuntimeFrameLoopContract, RenderFrameSkipsExtractionWhenBeginFrameFails)
{
    Trace trace;
    FakeRendererHooks renderer(trace);
    renderer.BeginFrameSucceeds = false;

    const Extrinsic::Runtime::RuntimeRenderFrameResult result =
        Extrinsic::Runtime::ExecuteRuntimeRenderFrameContract(renderer);

    EXPECT_FALSE(result.BeganFrame);
    EXPECT_FALSE(result.CompletedFrame);
    EXPECT_EQ(result.CompletedGpuValue, 0u);
    EXPECT_EQ(trace, Trace{"renderer:begin_frame"});
}

TEST(RuntimeFrameLoopContract, MaintenanceOrdersTransferStreamingAssetHooks)
{
    Trace trace;
    FakeTransferHooks transfer(trace);
    FakeStreamingHooks streaming(trace);
    FakeAssetHooks assets(trace);

    Extrinsic::Runtime::ExecuteRuntimeMaintenanceContract(transfer, streaming, assets, 8);

    EXPECT_EQ(streaming.PumpLaunches, 8u);
    EXPECT_EQ(trace, (Trace{
                         "transfer:collect_completed",
                         "streaming:drain_completions",
                         "streaming:apply_main_thread_results",
                         "assets:tick",
                         "streaming:submit_frame_work",
                         "streaming:pump_background",
                     }));
}

TEST(RuntimeFrameLoopContract, ShutdownOrdersApplicationStreamingAndSubsystemTeardown)
{
    Trace trace;
    FakeShutdownHooks shutdown(trace);

    Extrinsic::Runtime::ExecuteRuntimeShutdownContract(shutdown);

    EXPECT_EQ(trace, (Trace{
                         "shutdown:stop_running",
                         "shutdown:wait_device_idle",
                         "shutdown:application",
                         "shutdown:streaming_shutdown_and_drain",
                         "shutdown:destroy_scene",
                         "shutdown:destroy_assets",
                         "shutdown:destroy_streaming_state",
                         "shutdown:destroy_frame_graph",
                         "shutdown:renderer",
                         "shutdown:device",
                         "shutdown:destroy_window",
                         "shutdown:scheduler",
                         "shutdown:mark_uninitialized",
                     }));
}

