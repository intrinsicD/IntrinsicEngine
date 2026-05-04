module;

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;

namespace Extrinsic::Graphics
{
    export enum class RenderCommandPassStatus : std::uint8_t
    {
        Recorded,
        SkippedNonOperational,
        SkippedUnavailable,
    };

    export struct RenderGraphCompileStats
    {
        bool Succeeded = false;
        std::uint32_t PassCount = 0;
        std::uint32_t CulledPassCount = 0;
        std::uint32_t ResourceCount = 0;
        std::uint32_t BarrierCount = 0;
        std::uint64_t TransientMemoryEstimateBytes = 0;
        std::uint64_t TimeMicros = 0;
    };

    export struct RenderGraphExecuteStats
    {
        bool Succeeded = false;
        bool DeviceOperational = false;
        std::uint64_t TimeMicros = 0;
    };

    export struct RenderGraphCommandPassStats
    {
        std::string Name{};
        RenderCommandPassStatus Status = RenderCommandPassStatus::SkippedUnavailable;
    };

    export struct RenderGraphCommandRecordStats
    {
        std::uint32_t Recorded = 0;
        std::uint32_t Skipped = 0;
        std::uint32_t SkippedNonOperational = 0;
        std::uint32_t SkippedUnavailable = 0;
        std::vector<RenderGraphCommandPassStats> Passes{};
    };

    export struct RenderGraphFrameStats
    {
        RenderGraphCompileStats Compile{};
        RenderGraphExecuteStats Execute{};
        RenderGraphCommandRecordStats CommandRecords{};
        std::string DebugDump{};
        std::string Diagnostic{};
        std::string LifecycleDiagnostic{};
    };

    export struct RuntimeRenderSnapshotBatch
    {
        std::span<const TransformSyncRecord>     Transforms{};
        std::span<const LightSnapshot>           Lights{};
        std::span<const VisualizationSyncRecord> Visualizations{};
        std::span<const VisualizationAttributeBufferPacket> VisualizationAttributeBuffers{};
        std::span<const ScalarAttributePacket>              VisualizationScalars{};
        std::span<const ColorAttributePacket>               VisualizationColors{};
        std::span<const VectorFieldOverlayPacket>           VisualizationVectorFields{};
        std::span<const IsolineOverlayPacket>               VisualizationIsolines{};
        std::span<const HtexPatchPreviewAtlasPacket>        VisualizationHtexAtlases{};
        std::span<const FragmentBakeAtlasPacket>            VisualizationFragmentBakeAtlases{};
        std::span<const DebugLinePacket>         DebugLines{};
        std::span<const DebugPointPacket>        DebugPoints{};
        std::span<const DebugTrianglePacket>     DebugTriangles{};
        std::span<const TransformGizmoRenderPacket> TransformGizmos{};
    };

    export class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // ── Subsystem lifecycle ───────────────────────────────────────────

        virtual void Initialize(RHI::IDevice& device) = 0;

        // Runtime calls this after a device that was initialized in a
        // fail-closed/non-operational state becomes operational. Implementations
        // must rebuild GPU-only state through RHI managers without importing or
        // special-casing concrete backends.
        [[nodiscard]] virtual bool RebuildOperationalResources(RHI::IDevice& device) = 0;

        virtual void Shutdown() = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        // ── Per-frame phases (called in this order every frame) ───────────
        //
        //  1. BeginFrame     — acquire swapchain image, open command contexts.
        //                      Returns false if the frame must be skipped
        //                      (out-of-date swapchain, device lost, minimized).
        //
        //  2. ExtractRenderWorld — snapshot immutable render data from the
        //                      committed world state.  No mutable ECS/asset
        //                      references survive this call.
        //
        //  3. PrepareFrame   — CPU frustum cull, sort, build draw-packet
        //                      lists, upload per-frame staging data.
        //
        //  4. ExecuteFrame   — record and submit GPU command buffers.
        //
        //  5. EndFrame       — release frame-context ownership back to the
        //                      ring.  Returns IDevice::GetGlobalFrameNumber()
        //                      after the device EndFrame call. This is the
        //                      device's post-EndFrame global frame counter,
        //                      not necessarily the just-completed
        //                      frame.FrameIndex.

        [[nodiscard]] virtual bool BeginFrame(RHI::FrameHandle& outFrame) = 0;

        virtual void SubmitRuntimeSnapshots(const RuntimeRenderSnapshotBatch& snapshots) = 0;

        [[nodiscard]] virtual RenderWorld ExtractRenderWorld(
            const RenderFrameInput& input) = 0;

        virtual void PrepareFrame(RenderWorld& world) = 0;

        virtual void ExecuteFrame(const RHI::FrameHandle& frame,
                                  const RenderWorld&      world) = 0;

        [[nodiscard]] virtual std::uint64_t EndFrame(
            const RHI::FrameHandle& frame) = 0;

        // ── Resource managers ─────────────────────────────────────────────
        // Initialised inside Initialize() once IDevice is live.
        // Shutdown() destroys them in dependency order so no manager
        // outlives a resource it references.

        [[nodiscard]] virtual RHI::BufferManager&   GetBufferManager()   = 0;
        [[nodiscard]] virtual RHI::TextureManager&  GetTextureManager()  = 0;
        [[nodiscard]] virtual RHI::SamplerManager&  GetSamplerManager()  = 0;
        [[nodiscard]] virtual RHI::PipelineManager& GetPipelineManager() = 0;
        [[nodiscard]] virtual GpuWorld&              GetGpuWorld()       = 0;
        [[nodiscard]] virtual MaterialSystem&        GetMaterialSystem()  = 0;
        [[nodiscard]] virtual ColormapSystem&        GetColormapSystem()  = 0;
        [[nodiscard]] virtual VisualizationSyncSystem& GetVisualizationSyncSystem() = 0;
        [[nodiscard]] virtual CullingSystem&         GetCullingSystem()   = 0;
        [[nodiscard]] virtual TransformSyncSystem&   GetTransformSyncSystem() = 0;
        [[nodiscard]] virtual LightSystem&           GetLightSystem()     = 0;
        [[nodiscard]] virtual SelectionSystem&       GetSelectionSystem() = 0;
        [[nodiscard]] virtual ForwardSystem&         GetForwardSystem()   = 0;
        [[nodiscard]] virtual DeferredSystem&        GetDeferredSystem()  = 0;
        [[nodiscard]] virtual PostProcessSystem&     GetPostProcessSystem() = 0;
        [[nodiscard]] virtual ShadowSystem&          GetShadowSystem()    = 0;
        [[nodiscard]] virtual const RenderGraphFrameStats& GetLastRenderGraphStats() const = 0;
    };

    export std::unique_ptr<IRenderer> CreateRenderer();
}
