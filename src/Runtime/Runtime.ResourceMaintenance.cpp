module;
#include <cstdint>

module Runtime.ResourceMaintenance;

import Core.Logging;
import Core.SystemFeatureCatalog;
import Core.Tasks;
import Core.Telemetry;
import Graphics.ShaderHotReload;
import RHI.Image;

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
        // Baseline is 80%; feature toggles select alternate presets.
        const auto thresholdConfig = Runtime::SystemFeatureCatalog::ResolveGpuMemoryWarningThreshold(m_Features);
        const double warningThreshold = thresholdConfig.ThresholdFraction;
        if (!m_LoggedGpuMemoryThresholdConflict && thresholdConfig.EnabledPresetCount > 1u)
        {
            Core::Log::Warn(
                "Multiple GPU memory warning threshold presets are enabled; using {:.0f}% (preset {}).",
                warningThreshold * 100.0,
                thresholdConfig.ActivePresetName);
            m_LoggedGpuMemoryThresholdConflict = true;
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
        if (!m_Features.IsEnabled(Runtime::SystemFeatureCatalog::ShaderHotReload))
            return;

        auto* hotReload = m_Renderer.GetShaderHotReload();
        if (!hotReload)
        {
            // Lazy initialization: create and start the service on first call
            // when the feature is enabled. This avoids startup cost when disabled.
            m_Renderer.InitShaderHotReload();
            return;
        }

        hotReload->PollAndReload(
            m_Graphics.GetSwapchain().GetImageFormat(),
            RHI::VulkanImage::FindDepthFormat(m_Graphics.GetDevice()));
    }
}
