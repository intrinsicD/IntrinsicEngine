module;

#include <cstdint>

export module Extrinsic.Runtime.FrameLoop;

namespace Extrinsic::Runtime
{
    export enum class RuntimeRenderFramePhase : std::uint8_t
    {
        BeginFrame,
        ExtractRenderWorld,
        PrepareFrame,
        ExecuteFrame,
        EndFrame,
    };

    export struct RuntimeRenderFrameResult
    {
        bool BeganFrame{false};
        bool CompletedFrame{false};
        std::uint64_t CompletedGpuValue{0};
    };

    export class IRuntimeRenderFrameHooks
    {
    public:
        virtual ~IRuntimeRenderFrameHooks() = default;

        [[nodiscard]] virtual bool BeginFrame() = 0;
        virtual void ExtractRenderWorld() = 0;
        virtual void PrepareFrame() = 0;
        virtual void ExecuteFrame() = 0;
        [[nodiscard]] virtual std::uint64_t EndFrame() = 0;
    };

    export [[nodiscard]] inline RuntimeRenderFrameResult ExecuteRuntimeRenderFrameContract(
        IRuntimeRenderFrameHooks& hooks)
    {
        RuntimeRenderFrameResult result{};
        result.BeganFrame = hooks.BeginFrame();

        if (!result.BeganFrame)
            return result;

        hooks.ExtractRenderWorld();
        hooks.PrepareFrame();
        hooks.ExecuteFrame();
        result.CompletedGpuValue = hooks.EndFrame();
        result.CompletedFrame = true;

        return result;
    }

    export class IRuntimePlatformFrameHooks
    {
    public:
        virtual ~IRuntimePlatformFrameHooks() = default;

        virtual void PollEvents() = 0;
        [[nodiscard]] virtual bool ShouldClose() const = 0;
        [[nodiscard]] virtual bool IsMinimized() const = 0;
        virtual void WaitForEventsTimeout(double seconds) = 0;
    };

    export struct RuntimePlatformFrameResult
    {
        bool ContinueFrame{false};
        bool ShouldClose{false};
        bool Minimized{false};
    };

    export [[nodiscard]] inline RuntimePlatformFrameResult ExecuteRuntimePlatformBeginFrameContract(
        IRuntimePlatformFrameHooks& hooks,
        double minimizedWaitSeconds)
    {
        hooks.PollEvents();

        RuntimePlatformFrameResult result{};
        result.ShouldClose = hooks.ShouldClose();
        if (result.ShouldClose)
            return result;

        result.Minimized = hooks.IsMinimized();
        if (result.Minimized)
        {
            hooks.WaitForEventsTimeout(minimizedWaitSeconds);
            return result;
        }

        result.ContinueFrame = true;
        return result;
    }

    export class IRuntimeTransferFrameHooks
    {
    public:
        virtual ~IRuntimeTransferFrameHooks() = default;

        virtual void CollectCompletedTransfers() = 0;
    };

    export class IRuntimeAssetFrameHooks
    {
    public:
        virtual ~IRuntimeAssetFrameHooks() = default;

        virtual void TickAssets() = 0;
    };

    export class IRuntimeStreamingFrameHooks
    {
    public:
        virtual ~IRuntimeStreamingFrameHooks() = default;

        virtual void DrainCompletions() = 0;
        virtual void ApplyMainThreadResults() = 0;
        virtual void SubmitFrameWork() = 0;
        virtual void PumpBackground(std::uint32_t maxLaunches) = 0;
    };

    export inline void ExecuteRuntimeMaintenanceContract(
        IRuntimeTransferFrameHooks& transfer,
        IRuntimeStreamingFrameHooks& streaming,
        IRuntimeAssetFrameHooks& assets,
        std::uint32_t maxStreamingLaunches)
    {
        transfer.CollectCompletedTransfers();
        streaming.DrainCompletions();
        streaming.ApplyMainThreadResults();
        assets.TickAssets();
        streaming.SubmitFrameWork();
        streaming.PumpBackground(maxStreamingLaunches);
    }

    export class IRuntimeShutdownHooks
    {
    public:
        virtual ~IRuntimeShutdownHooks() = default;

        virtual void StopRunning() = 0;
        virtual void WaitDeviceIdle() = 0;
        virtual void ShutdownApplication() = 0;
        virtual void ShutdownStreaming() = 0;
        virtual void DestroyScene() = 0;
        virtual void DestroyAssets() = 0;
        virtual void DestroyStreamingState() = 0;
        virtual void DestroyFrameGraph() = 0;
        virtual void ShutdownRenderer() = 0;
        virtual void ShutdownDevice() = 0;
        virtual void DestroyWindow() = 0;
        virtual void ShutdownScheduler() = 0;
        virtual void MarkUninitialized() = 0;
    };

    export inline void ExecuteRuntimeShutdownContract(IRuntimeShutdownHooks& hooks)
    {
        hooks.StopRunning();
        hooks.WaitDeviceIdle();
        hooks.ShutdownApplication();
        hooks.ShutdownStreaming();
        hooks.DestroyScene();
        hooks.DestroyAssets();
        hooks.DestroyStreamingState();
        hooks.DestroyFrameGraph();
        hooks.ShutdownRenderer();
        hooks.ShutdownDevice();
        hooks.DestroyWindow();
        hooks.ShutdownScheduler();
        hooks.MarkUninitialized();
    }
}

