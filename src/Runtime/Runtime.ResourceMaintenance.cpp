module Runtime.ResourceMaintenance;

import Core.Tasks;
import Core.Telemetry;

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
        m_Renderer.GetRenderDriver().ProcessCompletedGpuWork(m_Scene.GetScene(),
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

    void ResourceMaintenanceService::CaptureFrameTelemetry(const FrameTelemetrySnapshot& snapshot)
    {
        auto& telemetry = Core::Telemetry::TelemetrySystem::Get();
        telemetry.SetSimulationStats(
            snapshot.FixedStepSubsteps,
            snapshot.AccumulatorClamped ? 1u : 0u,
            snapshot.SimulationCpuTimeNs);
        telemetry.SetTaskSchedulerStats(Core::Tasks::Scheduler::GetStats());
        telemetry.SetFrameGraphTimings(
            snapshot.FrameGraphCompileNs,
            snapshot.FrameGraphExecuteNs,
            snapshot.FrameGraphCriticalPathNs);
    }

    void ResourceMaintenanceService::BookkeepHotReloads()
    {
        // Hook for future shader/material hot-reload bookkeeping.
        // FileWatcher infrastructure exists; shader-specific reload
        // will be wired here when implemented (see ROADMAP.md Ongoing).
    }
}
