module;
#include <cstdint>

export module Runtime.ResourceMaintenance;

import Graphics.Material;
import Runtime.GraphicsBackend;
import Runtime.RenderOrchestrator;
import Runtime.SceneManager;

export namespace Runtime
{
    class ResourceMaintenanceService
    {
    public:
        ResourceMaintenanceService(SceneManager& scene, RenderOrchestrator& renderer, GraphicsBackend& graphics)
            : m_Scene(scene)
            , m_Renderer(renderer)
            , m_Graphics(graphics)
            , m_Materials(renderer.GetMaterialRegistry())
        {
        }

        void CaptureGpuSyncState();
        void ProcessCompletedReadbacks();
        void CollectGpuDeferredDestructions();
        void GarbageCollectTransfers();
        void ProcessTextureDeletions();
        void ProcessMaterialDeletions();

        [[nodiscard]] uint64_t LastCompletedGraphicsTimelineValue() const { return m_LastCompletedGraphicsTimelineValue; }
        [[nodiscard]] uint64_t LastObservedGlobalFrameNumber() const { return m_LastObservedGlobalFrameNumber; }

    private:
        SceneManager& m_Scene;
        RenderOrchestrator& m_Renderer;
        GraphicsBackend& m_Graphics;
        Graphics::MaterialRegistry& m_Materials;
        uint64_t m_LastCompletedGraphicsTimelineValue = 0;
        uint64_t m_LastObservedGlobalFrameNumber = 0;
    };
}
