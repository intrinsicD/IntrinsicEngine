module;
#include <cstdint>
#include <chrono>

export module Runtime.FrameLoop;

import Core.Assets;
import Core.FeatureRegistry;
import Core.FrameGraph;
import Core.InplaceFunction;
import Core.Window;
import Graphics.MaterialSystem;
import Runtime.AssetIngestService;
import Runtime.AssetPipeline;
import Runtime.GraphicsBackend;
import Runtime.RenderOrchestrator;
import Runtime.SceneManager;

export namespace Runtime
{
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
        double FixedDt = 1.0 / 60.0;
        double MaxFrameDelta = 0.25;
        int MaxSubstepsPerFrame = 8;
    };

    struct FrameTimeStep
    {
        double FrameTime = 0.0;
        bool Clamped = false;
    };

    [[nodiscard]] FrameTimeStep ComputeFrameTime(double rawFrameTime,
                                                 const FrameLoopPolicy& policy = {});

    struct FixedStepAdvanceResult
    {
        int ExecutedSubsteps = 0;
        bool AccumulatorClamped = false;
        uint64_t CpuTimeNs = 0;
    };

    using ExecuteGraphFn = Core::InplaceFunction<void(Core::FrameGraph&), 96>;
    using FixedUpdateFn = Core::InplaceFunction<void(float), 64>;
    using RegisterFixedSystemsFn = Core::InplaceFunction<void(Core::FrameGraph&, float), 96>;
    using VariableUpdateFn = Core::InplaceFunction<void(float), 64>;
    using RegisterVariableSystemsFn = Core::InplaceFunction<void(Core::FrameGraph&, float), 96>;
    using PreDispatchFn = Core::InplaceFunction<void(), 64>;
    using RenderHookFn = Core::InplaceFunction<void(), 64>;

    [[nodiscard]] FixedStepAdvanceResult RunFixedSteps(double& accumulator,
                                                       const FrameLoopPolicy& policy,
                                                       FixedUpdateFn&& onFixedUpdate,
                                                       RegisterFixedSystemsFn&& registerFixedSystems,
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
        [[nodiscard]] virtual bool IsMinimized() const = 0;
        virtual void WaitForEventsOrTimeout(double timeoutSeconds) = 0;
    };

    class RuntimePlatformFrameHost final : public IPlatformFrameHost
    {
    public:
        explicit RuntimePlatformFrameHost(Core::Windowing::Window& window)
            : m_Window(window)
        {
        }

        ~RuntimePlatformFrameHost() override;

        void PumpEvents() override;
        [[nodiscard]] bool IsMinimized() const override;
        void WaitForEventsOrTimeout(double timeoutSeconds) override;

    private:
        Core::Windowing::Window& m_Window;
    };

    struct PlatformFrameResult
    {
        bool ContinueFrame = true;
        bool Minimized = false;
    };

    struct PlatformFrameCoordinator
    {
        IPlatformFrameHost& Host;
        double MinimizedWaitSeconds = 0.05;

        [[nodiscard]] PlatformFrameResult BeginFrame(this const PlatformFrameCoordinator&);
    };

    class IStreamingLaneHost
    {
    public:
        virtual ~IStreamingLaneHost();

        virtual void ProcessAssetIngest() = 0;
        virtual void ProcessMainThreadQueue() = 0;
        virtual void ProcessUploads() = 0;
        virtual void ProcessTextureDeletions() = 0;
        virtual void ProcessMaterialDeletions() = 0;
        virtual void GarbageCollectTransfers() = 0;
    };

    class RuntimeStreamingLaneHost final : public IStreamingLaneHost
    {
    public:
        RuntimeStreamingLaneHost(AssetIngestService* ingest,
                                 AssetPipeline& assets,
                                 GraphicsBackend& graphics,
                                 Graphics::MaterialSystem& materials)
            : m_Ingest(ingest)
            , m_Assets(assets)
            , m_Graphics(graphics)
            , m_Materials(materials)
        {
        }

        ~RuntimeStreamingLaneHost() override;

        void ProcessAssetIngest() override;
        void ProcessMainThreadQueue() override;
        void ProcessUploads() override;
        void ProcessTextureDeletions() override;
        void ProcessMaterialDeletions() override;
        void GarbageCollectTransfers() override;

    private:
        AssetIngestService* m_Ingest = nullptr;
        AssetPipeline& m_Assets;
        GraphicsBackend& m_Graphics;
        Graphics::MaterialSystem& m_Materials;
    };

    struct StreamingLaneCoordinator
    {
        IStreamingLaneHost& Host;

        void BeginFrame(this const StreamingLaneCoordinator&);
        void EndFrame(this const StreamingLaneCoordinator&);
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
                 RenderLaneCallbacks&& callbacks,
                 ExecuteGraphFn&& executeGraph);
    };

    struct FramePhaseCallbacks
    {
        FixedUpdateFn OnFixedUpdate;
        RegisterFixedSystemsFn RegisterFixedSystems;
        ExecuteGraphFn ExecuteFixedGraph;
        RenderLaneCallbacks Render;
        ExecuteGraphFn ExecuteVariableGraph;
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
                                                     const RenderLaneCoordinator& renderLane,
                                                     Core::FrameGraph& fixedGraph,
                                                     FramePhaseCallbacks&& callbacks);

    [[nodiscard]] FramePhaseRunResult RunFramePhasesForMode(FrameLoopMode mode,
                                                            double frameTime,
                                                            double& accumulator,
                                                            const FrameLoopPolicy& policy,
                                                            const StreamingLaneCoordinator& streamingLane,
                                                            const RenderLaneCoordinator& renderLane,
                                                            Core::FrameGraph& fixedGraph,
                                                            FramePhaseCallbacks&& callbacks);
}
