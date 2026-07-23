#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

import Extrinsic.Core.FrameLoop;

namespace
{
    using Trace = std::vector<std::string>;

    class FakePlatformHooks final : public Extrinsic::Core::IPlatformFrameHooks
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

    class FakeRendererHooks final : public Extrinsic::Core::IRenderFrameHooks
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

    class FakeTransferHooks final : public Extrinsic::Core::ITransferFrameHooks
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

    class FakeAssetHooks final : public Extrinsic::Core::IAssetFrameHooks
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

    class FakeStreamingHooks final : public Extrinsic::Core::IStreamingFrameHooks
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

    class FakeOperationalTransitionHooks final : public Extrinsic::Core::IOperationalTransitionHooks
    {
    public:
        explicit FakeOperationalTransitionHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

        [[nodiscard]] bool IsDeviceOperational() const override
        {
            m_Trace.emplace_back("operational:device_query");
            return DeviceOperational;
        }
        [[nodiscard]] bool IsRendererOperational() const override
        {
            m_Trace.emplace_back("operational:renderer_query");
            return RendererOperational;
        }
        void WaitDeviceIdle() override { m_Trace.emplace_back("operational:wait_idle"); }
        [[nodiscard]] bool RebuildRendererOperationalResources() override
        {
            m_Trace.emplace_back("operational:renderer_rebuild");
            return RebuildSucceeds;
        }
        void MarkRendererOperational() override
        {
            RendererOperational = true;
            m_Trace.emplace_back("operational:mark_renderer_operational");
        }

        bool DeviceOperational{false};
        bool RendererOperational{false};
        bool RebuildSucceeds{true};

    private:
        Trace& m_Trace;
    };

    class FakeShutdownHooks final : public Extrinsic::Core::IShutdownHooks
    {
    public:
        explicit FakeShutdownHooks(Trace& trace)
            : m_Trace(trace)
        {
        }

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

    const Extrinsic::Core::PlatformFrameResult result =
        Extrinsic::Core::ExecutePlatformBeginFrameContract(platform, 0.25);

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

TEST(RuntimeFrameLoopContract, PlatformBeginFrameStopsWhenPollRequestsClose)
{
    Trace trace;
    FakePlatformHooks platform(trace);
    platform.CloseRequested = true;

    const Extrinsic::Core::PlatformFrameResult result =
        Extrinsic::Core::ExecutePlatformBeginFrameContract(platform, 0.25);

    EXPECT_FALSE(result.ContinueFrame);
    EXPECT_TRUE(result.ShouldClose);
    EXPECT_FALSE(result.Minimized);
    EXPECT_EQ(trace, (Trace{
                         "platform:poll_events",
                         "platform:should_close",
                     }));
}

TEST(RuntimeFrameLoopContract, RenderFrameOrdersPromotedRendererPhases)
{
    Trace trace;
    FakeRendererHooks renderer(trace);
    renderer.CompletedGpuValue = 99;

    const Extrinsic::Core::RenderFrameResult result =
        Extrinsic::Core::ExecuteRenderFrameContract(renderer);

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

    const Extrinsic::Core::RenderFrameResult result =
        Extrinsic::Core::ExecuteRenderFrameContract(renderer);

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

    Extrinsic::Core::ExecuteMaintenanceContract(transfer, streaming, assets, 8);

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

TEST(RuntimeFrameLoopContract, OperationalTransitionWaitsIdleThenRebuildsRendererOnce)
{
    Trace trace;
    FakeOperationalTransitionHooks hooks(trace);
    hooks.DeviceOperational = true;

    const bool transitioned = Extrinsic::Core::ExecuteOperationalTransitionContract(hooks);

    EXPECT_TRUE(transitioned);
    EXPECT_TRUE(hooks.RendererOperational);
    EXPECT_EQ(trace, (Trace{
                         "operational:device_query",
                         "operational:renderer_query",
                         "operational:wait_idle",
                         "operational:renderer_rebuild",
                         "operational:mark_renderer_operational",
                     }));
}

TEST(RuntimeFrameLoopContract, OperationalTransitionNoOpsUntilDeviceBecomesOperational)
{
    Trace trace;
    FakeOperationalTransitionHooks hooks(trace);

    const bool transitioned = Extrinsic::Core::ExecuteOperationalTransitionContract(hooks);

    EXPECT_FALSE(transitioned);
    EXPECT_FALSE(hooks.RendererOperational);
    EXPECT_EQ(trace, Trace{"operational:device_query"});
}

TEST(RuntimeFrameLoopContract, OperationalTransitionDoesNotMarkRendererWhenRebuildFails)
{
    Trace trace;
    FakeOperationalTransitionHooks hooks(trace);
    hooks.DeviceOperational = true;
    hooks.RebuildSucceeds = false;

    const bool transitioned = Extrinsic::Core::ExecuteOperationalTransitionContract(hooks);

    EXPECT_FALSE(transitioned);
    EXPECT_FALSE(hooks.RendererOperational);
    EXPECT_EQ(trace, (Trace{
                         "operational:device_query",
                         "operational:renderer_query",
                         "operational:wait_idle",
                         "operational:renderer_rebuild",
                     }));
}

TEST(RuntimeFrameLoopContract, ShutdownOrdersStreamingAndSubsystemTeardown)
{
    Trace trace;
    FakeShutdownHooks shutdown(trace);

    Extrinsic::Core::ExecuteShutdownContract(shutdown);

    EXPECT_EQ(trace, (Trace{
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
