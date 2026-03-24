module Runtime.ResourceMaintenance;

namespace Runtime
{
    void ResourceMaintenanceService::CaptureGpuSyncState()
    {
        auto& device = m_Graphics.GetDevice();
        m_LastCompletedGraphicsTimelineValue = device.GetGraphicsTimelineCompletedValue();
        m_LastObservedGlobalFrameNumber = device.GetGlobalFrameNumber();
    }

    void ResourceMaintenanceService::ProcessCompletedReadbacks()
    {
        m_Renderer.GetRenderSystem().ProcessCompletedGpuWork(m_Scene.GetScene(),
                                                             m_LastObservedGlobalFrameNumber);
    }

    void ResourceMaintenanceService::CollectGpuDeferredDestructions()
    {
        m_Graphics.CollectGpuDeferredDestructions();
    }

    void ResourceMaintenanceService::GarbageCollectTransfers()
    {
        m_Graphics.GarbageCollectTransfers();
    }

    void ResourceMaintenanceService::ProcessTextureDeletions()
    {
        m_Graphics.ProcessTextureDeletions();
    }

    void ResourceMaintenanceService::ProcessMaterialDeletions()
    {
        m_Materials.ProcessDeletions(m_LastObservedGlobalFrameNumber);
    }
}
