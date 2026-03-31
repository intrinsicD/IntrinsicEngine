module;
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>

export module Runtime.FrameLoop;

import Core.Assets;
import Core.FeatureRegistry;
import Core.FrameGraph;
import Core.InplaceFunction;
import Core.Window;
import Runtime.RenderExtraction;
import Runtime.AssetIngestService;
import Runtime.AssetPipeline;
import Runtime.GraphicsBackend;
import Runtime.RenderOrchestrator;
import Runtime.ResourceMaintenance;
import Runtime.SceneManager;

export namespace Runtime
{
    inline constexpr double DefaultFixedStepHz = 60.0;
    inline constexpr double HighFrequencyFixedStepHz = 120.0;
    inline constexpr double DefaultMaxFrameDeltaSeconds = 0.25;
    inline constexpr int DefaultMaxSubstepsPerFrame = 8;

    // ---------------------------------------------------------------------------
    // Frame Pacing — activity-aware idle throttling
    // ---------------------------------------------------------------------------

    // Configures how the engine throttles CPU usage based on user activity.
    // When no input, scene mutations, or async completions occur for a sustained
    // period, the frame rate drops to IdleFps to conserve CPU/GPU resources.
    // Any activity instantly restores the active rate.
    struct FramePacingConfig
    {
        double ActiveFps = 60.0;         // Target FPS when the user is interacting (0 = VSync-limited only)
        double IdleFps = 15.0;           // Target FPS when idle (0 = disabled, use ActiveFps always)
        double IdleTimeoutSeconds = 2.0; // Seconds of inactivity before entering idle pacing
        bool   Enabled = true;           // Master switch for idle throttling
    };

    // Tracks frame-loop activity to drive idle throttling.
    // Call NoteActivity() from any signal source (input events, scene mutations,
    // async completions, window resize). The coordinator queries
    // EffectiveFpsCap() each frame to select the active or idle rate.
    struct ActivityTracker
    {
        FramePacingConfig Config{};

        // Signal that user/scene activity occurred this frame.
        void NoteActivity() &;

        // Returns the effective FPS cap for the current frame, considering
        // idle timeout and configuration.
        [[nodiscard]] double EffectiveFpsCap(double currentTimeSeconds) const &;

        // Returns true when the tracker is in idle pacing mode.
        [[nodiscard]] bool IsIdle(double currentTimeSeconds) const &;

        // Reset to active state (e.g., on window focus).
        void Reset() &;

    private:
        double m_LastActivityTimeSeconds = 0.0;
        bool   m_HasActivity = false;
    };

    enum class FrameLoopMode : uint8_t
    {
        LegacyCompatibility = 0,
        StagedPhases,
    };

    namespace FrameLoopFeatureCatalog
    {
        inline constexpr Core::FeatureDescriptor StagedPhases = Core::MakeFeatureDescriptor(
            "FrameLoop.StagedPhases",
            Core::FeatureCategory::System,
            "Runs the staged frame loop coordinators for streaming, fixed-step, and render lanes.");

        inline constexpr Core::FeatureDescriptor LegacyCompatibility = Core::MakeFeatureDescriptor(
            "FrameLoop.LegacyCompatibility",
            Core::FeatureCategory::System,
            "Rollback shim that preserves the current frame-order contract through a legacy-compatible adapter.",
            false);
    }

    [[nodiscard]] FrameLoopMode ResolveFrameLoopMode(const Core::FeatureRegistry& features);
    [[nodiscard]] const char* ToString(FrameLoopMode mode);

    struct FrameLoopPolicy
    {
        double FixedDt = 1.0 / DefaultFixedStepHz;
        double MaxFrameDelta = DefaultMaxFrameDeltaSeconds;
        int MaxSubstepsPerFrame = DefaultMaxSubstepsPerFrame;
    };

    [[nodiscard]] FrameLoopPolicy MakeFrameLoopPolicy(double fixedStepHz = DefaultFixedStepHz,
                                                      double maxFrameDeltaSeconds = DefaultMaxFrameDeltaSeconds,
                                                      int maxSubstepsPerFrame = DefaultMaxSubstepsPerFrame);

    struct FrameTimeStep
    {
        double FrameTime = 0.0;
        bool Clamped = false;
    };

    [[nodiscard]] FrameTimeStep ComputeFrameTime(double rawFrameTime,
                                                 const FrameLoopPolicy& policy = {});
    [[nodiscard]] double ComputeRenderInterpolationAlpha(double accumulator,
                                                         const FrameLoopPolicy& policy = {});

    class FrameClock
    {
    public:
        void Reset();
        [[nodiscard]] FrameTimeStep Advance(const FrameLoopPolicy& policy) &;

        // Re-anchor the last-sample timestamp to now without computing a frame
        // delta. Call after deliberate sleeps so the next Advance() does not
        // count the sleep duration as part of the following frame's time.
        void Resample() &;

    private:
        double m_LastSampleSeconds = 0.0;
        bool m_HasLastSample = false;
    };

    struct FixedStepAdvanceResult
    {
        int ExecutedSubsteps = 0;
        bool AccumulatorClamped = false;
        uint64_t CpuTimeNs = 0;
    };

    using ExecuteGraphFn = Core::InplaceFunction<void(Core::FrameGraph&), 96>;
    using FixedUpdateFn = Core::InplaceFunction<void(float), 64>;
    using RegisterFixedSystemsFn = Core::InplaceFunction<void(Core::FrameGraph&, float), 96>;
    using CommitFixedTickFn = Core::InplaceFunction<void(), 64>;
    using VariableUpdateFn = Core::InplaceFunction<void(float), 64>;
    using RegisterVariableSystemsFn = Core::InplaceFunction<void(Core::FrameGraph&, float), 96>;
    using PreDispatchFn = Core::InplaceFunction<void(), 64>;
    using RenderHookFn = Core::InplaceFunction<void(double), 64>;

    [[nodiscard]] FixedStepAdvanceResult RunFixedSteps(double& accumulator,
                                                       const FrameLoopPolicy& policy,
                                                       FixedUpdateFn&& onFixedUpdate,
                                                       RegisterFixedSystemsFn&& registerFixedSystems,
                                                       CommitFixedTickFn&& commitFixedTick,
                                                       Core::FrameGraph& fixedGraph,
                                                       ExecuteGraphFn&& executeGraph);

    struct FrameGraphTimingTotals
    {
        uint64_t CompileNsTotal = 0;
        uint64_t ExecuteNsTotal = 0;
        uint64_t CriticalPathNsTotal = 0;
    };

    struct FrameGraphExecutor
    {
        Core::Assets::AssetManager& AssetManager;
        FrameGraphTimingTotals& Timings;

        void Execute(this const FrameGraphExecutor&, Core::FrameGraph& graph);
    };

    class IPlatformFrameHost
    {
    public:
        virtual ~IPlatformFrameHost();

        virtual void PumpEvents() = 0;
        [[nodiscard]] virtual bool ShouldQuit() const = 0;
        [[nodiscard]] virtual bool IsMinimized() const = 0;
        virtual void WaitForEventsOrTimeout(double timeoutSeconds) = 0;
        [[nodiscard]] virtual int GetFramebufferWidth() const = 0;
        [[nodiscard]] virtual int GetFramebufferHeight() const = 0;
        virtual bool HasResizeRequest() const = 0;
        virtual bool ConsumeResizeRequest() = 0;

        // Returns true if any user input events occurred during the last
        // PumpEvents() call, then clears the flag. Used by the activity
        // tracker to detect idle state.
        [[nodiscard]] virtual bool ConsumeInputActivity() { return false; }
    };

    class RuntimePlatformFrameHost final : public IPlatformFrameHost
    {
    public:
        RuntimePlatformFrameHost(Core::Windowing::Window& window, bool& resizeRequested)
            : m_Window(window)
            , m_ResizeRequested(resizeRequested)
        {
        }

        ~RuntimePlatformFrameHost() override;

        void PumpEvents() override;
        [[nodiscard]] bool ShouldQuit() const override;
        [[nodiscard]] bool IsMinimized() const override;
        void WaitForEventsOrTimeout(double timeoutSeconds) override;
        [[nodiscard]] int GetFramebufferWidth() const override;
        [[nodiscard]] int GetFramebufferHeight() const override;
        bool HasResizeRequest() const override;
        bool ConsumeResizeRequest() override;
        [[nodiscard]] bool ConsumeInputActivity() override;

    private:
        Core::Windowing::Window& m_Window;
        bool& m_ResizeRequested;
    };

    struct PlatformFrameResult
    {
        bool ContinueFrame = true;
        bool Minimized = false;
        bool ShouldQuit = false;
        bool ThreadViolation = false;
        bool ResizeRequested = false;
        int FramebufferWidth = 0;
        int FramebufferHeight = 0;
        FrameTimeStep FrameStep{};
    };

    struct PlatformFrameCoordinator
    {
        IPlatformFrameHost& Host;
        FrameClock& Clock;
        double MinimizedWaitSeconds = 0.05;
        // Activity-aware frame pacing. When non-null, EffectiveFpsCap()
        // drives sleep-based throttling each frame. When null, no CPU-side
        // cap is applied (VSync or GPU back-pressure is the sole limiter).
        ActivityTracker* Activity = nullptr;
        Core::InplaceFunction<void(double), 64> SleepForSeconds =
            [](double seconds)
        {
            if (seconds <= 0.0)
                return;
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        };
        std::thread::id OwningThread = std::this_thread::get_id();

        [[nodiscard]] PlatformFrameResult BeginFrame(this PlatformFrameCoordinator&,
                                                    const FrameLoopPolicy& policy);
    };

    struct FramebufferExtent
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    class IResizeSyncHost
    {
    public:
        virtual ~IResizeSyncHost();

        [[nodiscard]] virtual FramebufferExtent GetSwapchainExtent() const = 0;
        virtual void ApplyResize() = 0;
    };

    class RuntimeResizeSyncHost final : public IResizeSyncHost
    {
    public:
        RuntimeResizeSyncHost(GraphicsBackend& graphics, RenderOrchestrator& renderer)
            : m_Graphics(graphics)
            , m_Renderer(renderer)
        {
        }

        ~RuntimeResizeSyncHost() override;

        [[nodiscard]] FramebufferExtent GetSwapchainExtent() const override;
        void ApplyResize() override;

    private:
        GraphicsBackend& m_Graphics;
        RenderOrchestrator& m_Renderer;
    };

    struct ResizeSyncResult
    {
        FramebufferExtent SwapchainExtentBefore{};
        bool ResizeRequested = false;
        bool FramebufferExtentMismatch = false;
        bool ResizeApplied = false;
    };

    struct ResizeSyncCoordinator
    {
        IResizeSyncHost& Host;

        [[nodiscard]] ResizeSyncResult Sync(const PlatformFrameResult& platformFrame) const;
    };

    class IStreamingLaneHost
    {
    public:
        virtual ~IStreamingLaneHost();

        virtual void ProcessAssetIngest() = 0;
        virtual void ProcessMainThreadQueue() = 0;
        virtual void ProcessUploads() = 0;
    };

    class RuntimeStreamingLaneHost final : public IStreamingLaneHost
    {
    public:
        RuntimeStreamingLaneHost(AssetIngestService* ingest,
                                 AssetPipeline& assets)
            : m_Ingest(ingest)
            , m_Assets(assets)
        {
        }

        ~RuntimeStreamingLaneHost() override;

        void ProcessAssetIngest() override;
        void ProcessMainThreadQueue() override;
        void ProcessUploads() override;

    private:
        AssetIngestService* m_Ingest = nullptr;
        AssetPipeline& m_Assets;
    };

    struct StreamingLaneCoordinator
    {
        IStreamingLaneHost& Host;

        void BeginFrame(this const StreamingLaneCoordinator&);
    };

    class IMaintenanceLaneHost
    {
    public:
        virtual ~IMaintenanceLaneHost();

        virtual void CaptureGpuSyncState() = 0;
        virtual void ProcessCompletedReadbacks() = 0;
        virtual void CollectGpuDeferredDestructions() = 0;
        virtual void GarbageCollectTransfers() = 0;
        virtual void ProcessTextureDeletions() = 0;
        virtual void ProcessMaterialDeletions() = 0;
        virtual void CaptureFrameTelemetry(const FrameTelemetrySnapshot& snapshot) = 0;
        virtual void BookkeepHotReloads() = 0;
    };

    class RuntimeMaintenanceLaneHost final : public IMaintenanceLaneHost
    {
    public:
        RuntimeMaintenanceLaneHost(SceneManager& scene, RenderOrchestrator& renderer, GraphicsBackend& graphics,
                                   Core::FeatureRegistry& features)
            : m_Maintenance(scene, renderer, graphics, features)
        {
        }

        ~RuntimeMaintenanceLaneHost() override;

        void CaptureGpuSyncState() override;
        void ProcessCompletedReadbacks() override;
        void CollectGpuDeferredDestructions() override;
        void GarbageCollectTransfers() override;
        void ProcessTextureDeletions() override;
        void ProcessMaterialDeletions() override;
        void CaptureFrameTelemetry(const FrameTelemetrySnapshot& snapshot) override;
        void BookkeepHotReloads() override;

    private:
        ResourceMaintenanceService m_Maintenance;
    };

    struct MaintenanceLaneCoordinator
    {
        IMaintenanceLaneHost& Host;

        void Run(this const MaintenanceLaneCoordinator&, const FrameTelemetrySnapshot& telemetry);
    };

    struct RenderLaneCallbacks
    {
        VariableUpdateFn OnUpdate;
        RegisterVariableSystemsFn RegisterVariableSystems;
        PreDispatchFn BeforeDispatch;
        RenderHookFn OnRender;
    };

    class IRenderLaneHost
    {
    public:
        virtual ~IRenderLaneHost();

        [[nodiscard]] virtual Core::FrameGraph& GetFrameGraph() = 0;
        virtual void RegisterEngineSystems(Core::FrameGraph& graph) = 0;
        virtual void DispatchDeferredEvents() = 0;
        [[nodiscard]] virtual std::optional<RenderWorld> ExtractRenderWorld(double alpha) = 0;
        virtual void ExecutePreparedFrame(RenderWorld renderWorld) = 0;
    };

    class RuntimeRenderLaneHost final : public IRenderLaneHost
    {
    public:
        RuntimeRenderLaneHost(SceneManager& scene,
                              RenderOrchestrator& renderer,
                              GraphicsBackend& graphics,
                              Core::FeatureRegistry& features,
                              Core::Assets::AssetManager& assets)
            : m_Scene(scene)
            , m_Renderer(renderer)
            , m_Graphics(graphics)
            , m_Features(features)
            , m_Assets(assets)
        {
        }

        ~RuntimeRenderLaneHost() override;

        [[nodiscard]] Core::FrameGraph& GetFrameGraph() override;
        void RegisterEngineSystems(Core::FrameGraph& graph) override;
        void DispatchDeferredEvents() override;
        [[nodiscard]] std::optional<RenderWorld> ExtractRenderWorld(double alpha) override;
        void ExecutePreparedFrame(RenderWorld renderWorld) override;

    private:
        SceneManager& m_Scene;
        RenderOrchestrator& m_Renderer;
        GraphicsBackend& m_Graphics;
        Core::FeatureRegistry& m_Features;
        Core::Assets::AssetManager& m_Assets;
    };

    struct RenderLaneCoordinator
    {
        IRenderLaneHost& Host;

        void Run(this const RenderLaneCoordinator&,
                 double frameTime,
                 double alpha,
                 RenderLaneCallbacks&& callbacks,
                 ExecuteGraphFn&& executeGraph);
    };

    struct FramePhaseCallbacks
    {
        FixedUpdateFn OnFixedUpdate;
        RegisterFixedSystemsFn RegisterFixedSystems;
        CommitFixedTickFn CommitFixedTick;
        ExecuteGraphFn ExecuteFixedGraph;
        RenderLaneCallbacks Render;
        ExecuteGraphFn ExecuteVariableGraph;
        FrameGraphTimingTotals* Timings = nullptr;
    };

    struct FramePhaseRunResult
    {
        FixedStepAdvanceResult FixedStep;
        FrameLoopMode Mode = FrameLoopMode::StagedPhases;
    };

    [[nodiscard]] FramePhaseRunResult RunFramePhases(double frameTime,
                                                     double& accumulator,
                                                     const FrameLoopPolicy& policy,
                                                     const StreamingLaneCoordinator& streamingLane,
                                                     const MaintenanceLaneCoordinator& maintenanceLane,
                                                     const RenderLaneCoordinator& renderLane,
                                                     Core::FrameGraph& fixedGraph,
                                                     FramePhaseCallbacks&& callbacks);

    [[nodiscard]] FramePhaseRunResult RunFramePhasesForMode(FrameLoopMode mode,
                                                            double frameTime,
                                                            double& accumulator,
                                                            const FrameLoopPolicy& policy,
                                                            const StreamingLaneCoordinator& streamingLane,
                                                            const MaintenanceLaneCoordinator& maintenanceLane,
                                                            const RenderLaneCoordinator& renderLane,
                                                            Core::FrameGraph& fixedGraph,
                                                            FramePhaseCallbacks&& callbacks);
}
