module;

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

module Extrinsic.Graphics.RenderSubsystemRegistry;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::array<RenderSubsystemStage,
                             static_cast<std::uint8_t>(RenderSubsystemStage::Count)>
            kAllStages{
                RenderSubsystemStage::BufferManager,
                RenderSubsystemStage::SamplerManager,
                RenderSubsystemStage::TextureManager,
                RenderSubsystemStage::PipelineManager,
                RenderSubsystemStage::GpuWorld,
                RenderSubsystemStage::MaterialSystem,
                RenderSubsystemStage::ColormapSystem,
                RenderSubsystemStage::VisualizationSyncSystem,
                RenderSubsystemStage::TransformSyncSystem,
                RenderSubsystemStage::CullingSystem,
                RenderSubsystemStage::LightSystem,
                RenderSubsystemStage::SelectionSystem,
                RenderSubsystemStage::ForwardSystem,
                RenderSubsystemStage::ShadowSystem,
                RenderSubsystemStage::DeferredSystem,
                RenderSubsystemStage::PostProcessSystem,
                RenderSubsystemStage::UvView,
            };

        constexpr std::uint8_t StageIndex(const RenderSubsystemStage stage) noexcept
        {
            return static_cast<std::uint8_t>(stage);
        }
    }

    void RenderSubsystemRegistry::Initialize(RHI::IDevice& device,
                                             const RenderSubsystemRegistryInitOptions& options)
    {
        ResetStorage();
        m_LifecycleEvents.clear();
        m_ShutdownEventsRecorded.fill(false);
        m_LastRebuildSucceeded = false;
        m_LastRebuildFailedMissingRequiredSubsystem = false;

        const auto shouldStop = [&options](const RenderSubsystemStage stage) noexcept {
            return options.StopAfterStage.has_value() && *options.StopAfterStage == stage;
        };

        m_BufferManager.emplace(device);
        RecordInitialize(RenderSubsystemStage::BufferManager);
        if (shouldStop(RenderSubsystemStage::BufferManager)) return;

        m_SamplerManager.emplace(device);
        RecordInitialize(RenderSubsystemStage::SamplerManager);
        if (shouldStop(RenderSubsystemStage::SamplerManager)) return;

        m_TextureManager.emplace(device, device.GetBindlessHeap());
        RecordInitialize(RenderSubsystemStage::TextureManager);
        if (shouldStop(RenderSubsystemStage::TextureManager)) return;

        m_PipelineManager.emplace(device);
        RecordInitialize(RenderSubsystemStage::PipelineManager);
        if (shouldStop(RenderSubsystemStage::PipelineManager)) return;

        m_GpuWorld.emplace();
        m_GpuWorld->Initialize(device, *m_BufferManager);
        RecordInitialize(RenderSubsystemStage::GpuWorld);
        if (shouldStop(RenderSubsystemStage::GpuWorld)) return;

        m_MaterialSystem.emplace();
        m_MaterialSystem->Initialize(device, *m_BufferManager);
        RecordInitialize(RenderSubsystemStage::MaterialSystem);
        if (shouldStop(RenderSubsystemStage::MaterialSystem)) return;

        m_ColormapSystem.emplace();
        m_ColormapSystem->Initialize(device, *m_TextureManager, *m_SamplerManager);
        RecordInitialize(RenderSubsystemStage::ColormapSystem);
        if (shouldStop(RenderSubsystemStage::ColormapSystem)) return;

        m_VisualizationSyncSystem.emplace();
        m_VisualizationSyncSystem->Initialize(*m_MaterialSystem, device);
        RecordInitialize(RenderSubsystemStage::VisualizationSyncSystem);
        if (shouldStop(RenderSubsystemStage::VisualizationSyncSystem)) return;

        m_TransformSyncSystem.emplace();
        m_TransformSyncSystem->Initialize();
        RecordInitialize(RenderSubsystemStage::TransformSyncSystem);
        if (shouldStop(RenderSubsystemStage::TransformSyncSystem)) return;

        m_GpuWorld->SetMaterialBuffer(
            m_MaterialSystem->GetBuffer(),
            m_MaterialSystem->GetCapacity());

        m_CullingSystem.emplace();
        RecordInitialize(RenderSubsystemStage::CullingSystem);
        if (shouldStop(RenderSubsystemStage::CullingSystem)) return;

        m_LightSystem.emplace();
        m_LightSystem->Initialize();
        RecordInitialize(RenderSubsystemStage::LightSystem);
        if (shouldStop(RenderSubsystemStage::LightSystem)) return;

        m_SelectionSystem.emplace();
        m_SelectionSystem->Initialize();
        RecordInitialize(RenderSubsystemStage::SelectionSystem);
        if (shouldStop(RenderSubsystemStage::SelectionSystem)) return;

        m_ForwardSystem.emplace();
        m_ForwardSystem->Initialize();
        RecordInitialize(RenderSubsystemStage::ForwardSystem);
        if (shouldStop(RenderSubsystemStage::ForwardSystem)) return;

        m_ShadowSystem.emplace();
        m_ShadowSystem->Initialize(device, *m_TextureManager, *m_SamplerManager);
        RecordInitialize(RenderSubsystemStage::ShadowSystem);
        if (shouldStop(RenderSubsystemStage::ShadowSystem)) return;

        m_DeferredSystem.emplace();
        m_DeferredSystem->Initialize();
        RecordInitialize(RenderSubsystemStage::DeferredSystem);
        if (shouldStop(RenderSubsystemStage::DeferredSystem)) return;

        m_PostProcessSystem.emplace();
        m_PostProcessSystem->Initialize(device, *m_TextureManager, *m_BufferManager);
        RecordInitialize(RenderSubsystemStage::PostProcessSystem);
        if (shouldStop(RenderSubsystemStage::PostProcessSystem)) return;

        m_UvView.emplace();
        m_UvView->Initialize(device,
                             *m_BufferManager,
                             *m_TextureManager,
                             *m_SamplerManager,
                             *m_PipelineManager);
        RecordInitialize(RenderSubsystemStage::UvView);
    }

    void RenderSubsystemRegistry::ShutdownSystems()
    {
        if (m_UvView)
        {
            m_UvView->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::UvView);

        if (m_SelectionSystem)
        {
            m_SelectionSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::SelectionSystem);

        if (m_LightSystem)
        {
            m_LightSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::LightSystem);

        if (m_ForwardSystem)
        {
            m_ForwardSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::ForwardSystem);

        if (m_DeferredSystem)
        {
            m_DeferredSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::DeferredSystem);

        if (m_PostProcessSystem)
        {
            m_PostProcessSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::PostProcessSystem);

        if (m_ShadowSystem)
        {
            m_ShadowSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::ShadowSystem);

        if (m_CullingSystem)
        {
            m_CullingSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::CullingSystem);

        if (m_TransformSyncSystem)
        {
            m_TransformSyncSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::TransformSyncSystem);

        if (m_VisualizationSyncSystem)
        {
            m_VisualizationSyncSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::VisualizationSyncSystem);

        if (m_ColormapSystem)
        {
            m_ColormapSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::ColormapSystem);

        if (m_GpuWorld)
        {
            m_GpuWorld->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::GpuWorld);

        if (m_MaterialSystem)
        {
            m_MaterialSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::MaterialSystem);
    }

    void RenderSubsystemRegistry::ResetStorage()
    {
        m_UvView.reset();
        m_SelectionSystem.reset();
        m_LightSystem.reset();
        m_ForwardSystem.reset();
        m_DeferredSystem.reset();
        m_PostProcessSystem.reset();
        m_ShadowSystem.reset();
        m_CullingSystem.reset();
        m_TransformSyncSystem.reset();
        m_VisualizationSyncSystem.reset();
        m_ColormapSystem.reset();
        m_GpuWorld.reset();
        m_MaterialSystem.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::PipelineManager);
        m_PipelineManager.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::TextureManager);
        m_TextureManager.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::SamplerManager);
        m_SamplerManager.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::BufferManager);
        m_BufferManager.reset();
    }

    void RenderSubsystemRegistry::Shutdown()
    {
        ShutdownSystems();
        ResetStorage();
    }

    bool RenderSubsystemRegistry::RebuildOperationalResources(RHI::IDevice& device)
    {
        m_LastRebuildSucceeded = false;
        m_LastRebuildFailedMissingRequiredSubsystem = false;
        if (!m_BufferManager || !m_MaterialSystem || !m_GpuWorld)
        {
            m_LastRebuildFailedMissingRequiredSubsystem = true;
            return false;
        }

        if (!m_MaterialSystem->RebuildGpuResources(device, *m_BufferManager))
        {
            return false;
        }
        if (!m_GpuWorld->RebuildGpuResources(device, *m_BufferManager))
        {
            return false;
        }
        if (m_ColormapSystem && m_TextureManager && m_SamplerManager &&
            !m_ColormapSystem->IsInitialized())
        {
            m_ColormapSystem->Initialize(device, *m_TextureManager, *m_SamplerManager);
        }

        m_GpuWorld->SetMaterialBuffer(
            m_MaterialSystem->GetBuffer(),
            m_MaterialSystem->GetCapacity());
        m_MaterialSystem->SyncGpuBuffer();
        m_GpuWorld->SyncFrame();

        if (m_PostProcessSystem && m_TextureManager)
        {
            m_PostProcessSystem->Initialize(device, *m_TextureManager, *m_BufferManager);
        }
        if (m_UvView)
        {
            (void)m_UvView->RebuildOperationalResources(device);
        }

        m_LastRebuildSucceeded = true;
        return true;
    }

    RenderSubsystemRegistryDiagnostics RenderSubsystemRegistry::GetDiagnostics() const
    {
        RenderSubsystemRegistryDiagnostics diagnostics{};
        diagnostics.ExpectedSubsystemCount = static_cast<std::uint32_t>(kAllStages.size());
        diagnostics.LastRebuildSucceeded = m_LastRebuildSucceeded;
        diagnostics.LastRebuildFailedMissingRequiredSubsystem =
            m_LastRebuildFailedMissingRequiredSubsystem;
        diagnostics.LifecycleEvents = m_LifecycleEvents;
        for (const RenderSubsystemStage stage : kAllStages)
        {
            if (IsPresent(stage))
            {
                ++diagnostics.InitializedSubsystemCount;
            }
            else
            {
                diagnostics.MissingStages.push_back(stage);
            }
        }
        diagnostics.FullyInitialized =
            diagnostics.InitializedSubsystemCount == diagnostics.ExpectedSubsystemCount;
        return diagnostics;
    }

    std::optional<RHI::BufferManager>& RenderSubsystemRegistry::BufferManager() noexcept
    {
        return m_BufferManager;
    }

    const std::optional<RHI::BufferManager>& RenderSubsystemRegistry::BufferManager() const noexcept
    {
        return m_BufferManager;
    }

    std::optional<RHI::SamplerManager>& RenderSubsystemRegistry::SamplerManager() noexcept
    {
        return m_SamplerManager;
    }

    const std::optional<RHI::SamplerManager>& RenderSubsystemRegistry::SamplerManager() const noexcept
    {
        return m_SamplerManager;
    }

    std::optional<RHI::TextureManager>& RenderSubsystemRegistry::TextureManager() noexcept
    {
        return m_TextureManager;
    }

    const std::optional<RHI::TextureManager>& RenderSubsystemRegistry::TextureManager() const noexcept
    {
        return m_TextureManager;
    }

    std::optional<RHI::PipelineManager>& RenderSubsystemRegistry::PipelineManager() noexcept
    {
        return m_PipelineManager;
    }

    const std::optional<RHI::PipelineManager>& RenderSubsystemRegistry::PipelineManager() const noexcept
    {
        return m_PipelineManager;
    }

    std::optional<GpuWorld>& RenderSubsystemRegistry::GpuWorldSystem() noexcept
    {
        return m_GpuWorld;
    }

    const std::optional<GpuWorld>& RenderSubsystemRegistry::GpuWorldSystem() const noexcept
    {
        return m_GpuWorld;
    }

    std::optional<MaterialSystem>& RenderSubsystemRegistry::MaterialSystemRegistry() noexcept
    {
        return m_MaterialSystem;
    }

    const std::optional<MaterialSystem>& RenderSubsystemRegistry::MaterialSystemRegistry() const noexcept
    {
        return m_MaterialSystem;
    }

    std::optional<ColormapSystem>& RenderSubsystemRegistry::ColormapSystemRegistry() noexcept
    {
        return m_ColormapSystem;
    }

    const std::optional<ColormapSystem>& RenderSubsystemRegistry::ColormapSystemRegistry() const noexcept
    {
        return m_ColormapSystem;
    }

    std::optional<VisualizationSyncSystem>&
    RenderSubsystemRegistry::VisualizationSyncSystemRegistry() noexcept
    {
        return m_VisualizationSyncSystem;
    }

    const std::optional<VisualizationSyncSystem>&
    RenderSubsystemRegistry::VisualizationSyncSystemRegistry() const noexcept
    {
        return m_VisualizationSyncSystem;
    }

    std::optional<CullingSystem>& RenderSubsystemRegistry::CullingSystemRegistry() noexcept
    {
        return m_CullingSystem;
    }

    const std::optional<CullingSystem>& RenderSubsystemRegistry::CullingSystemRegistry() const noexcept
    {
        return m_CullingSystem;
    }

    std::optional<TransformSyncSystem>& RenderSubsystemRegistry::TransformSyncSystemRegistry() noexcept
    {
        return m_TransformSyncSystem;
    }

    const std::optional<TransformSyncSystem>&
    RenderSubsystemRegistry::TransformSyncSystemRegistry() const noexcept
    {
        return m_TransformSyncSystem;
    }

    std::optional<LightSystem>& RenderSubsystemRegistry::LightSystemRegistry() noexcept
    {
        return m_LightSystem;
    }

    const std::optional<LightSystem>& RenderSubsystemRegistry::LightSystemRegistry() const noexcept
    {
        return m_LightSystem;
    }

    std::optional<SelectionSystem>& RenderSubsystemRegistry::SelectionSystemRegistry() noexcept
    {
        return m_SelectionSystem;
    }

    const std::optional<SelectionSystem>& RenderSubsystemRegistry::SelectionSystemRegistry() const noexcept
    {
        return m_SelectionSystem;
    }

    std::optional<ForwardSystem>& RenderSubsystemRegistry::ForwardSystemRegistry() noexcept
    {
        return m_ForwardSystem;
    }

    const std::optional<ForwardSystem>& RenderSubsystemRegistry::ForwardSystemRegistry() const noexcept
    {
        return m_ForwardSystem;
    }

    std::optional<DeferredSystem>& RenderSubsystemRegistry::DeferredSystemRegistry() noexcept
    {
        return m_DeferredSystem;
    }

    const std::optional<DeferredSystem>& RenderSubsystemRegistry::DeferredSystemRegistry() const noexcept
    {
        return m_DeferredSystem;
    }

    std::optional<PostProcessSystem>& RenderSubsystemRegistry::PostProcessSystemRegistry() noexcept
    {
        return m_PostProcessSystem;
    }

    const std::optional<PostProcessSystem>&
    RenderSubsystemRegistry::PostProcessSystemRegistry() const noexcept
    {
        return m_PostProcessSystem;
    }

    std::optional<ShadowSystem>& RenderSubsystemRegistry::ShadowSystemRegistry() noexcept
    {
        return m_ShadowSystem;
    }

    const std::optional<ShadowSystem>& RenderSubsystemRegistry::ShadowSystemRegistry() const noexcept
    {
        return m_ShadowSystem;
    }

    std::optional<UvView>& RenderSubsystemRegistry::UvViewSystem() noexcept
    {
        return m_UvView;
    }

    const std::optional<UvView>& RenderSubsystemRegistry::UvViewSystem() const noexcept
    {
        return m_UvView;
    }

    bool RenderSubsystemRegistry::IsPresent(const RenderSubsystemStage stage) const noexcept
    {
        switch (stage)
        {
        case RenderSubsystemStage::BufferManager: return m_BufferManager.has_value();
        case RenderSubsystemStage::SamplerManager: return m_SamplerManager.has_value();
        case RenderSubsystemStage::TextureManager: return m_TextureManager.has_value();
        case RenderSubsystemStage::PipelineManager: return m_PipelineManager.has_value();
        case RenderSubsystemStage::GpuWorld: return m_GpuWorld.has_value();
        case RenderSubsystemStage::MaterialSystem: return m_MaterialSystem.has_value();
        case RenderSubsystemStage::ColormapSystem: return m_ColormapSystem.has_value();
        case RenderSubsystemStage::VisualizationSyncSystem:
            return m_VisualizationSyncSystem.has_value();
        case RenderSubsystemStage::TransformSyncSystem: return m_TransformSyncSystem.has_value();
        case RenderSubsystemStage::CullingSystem: return m_CullingSystem.has_value();
        case RenderSubsystemStage::LightSystem: return m_LightSystem.has_value();
        case RenderSubsystemStage::SelectionSystem: return m_SelectionSystem.has_value();
        case RenderSubsystemStage::ForwardSystem: return m_ForwardSystem.has_value();
        case RenderSubsystemStage::ShadowSystem: return m_ShadowSystem.has_value();
        case RenderSubsystemStage::DeferredSystem: return m_DeferredSystem.has_value();
        case RenderSubsystemStage::PostProcessSystem: return m_PostProcessSystem.has_value();
        case RenderSubsystemStage::UvView: return m_UvView.has_value();
        case RenderSubsystemStage::Count: return false;
        }
        return false;
    }

    void RenderSubsystemRegistry::RecordInitialize(const RenderSubsystemStage stage)
    {
        if (stage == RenderSubsystemStage::Count)
        {
            return;
        }
        m_ShutdownEventsRecorded[StageIndex(stage)] = false;
        m_LifecycleEvents.push_back(RenderSubsystemLifecycleEvent{
            .Stage = stage,
            .Kind = RenderSubsystemLifecycleEventKind::Initialized,
        });
    }

    void RenderSubsystemRegistry::RecordShutdownIfPresent(const RenderSubsystemStage stage)
    {
        if (stage == RenderSubsystemStage::Count || !IsPresent(stage))
        {
            return;
        }
        const std::uint8_t index = StageIndex(stage);
        if (m_ShutdownEventsRecorded[index])
        {
            return;
        }
        m_ShutdownEventsRecorded[index] = true;
        m_LifecycleEvents.push_back(RenderSubsystemLifecycleEvent{
            .Stage = stage,
            .Kind = RenderSubsystemLifecycleEventKind::Shutdown,
        });
    }
}
