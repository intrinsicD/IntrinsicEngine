module;

#include <cstdint>

export module Extrinsic.Core.FrameLoop;

namespace Extrinsic::Core
{
    export enum class RenderFramePhase : std::uint8_t
    {
        BeginFrame,
        ExtractRenderWorld,
        PrepareFrame,
        ExecuteFrame,
        EndFrame,
    };

    export struct RenderFrameResult
    {
        bool BeganFrame{false};
        bool CompletedFrame{false};
        std::uint64_t CompletedGpuValue{0};
    };

    export class IRenderFrameHooks
    {
    public:
        virtual ~IRenderFrameHooks() = default;

        [[nodiscard]] virtual bool BeginFrame() = 0;
        virtual void ExtractRenderWorld() = 0;
        virtual void PrepareFrame() = 0;
        virtual void ExecuteFrame() = 0;
        [[nodiscard]] virtual std::uint64_t EndFrame() = 0;
    };

    export [[nodiscard]] RenderFrameResult ExecuteRenderFrameContract(
        IRenderFrameHooks& hooks);

    export class IPlatformFrameHooks
    {
    public:
        virtual ~IPlatformFrameHooks() = default;

        virtual void PollEvents() = 0;
        [[nodiscard]] virtual bool ShouldClose() const = 0;
        [[nodiscard]] virtual bool IsMinimized() const = 0;
        virtual void WaitForEventsTimeout(double seconds) = 0;
    };

    export struct PlatformFrameResult
    {
        bool ContinueFrame{false};
        bool ShouldClose{false};
        bool Minimized{false};
    };

    export [[nodiscard]] PlatformFrameResult ExecutePlatformBeginFrameContract(
        IPlatformFrameHooks& hooks,
        double minimizedWaitSeconds);

    export class ITransferFrameHooks
    {
    public:
        virtual ~ITransferFrameHooks() = default;

        virtual void CollectCompletedTransfers() = 0;
    };

    export class IAssetFrameHooks
    {
    public:
        virtual ~IAssetFrameHooks() = default;

        virtual void TickAssets() = 0;
    };

    export void ExecuteMaintenanceContract(
        ITransferFrameHooks& transfer,
        IAssetFrameHooks& assets);

    export class IOperationalTransitionHooks
    {
    public:
        virtual ~IOperationalTransitionHooks() = default;

        [[nodiscard]] virtual bool IsDeviceOperational() const = 0;
        [[nodiscard]] virtual bool IsRendererOperational() const = 0;
        virtual void WaitDeviceIdle() = 0;
        [[nodiscard]] virtual bool RebuildRendererOperationalResources() = 0;
        virtual void MarkRendererOperational() = 0;
    };

    export [[nodiscard]] bool ExecuteOperationalTransitionContract(
        IOperationalTransitionHooks& hooks);

    export class IShutdownHooks
    {
    public:
        virtual ~IShutdownHooks() = default;

        virtual void ShutdownRuntimeModules() = 0;
        virtual void DestroyScene() = 0;
        virtual void DestroyFrameGraph() = 0;
        virtual void ShutdownRenderer() = 0;
        virtual void ShutdownDevice() = 0;
        virtual void DestroyWindow() = 0;
        virtual void ShutdownScheduler() = 0;
        virtual void MarkUninitialized() = 0;
    };

    export void ExecuteShutdownContract(IShutdownHooks& hooks);
}
