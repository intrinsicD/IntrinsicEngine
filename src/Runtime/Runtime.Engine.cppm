module;
#include <memory>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>


#include "tiny_gltf.h"

export module Runtime.Engine;

import Core.Window;
import Core.FrameGraph;
import Core.Assets;
import Core.Memory;
import Core.FeatureRegistry;
import Core.IOBackend;
import Core.Benchmark;
import RHI.Buffer;
#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
#endif
import RHI.Descriptors;
import RHI.Device;
import RHI.Swapchain;
import RHI.Transfer;
import Graphics.Geometry;
import Graphics.IORegistry;
import ECS;
import Runtime.SelectionModule;
import Runtime.FrameLoop;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.AssetIngestService;
import Runtime.SceneManager;
import Runtime.RenderExtraction;
import Runtime.RenderOrchestrator;

export namespace Runtime
{
    struct EngineConfig
    {
        std::string AppName = "Intrinsic App";
        int Width = 1600;
        int Height = 900;
        size_t FrameArenaSize = 1024 * 1024; // Configurable frame arena (default: 1 MB)
        uint32_t FrameContextCount = DefaultFrameContexts;

        // Fixed-step simulation policy. Defaults target 60 Hz with a 250 ms
        // frame-delta clamp, while allowing 120 Hz or tighter policies when
        // the caller explicitly opts in.
        double FixedStepHz = DefaultFixedStepHz;
        double MaxFrameDeltaSeconds = DefaultMaxFrameDeltaSeconds;
        int MaxSubstepsPerFrame = DefaultMaxSubstepsPerFrame;
        double MaxActiveFps = 0.0;

        // Activity-aware idle throttling. When Enabled, the engine automatically
        // reduces frame rate after IdleTimeoutSeconds of no user/scene activity.
        FramePacingConfig FramePacing{};

        // Benchmark mode: when enabled, the engine runs for a fixed number of frames
        // and writes timing data to a JSON file, then exits.
        bool BenchmarkMode = false;
        uint32_t BenchmarkFrames = 300;
        uint32_t BenchmarkWarmupFrames = 30;
        std::string BenchmarkOutputPath = "benchmark.json";
    };

    class Engine
    {
    public:
        explicit Engine(const EngineConfig& config);
        virtual ~Engine();

        void Run();

        // To be implemented by the Client (Sandbox)
        virtual void OnStart() = 0;
        virtual void OnUpdate(float deltaTime) = 0;

        // Optional fixed-step hook (default no-op). Called 0..N times per render frame.
        // Intended for deterministic simulation / physics.
        virtual void OnFixedUpdate([[maybe_unused]] float fixedDeltaTime) {}

        // Variable-rate render hook. `alpha` is the fixed-step interpolation factor
        // computed from the remaining simulation accumulator after fixed ticks.
        // Runs immediately before the runtime extracts immutable render state and
        // hands the frame to RenderOrchestrator.
        virtual void OnRender([[maybe_unused]] double alpha) {}

        // Override to register additional systems into the FrameGraph each frame.
        // Called after the engine's core systems are registered but before Compile/Execute.
        // Client systems (e.g. AxisRotator, gameplay logic) should be registered here.
        virtual void OnRegisterSystems([[maybe_unused]] Core::FrameGraph& graph, [[maybe_unused]] float deltaTime) {}

        // Override to register fixed-step systems (e.g. physics) into the FrameGraph.
        // Called 0..N times per render frame, once per fixed tick.
        virtual void OnRegisterFixedSystems([[maybe_unused]] Core::FrameGraph& graph, [[maybe_unused]] float fixedDeltaTime) {}

        entt::entity SpawnModel(Core::Assets::AssetHandle modelHandle,
                                Core::Assets::AssetHandle materialHandle,
                                glm::vec3 position,
                                glm::vec3 scale = glm::vec3(1.0f));

    protected:
        // Central feature catalog: render features, systems, panels, geometry operators.
        Core::FeatureRegistry m_FeatureRegistry;

        std::unique_ptr<Core::Windowing::Window> m_Window;

        // All Vulkan/GPU infrastructure: context, device, swapchain, descriptors, etc.
        std::unique_ptr<GraphicsBackend> m_GraphicsBackend;

        // Asset management: AssetManager, pending transfers, main-thread queue, material tracking.
        std::unique_ptr<AssetPipeline> m_AssetPipeline;

        // ECS scene, entity lifecycle, and EnTT GPU-reclaim hooks.
        std::unique_ptr<SceneManager> m_SceneManager;

        // External asset ingest orchestration (drag-drop + re-import).
        std::unique_ptr<AssetIngestService> m_AssetIngestService;

        // Render subsystem: ShaderRegistry, PipelineLibrary, GPUScene, RenderDriver,
        // MaterialRegistry, per-frame arena/scope/FrameGraph, GeometryPool.
        std::unique_ptr<RenderOrchestrator> m_RenderOrchestrator;

        // I/O backend (Phase 0: loose files via std::ifstream)
        std::unique_ptr<Core::IO::IIOBackend> m_IOBackend;

        // Format loader/exporter registry (populated at startup with built-in loaders)
        Graphics::IORegistry m_IORegistry;

    public:
        // Engine-owned selection controller (Editor-like single selection).
        SelectionModule m_Selection;

        [[nodiscard]] SelectionModule& GetSelection() { return m_Selection; }
        [[nodiscard]] const SelectionModule& GetSelection() const { return m_Selection; }

        // Access to the central feature registry.
        [[nodiscard]] Core::FeatureRegistry& GetFeatureRegistry() { return m_FeatureRegistry; }
        [[nodiscard]] const Core::FeatureRegistry& GetFeatureRegistry() const { return m_FeatureRegistry; }

        // Access to the GraphicsBackend subsystem.
        [[nodiscard]] GraphicsBackend& GetGraphicsBackend() const { return *m_GraphicsBackend; }

        // Access to the AssetPipeline subsystem.
        [[nodiscard]] AssetPipeline& GetAssetPipeline() { return *m_AssetPipeline; }
        [[nodiscard]] const AssetPipeline& GetAssetPipeline() const { return *m_AssetPipeline; }

        // Access to the SceneManager subsystem.
        [[nodiscard]] SceneManager& GetSceneManager() { return *m_SceneManager; }
        [[nodiscard]] const SceneManager& GetSceneManager() const { return *m_SceneManager; }

        // Access to the AssetIngestService subsystem.
        [[nodiscard]] AssetIngestService& GetAssetIngestService() { return *m_AssetIngestService; }
        [[nodiscard]] const AssetIngestService& GetAssetIngestService() const { return *m_AssetIngestService; }

        // Access to the RenderOrchestrator subsystem.
        [[nodiscard]] RenderOrchestrator& GetRenderOrchestrator() { return *m_RenderOrchestrator; }
        [[nodiscard]] const RenderOrchestrator& GetRenderOrchestrator() const { return *m_RenderOrchestrator; }

        // Access to the I/O subsystem.
        [[nodiscard]] Core::IO::IIOBackend& GetIOBackend() { return *m_IOBackend; }
        [[nodiscard]] const Graphics::IORegistry& GetIORegistry() const { return m_IORegistry; }
        [[nodiscard]] Graphics::IORegistry& GetIORegistry() { return m_IORegistry; }
        [[nodiscard]] Core::Benchmark::BenchmarkRunner& GetBenchmarkRunner() { return m_BenchmarkRunner; }
        [[nodiscard]] const Core::Benchmark::BenchmarkRunner& GetBenchmarkRunner() const { return m_BenchmarkRunner; }
        [[nodiscard]] const EngineConfig& GetEngineConfig() const { return m_EngineConfig; }


    protected:
        bool m_Running = true;
        bool m_FramebufferResized = false;

        // Benchmark runner (active when EngineConfig::BenchmarkMode is true).
        Core::Benchmark::BenchmarkRunner m_BenchmarkRunner;

    private:
        EngineConfig m_EngineConfig;

        // Populate the FeatureRegistry with all core engine features
        // (render passes, ECS systems). Called once at the end of the constructor.
        void RegisterCoreFeatures();
    };
}
