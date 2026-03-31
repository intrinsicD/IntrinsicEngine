module;
#include <cstdint>

module Runtime.ResourceMaintenance;

import Core.Logging;
import Core.SystemFeatureCatalog;
import Core.Tasks;
import Core.Telemetry;

namespace Runtime
{
    void ResourceMaintenanceService::CaptureGpuSyncState()
    {
        auto& device = m_Graphics.GetDevice();
        m_LastCompletedGraphicsTimelineValue = device.GetGraphicsTimelineCompletedValue();
        m_LastObservedGlobalFrameNumber = device.GetGlobalFrameNumber();

        // Query GPU memory budgets and publish to telemetry.
        const auto memSnap = device.QueryMemoryBudgets();
        Core::Telemetry::TelemetrySystem::Get().SetGpuMemoryBudgets(memSnap);

        // Fire once-per-transition warnings when a heap crosses the configured threshold.
        // Baseline is 80%; feature toggles can raise/lower this.
        double warningThreshold = 0.80;
        if (m_Features.IsEnabled(Runtime::SystemFeatureCatalog::GpuMemoryWarnThreshold70))
        {
            warningThreshold = 0.70;
        }
        if (m_Features.IsEnabled(Runtime::SystemFeatureCatalog::GpuMemoryWarnThreshold90))
        {
            warningThreshold = 0.90;
        }

        for (uint32_t i = 0; i < memSnap.HeapCount && i < kMaxHeaps; ++i)
        {
            if (memSnap.Heaps[i].BudgetBytes == 0) continue;
            const double usage = static_cast<double>(memSnap.Heaps[i].UsageBytes)
                               / static_cast<double>(memSnap.Heaps[i].BudgetBytes);
            const bool over = usage >= warningThreshold;
            if (over && !m_HeapOverBudget[i])
            {
                const bool deviceLocal = (memSnap.Heaps[i].Flags & Core::Telemetry::kHeapFlagDeviceLocal) != 0;
                Core::Log::Warn("GPU memory heap {} ({}) at {:.1f}% of budget ({:.0f} / {:.0f} MB)",
                    i, deviceLocal ? "device-local" : "host-visible", usage * 100.0,
                    static_cast<double>(memSnap.Heaps[i].UsageBytes) / (1024.0 * 1024.0),
                    static_cast<double>(memSnap.Heaps[i].BudgetBytes) / (1024.0 * 1024.0));
            }
            m_HeapOverBudget[i] = over;
        }
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
