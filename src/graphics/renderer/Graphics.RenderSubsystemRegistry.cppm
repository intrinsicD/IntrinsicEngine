// Renderer-owned subsystem storage and ordered lifecycle. Release pass borrowers
// before resetting the stored systems.
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
import Extrinsic.Graphics.UvView;

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
        UvView,
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

        std::optional<RHI::BufferManager>   BufferManager{};
        std::optional<RHI::SamplerManager>  SamplerManager{};
        std::optional<RHI::TextureManager>  TextureManager{};
        std::optional<RHI::PipelineManager> PipelineManager{};
        std::optional<GpuWorld>             GpuWorldSystem{};
        std::optional<MaterialSystem>       MaterialSystemRegistry{};
        std::optional<ColormapSystem>       ColormapSystemRegistry{};
        std::optional<VisualizationSyncSystem> VisualizationSyncSystemRegistry{};
        std::optional<CullingSystem>        CullingSystemRegistry{};
        std::optional<TransformSyncSystem>  TransformSyncSystemRegistry{};
        std::optional<LightSystem>          LightSystemRegistry{};
        std::optional<SelectionSystem>      SelectionSystemRegistry{};
        std::optional<ForwardSystem>        ForwardSystemRegistry{};
        std::optional<DeferredSystem>       DeferredSystemRegistry{};
        std::optional<PostProcessSystem>    PostProcessSystemRegistry{};
        std::optional<ShadowSystem>         ShadowSystemRegistry{};
        std::optional<UvView>               UvViewSystem{};

    private:
        [[nodiscard]] bool IsPresent(RenderSubsystemStage stage) const noexcept;
        void RecordInitialize(RenderSubsystemStage stage);
        void RecordShutdownIfPresent(RenderSubsystemStage stage);

        bool m_LastRebuildSucceeded = false;
        bool m_LastRebuildFailedMissingRequiredSubsystem = false;
        std::vector<RenderSubsystemLifecycleEvent> m_LifecycleEvents{};
        std::array<bool, static_cast<std::uint8_t>(RenderSubsystemStage::Count)> m_ShutdownEventsRecorded{};
    };
}
