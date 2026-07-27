module;

#include <cstdint>

module Extrinsic.Core.FrameLoop;

namespace Extrinsic::Core
{
    RenderFrameResult ExecuteRenderFrameContract(IRenderFrameHooks& hooks)
    {
        RenderFrameResult result{};
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

    PlatformFrameResult ExecutePlatformBeginFrameContract(
        IPlatformFrameHooks& hooks,
        double minimizedWaitSeconds)
    {
        hooks.PollEvents();

        PlatformFrameResult result{};
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

    void ExecuteMaintenanceContract(
        ITransferFrameHooks& transfer,
        IAssetFrameHooks& assets)
    {
        transfer.CollectCompletedTransfers();
        assets.TickAssets();
    }

    bool ExecuteOperationalTransitionContract(IOperationalTransitionHooks& hooks)
    {
        if (!hooks.IsDeviceOperational() || hooks.IsRendererOperational())
        {
            return false;
        }

        hooks.WaitDeviceIdle();
        if (!hooks.RebuildRendererOperationalResources())
        {
            return false;
        }

        hooks.MarkRendererOperational();
        return true;
    }

    void ExecuteShutdownContract(IShutdownHooks& hooks)
    {
        hooks.ShutdownRuntimeModules();
        hooks.DestroyScene();
        hooks.DestroyFrameGraph();
        hooks.ShutdownRenderer();
        hooks.ShutdownDevice();
        hooks.DestroyWindow();
        hooks.ShutdownScheduler();
        hooks.MarkUninitialized();
    }
}
