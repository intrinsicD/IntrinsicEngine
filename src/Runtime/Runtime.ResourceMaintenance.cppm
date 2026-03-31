module;
#include <cstdint>
#include <array>

export module Runtime.ResourceMaintenance;

import Core.FeatureRegistry;
import Graphics.Material;
import Runtime.GraphicsBackend;
import Runtime.RenderOrchestrator;
import Runtime.SceneManager;

export namespace Runtime
{
    struct FrameTelemetrySnapshot
    {
        uint32_t FixedStepSubsteps = 0;
        bool AccumulatorClamped = false;
        uint64_t SimulationCpuTimeNs = 0;
        uint64_t FrameGraphCompileNs = 0;
        uint64_t FrameGraphExecuteNs = 0;
        uint64_t FrameGraphCriticalPathNs = 0;
    };

    class ResourceMaintenanceService
    {
    public:
        ResourceMaintenanceService(SceneManager& scene, RenderOrchestrator& renderer, GraphicsBackend& graphics,
                                   Core::FeatureRegistry& features)
            : m_Scene(scene)
            , m_Renderer(renderer)
            , m_Graphics(graphics)
            , m_Materials(renderer.GetMaterialRegistry())
            , m_Features(features)
        {
        }

        void CaptureGpuSyncState();
        void ProcessCompletedReadbacks();
        void CollectGpuDeferredDestructions();
        void GarbageCollectTransfers();
        void ProcessTextureDeletions();
        void ProcessMaterialDeletions();
        void CaptureFrameTelemetry(const FrameTelemetrySnapshot& snapshot);
        void BookkeepHotReloads();

        [[nodiscard]] uint64_t LastCompletedGraphicsTimelineValue() const { return m_LastCompletedGraphicsTimelineValue; }
        [[nodiscard]] uint64_t LastObservedGlobalFrameNumber() const { return m_LastObservedGlobalFrameNumber; }

    private:
        SceneManager& m_Scene;
        RenderOrchestrator& m_Renderer;
        GraphicsBackend& m_Graphics;
        Graphics::MaterialRegistry& m_Materials;
        Core::FeatureRegistry& m_Features;
        uint64_t m_LastCompletedGraphicsTimelineValue = 0;
        uint64_t m_LastObservedGlobalFrameNumber = 0;

        // Per-heap memory warning state: true = currently above threshold.
        // Warnings fire once per transition (below→above), not per frame.
        static constexpr uint32_t kMaxHeaps = 16;
        std::array<bool, kMaxHeaps> m_HeapOverBudget{};
    };
}
