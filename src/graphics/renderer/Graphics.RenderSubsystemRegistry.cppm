module;

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Graphics.RenderSubsystemRegistry;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.TransformSyncSystem;

namespace Extrinsic::Graphics
{
    export enum class RenderSubsystemStage : std::uint8_t
    {
        BufferManager,
        SamplerManager,
        TextureManager,
        PipelineManager,
        GpuWorld,
        MaterialSystem,
        ColormapSystem,
        VisualizationSyncSystem,
        TransformSyncSystem,
        CullingSystem,
        LightSystem,
        SelectionSystem,
        ForwardSystem,
        ShadowSystem,
        DeferredSystem,
        PostProcessSystem,
        Count,
    };

    export enum class RenderSubsystemLifecycleEventKind : std::uint8_t
    {
        Initialized,
        Shutdown,
    };

    export struct RenderSubsystemLifecycleEvent
    {
        RenderSubsystemStage Stage = RenderSubsystemStage::Count;
        RenderSubsystemLifecycleEventKind Kind = RenderSubsystemLifecycleEventKind::Initialized;
    };

    export struct RenderSubsystemRegistryDiagnostics
    {
        std::uint32_t InitializedSubsystemCount = 0u;
        std::uint32_t ExpectedSubsystemCount = 0u;
        bool FullyInitialized = false;
        bool LastRebuildSucceeded = false;
        bool LastRebuildFailedMissingRequiredSubsystem = false;
        std::vector<RenderSubsystemStage> MissingStages{};
        std::vector<RenderSubsystemLifecycleEvent> LifecycleEvents{};
    };

    export struct RenderSubsystemRegistryInitOptions
    {
        std::optional<RenderSubsystemStage> StopAfterStage{};
    };

    export class RenderSubsystemRegistry final
    {
    public:
        void Initialize(RHI::IDevice& device,
                        const RenderSubsystemRegistryInitOptions& options = {});
        void ShutdownSystems();
        void ResetStorage();
        void Shutdown();

        [[nodiscard]] bool RebuildOperationalResources(RHI::IDevice& device);
        [[nodiscard]] RenderSubsystemRegistryDiagnostics GetDiagnostics() const;

        [[nodiscard]] std::optional<RHI::BufferManager>& BufferManager() noexcept;
        [[nodiscard]] const std::optional<RHI::BufferManager>& BufferManager() const noexcept;
        [[nodiscard]] std::optional<RHI::SamplerManager>& SamplerManager() noexcept;
        [[nodiscard]] const std::optional<RHI::SamplerManager>& SamplerManager() const noexcept;
        [[nodiscard]] std::optional<RHI::TextureManager>& TextureManager() noexcept;
        [[nodiscard]] const std::optional<RHI::TextureManager>& TextureManager() const noexcept;
        [[nodiscard]] std::optional<RHI::PipelineManager>& PipelineManager() noexcept;
        [[nodiscard]] const std::optional<RHI::PipelineManager>& PipelineManager() const noexcept;
        [[nodiscard]] std::optional<GpuWorld>& GpuWorldSystem() noexcept;
        [[nodiscard]] const std::optional<GpuWorld>& GpuWorldSystem() const noexcept;
        [[nodiscard]] std::optional<MaterialSystem>& MaterialSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<MaterialSystem>& MaterialSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<ColormapSystem>& ColormapSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<ColormapSystem>& ColormapSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<VisualizationSyncSystem>& VisualizationSyncSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<VisualizationSyncSystem>& VisualizationSyncSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<CullingSystem>& CullingSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<CullingSystem>& CullingSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<TransformSyncSystem>& TransformSyncSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<TransformSyncSystem>& TransformSyncSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<LightSystem>& LightSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<LightSystem>& LightSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<SelectionSystem>& SelectionSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<SelectionSystem>& SelectionSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<ForwardSystem>& ForwardSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<ForwardSystem>& ForwardSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<DeferredSystem>& DeferredSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<DeferredSystem>& DeferredSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<PostProcessSystem>& PostProcessSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<PostProcessSystem>& PostProcessSystemRegistry() const noexcept;
        [[nodiscard]] std::optional<ShadowSystem>& ShadowSystemRegistry() noexcept;
        [[nodiscard]] const std::optional<ShadowSystem>& ShadowSystemRegistry() const noexcept;

    private:
        [[nodiscard]] bool IsPresent(RenderSubsystemStage stage) const noexcept;
        void RecordInitialize(RenderSubsystemStage stage);
        void RecordShutdownIfPresent(RenderSubsystemStage stage);

        std::optional<RHI::BufferManager>   m_BufferManager{};
        std::optional<RHI::SamplerManager>  m_SamplerManager{};
        std::optional<RHI::TextureManager>  m_TextureManager{};
        std::optional<RHI::PipelineManager> m_PipelineManager{};
        std::optional<GpuWorld>             m_GpuWorld{};
        std::optional<MaterialSystem>       m_MaterialSystem{};
        std::optional<ColormapSystem>       m_ColormapSystem{};
        std::optional<VisualizationSyncSystem> m_VisualizationSyncSystem{};
        std::optional<CullingSystem>        m_CullingSystem{};
        std::optional<TransformSyncSystem>  m_TransformSyncSystem{};
        std::optional<LightSystem>          m_LightSystem{};
        std::optional<SelectionSystem>      m_SelectionSystem{};
        std::optional<ForwardSystem>        m_ForwardSystem{};
        std::optional<DeferredSystem>       m_DeferredSystem{};
        std::optional<PostProcessSystem>    m_PostProcessSystem{};
        std::optional<ShadowSystem>         m_ShadowSystem{};
        bool m_LastRebuildSucceeded = false;
        bool m_LastRebuildFailedMissingRequiredSubsystem = false;
        std::vector<RenderSubsystemLifecycleEvent> m_LifecycleEvents{};
        std::array<bool, static_cast<std::uint8_t>(RenderSubsystemStage::Count)> m_ShutdownEventsRecorded{};
    };
}
