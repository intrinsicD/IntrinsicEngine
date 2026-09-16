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

        BufferManager.emplace(device);
        RecordInitialize(RenderSubsystemStage::BufferManager);
        if (shouldStop(RenderSubsystemStage::BufferManager)) return;

        SamplerManager.emplace(device);
        RecordInitialize(RenderSubsystemStage::SamplerManager);
        if (shouldStop(RenderSubsystemStage::SamplerManager)) return;

        TextureManager.emplace(device, device.GetBindlessHeap());
        RecordInitialize(RenderSubsystemStage::TextureManager);
        if (shouldStop(RenderSubsystemStage::TextureManager)) return;

        PipelineManager.emplace(device);
        RecordInitialize(RenderSubsystemStage::PipelineManager);
        if (shouldStop(RenderSubsystemStage::PipelineManager)) return;

        GpuWorldSystem.emplace();
        GpuWorldSystem->Initialize(device, *BufferManager);
        RecordInitialize(RenderSubsystemStage::GpuWorld);
        if (shouldStop(RenderSubsystemStage::GpuWorld)) return;

        MaterialSystemRegistry.emplace();
        MaterialSystemRegistry->Initialize(device, *BufferManager);
        RecordInitialize(RenderSubsystemStage::MaterialSystem);
        if (shouldStop(RenderSubsystemStage::MaterialSystem)) return;

        ColormapSystemRegistry.emplace();
        ColormapSystemRegistry->Initialize(device, *TextureManager, *SamplerManager);
        RecordInitialize(RenderSubsystemStage::ColormapSystem);
        if (shouldStop(RenderSubsystemStage::ColormapSystem)) return;

        VisualizationSyncSystemRegistry.emplace();
        VisualizationSyncSystemRegistry->Initialize(device);
        RecordInitialize(RenderSubsystemStage::VisualizationSyncSystem);
        if (shouldStop(RenderSubsystemStage::VisualizationSyncSystem)) return;

        TransformSyncSystemRegistry.emplace();
        TransformSyncSystemRegistry->Initialize();
        RecordInitialize(RenderSubsystemStage::TransformSyncSystem);
        if (shouldStop(RenderSubsystemStage::TransformSyncSystem)) return;

        GpuWorldSystem->SetMaterialBuffer(
            MaterialSystemRegistry->GetBuffer(),
            MaterialSystemRegistry->GetCapacity());

        CullingSystemRegistry.emplace();
        RecordInitialize(RenderSubsystemStage::CullingSystem);
        if (shouldStop(RenderSubsystemStage::CullingSystem)) return;

        LightSystemRegistry.emplace();
        LightSystemRegistry->Initialize();
        RecordInitialize(RenderSubsystemStage::LightSystem);
        if (shouldStop(RenderSubsystemStage::LightSystem)) return;

        SelectionSystemRegistry.emplace();
        SelectionSystemRegistry->Initialize();
        RecordInitialize(RenderSubsystemStage::SelectionSystem);
        if (shouldStop(RenderSubsystemStage::SelectionSystem)) return;

        ForwardSystemRegistry.emplace();
        ForwardSystemRegistry->Initialize();
        RecordInitialize(RenderSubsystemStage::ForwardSystem);
        if (shouldStop(RenderSubsystemStage::ForwardSystem)) return;

        ShadowSystemRegistry.emplace();
        ShadowSystemRegistry->Initialize(device, *TextureManager, *SamplerManager);
        RecordInitialize(RenderSubsystemStage::ShadowSystem);
        if (shouldStop(RenderSubsystemStage::ShadowSystem)) return;

        DeferredSystemRegistry.emplace();
        DeferredSystemRegistry->Initialize();
        RecordInitialize(RenderSubsystemStage::DeferredSystem);
        if (shouldStop(RenderSubsystemStage::DeferredSystem)) return;

        PostProcessSystemRegistry.emplace();
        PostProcessSystemRegistry->Initialize(device, *TextureManager, *BufferManager);
        RecordInitialize(RenderSubsystemStage::PostProcessSystem);
        if (shouldStop(RenderSubsystemStage::PostProcessSystem)) return;

        UvViewSystem.emplace();
        UvViewSystem->Initialize(device,
                             *BufferManager,
                             *TextureManager,
                             *SamplerManager,
                             *PipelineManager);
        RecordInitialize(RenderSubsystemStage::UvView);
    }

    void RenderSubsystemRegistry::ShutdownSystems()
    {
        if (UvViewSystem)
        {
            UvViewSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::UvView);

        if (SelectionSystemRegistry)
        {
            SelectionSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::SelectionSystem);

        if (LightSystemRegistry)
        {
            LightSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::LightSystem);

        if (ForwardSystemRegistry)
        {
            ForwardSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::ForwardSystem);

        if (DeferredSystemRegistry)
        {
            DeferredSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::DeferredSystem);

        if (PostProcessSystemRegistry)
        {
            PostProcessSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::PostProcessSystem);

        if (ShadowSystemRegistry)
        {
            ShadowSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::ShadowSystem);

        if (CullingSystemRegistry)
        {
            CullingSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::CullingSystem);

        if (TransformSyncSystemRegistry)
        {
            TransformSyncSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::TransformSyncSystem);

        if (VisualizationSyncSystemRegistry)
        {
            VisualizationSyncSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::VisualizationSyncSystem);

        if (ColormapSystemRegistry)
        {
            ColormapSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::ColormapSystem);

        if (GpuWorldSystem)
        {
            GpuWorldSystem->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::GpuWorld);

        if (MaterialSystemRegistry)
        {
            MaterialSystemRegistry->Shutdown();
        }
        RecordShutdownIfPresent(RenderSubsystemStage::MaterialSystem);
    }

    void RenderSubsystemRegistry::ResetStorage()
    {
        UvViewSystem.reset();
        SelectionSystemRegistry.reset();
        LightSystemRegistry.reset();
        ForwardSystemRegistry.reset();
        DeferredSystemRegistry.reset();
        PostProcessSystemRegistry.reset();
        ShadowSystemRegistry.reset();
        CullingSystemRegistry.reset();
        TransformSyncSystemRegistry.reset();
        VisualizationSyncSystemRegistry.reset();
        ColormapSystemRegistry.reset();
        GpuWorldSystem.reset();
        MaterialSystemRegistry.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::PipelineManager);
        PipelineManager.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::TextureManager);
        TextureManager.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::SamplerManager);
        SamplerManager.reset();
        RecordShutdownIfPresent(RenderSubsystemStage::BufferManager);
        BufferManager.reset();
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
        if (!BufferManager || !MaterialSystemRegistry || !GpuWorldSystem)
        {
            m_LastRebuildFailedMissingRequiredSubsystem = true;
            return false;
        }

        if (!MaterialSystemRegistry->RebuildGpuResources(device, *BufferManager))
        {
            return false;
        }
        if (!GpuWorldSystem->RebuildGpuResources(device, *BufferManager))
        {
            return false;
        }
        if (ColormapSystemRegistry && TextureManager && SamplerManager &&
            !ColormapSystemRegistry->IsInitialized())
        {
            ColormapSystemRegistry->Initialize(device, *TextureManager, *SamplerManager);
        }

        GpuWorldSystem->SetMaterialBuffer(
            MaterialSystemRegistry->GetBuffer(),
            MaterialSystemRegistry->GetCapacity());
        MaterialSystemRegistry->SyncGpuBuffer();
        GpuWorldSystem->SyncFrame();

        if (PostProcessSystemRegistry && TextureManager)
        {
            PostProcessSystemRegistry->Initialize(device, *TextureManager, *BufferManager);
        }
        if (UvViewSystem)
        {
            (void)UvViewSystem->RebuildOperationalResources(device);
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

    bool RenderSubsystemRegistry::IsPresent(const RenderSubsystemStage stage) const noexcept
    {
        switch (stage)
        {
        case RenderSubsystemStage::BufferManager: return BufferManager.has_value();
        case RenderSubsystemStage::SamplerManager: return SamplerManager.has_value();
        case RenderSubsystemStage::TextureManager: return TextureManager.has_value();
        case RenderSubsystemStage::PipelineManager: return PipelineManager.has_value();
        case RenderSubsystemStage::GpuWorld: return GpuWorldSystem.has_value();
        case RenderSubsystemStage::MaterialSystem: return MaterialSystemRegistry.has_value();
        case RenderSubsystemStage::ColormapSystem: return ColormapSystemRegistry.has_value();
        case RenderSubsystemStage::VisualizationSyncSystem:
            return VisualizationSyncSystemRegistry.has_value();
        case RenderSubsystemStage::TransformSyncSystem: return TransformSyncSystemRegistry.has_value();
        case RenderSubsystemStage::CullingSystem: return CullingSystemRegistry.has_value();
        case RenderSubsystemStage::LightSystem: return LightSystemRegistry.has_value();
        case RenderSubsystemStage::SelectionSystem: return SelectionSystemRegistry.has_value();
        case RenderSubsystemStage::ForwardSystem: return ForwardSystemRegistry.has_value();
        case RenderSubsystemStage::ShadowSystem: return ShadowSystemRegistry.has_value();
        case RenderSubsystemStage::DeferredSystem: return DeferredSystemRegistry.has_value();
        case RenderSubsystemStage::PostProcessSystem: return PostProcessSystemRegistry.has_value();
        case RenderSubsystemStage::UvView: return UvViewSystem.has_value();
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
