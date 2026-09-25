// The renderer facade: `IRenderer`'s frame lifecycle, subsystem accessors, the
// typed `RendererPipelineId` pipeline queries, the runtime snapshot batch it
// consumes, and the frame command-hook seam. Diagnostics, recipe-override and
// frame-recipe owners are re-exported because this interface names them.
module;

#include <cstdint>

#include <glm/fwd.hpp>

export module Extrinsic.Graphics.Renderer;

import Extrinsic.Core.Std;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Graphics.GpuWorld;
export import Extrinsic.Graphics.UvView;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.HZB;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
export import Extrinsic.Graphics.RenderingContract;
export import Extrinsic.Graphics.RenderCommandRouter;
// Canonical owners of the renderer's data contracts, re-exported because they
// appear directly in this interface: the per-frame diagnostics record returned
// by `GetLastRenderGraphStats()`, the config-lane `FrameRecipeOverride` the
// override seam accepts, and the frame-recipe vocabulary
// (`FrameRecipeLightingPath`) the lighting-path seam names.
export import Extrinsic.Graphics.RenderDiagnostics;
export import Extrinsic.Graphics.RenderRecipeConfig;
export import Extrinsic.Graphics.FrameRecipe;
// RUNTIME-082 Slice D — `RuntimeRenderSnapshotBatch` carries spans of
// `SpatialDebugAabb` / `SpatialDebugHierarchyNode` / `SpatialDebugSplitPlane`
// / `SpatialDebugWireEdge` produced by the runtime spatial-debug adapter
// pump. The types live in `Extrinsic.Graphics.SpatialDebugVisualizers`; the
// import is local (non-export) because the existing graphics consumers that
// build the matching wireframe packets already import the module directly.
import Extrinsic.Graphics.SpatialDebugVisualizers;

namespace Extrinsic::Graphics
{
    export using RuntimeFrameCommandHook =
        Core::Std::function<void(RHI::ICommandContext&)>;

    export struct RuntimeFrameCommandHookHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeFrameCommandHookHandle,
            RuntimeFrameCommandHookHandle) noexcept = default;
    };

    // SpatialDebugAabb's required owner supplies the vec3 definition. The forward
    // header supplies its name; span still requires a complete element type here.
    static_assert(sizeof(glm::vec3) > 0);

    export struct RuntimeRenderSnapshotBatch
    {
        Core::Std::span<const TransformSyncRecord>     Transforms{};
        Core::Std::span<const LightSnapshot>           Lights{};
        Core::Std::span<const VisualizationSyncRecord> Visualizations{};
        // Payload bytes are read only during `SubmitRuntimeSnapshots`; the
        // renderer keeps descriptor metadata and resident GPU addresses, not
        // the bytes, so producers may borrow payload storage for the call.
        Core::Std::span<const VisualizationPropertyBufferUploadDescriptor> VisualizationPropertyBuffers{};
        Core::Std::span<const VisualizationAttributeBufferPacket> VisualizationAttributeBuffers{};
        Core::Std::span<const ScalarAttributePacket>              VisualizationScalars{};
        Core::Std::span<const ColorAttributePacket>               VisualizationColors{};
        Core::Std::span<const VectorFieldOverlayPacket>           VisualizationVectorFields{};
        Core::Std::span<const IsolineOverlayPacket>               VisualizationIsolines{};
        Core::Std::span<const HtexPatchPreviewAtlasPacket>        VisualizationHtexAtlases{};
        Core::Std::span<const FragmentBakeAtlasPacket>            VisualizationFragmentBakeAtlases{};
        Core::Std::span<const DebugLinePacket>         DebugLines{};
        Core::Std::span<const DebugPointPacket>        DebugPoints{};
        Core::Std::span<const DebugTrianglePacket>     DebugTriangles{};
        Core::Std::span<const TransformGizmoRenderPacket> TransformGizmos{};

        // Spatial-debug snapshot spans. Consumers that build wireframe
        // packets feed these into
        // `BuildSpatialDebug{Bounds,Hierarchy,SplitPlane,ConvexHull,...}Wireframes`.
        //
        // `RUNTIME-199` retired the runtime adapter registry that used to
        // populate these spans, so no producer is currently composed and they
        // stay default-empty. The packet contract is deliberately preserved:
        // a feature that owns a concrete spatial-debug record (for example
        // `RUNTIME-189`) fills them directly rather than through a generic
        // adapter registry.
        Core::Std::span<const SpatialDebugAabb>          SpatialDebugBounds{};
        Core::Std::span<const SpatialDebugHierarchyNode> SpatialDebugHierarchyNodes{};
        Core::Std::span<const SpatialDebugSplitPlane>    SpatialDebugSplitPlanes{};
        Core::Std::span<const glm::vec3>                 SpatialDebugConvexHullVertices{};
        Core::Std::span<const SpatialDebugWireEdge>      SpatialDebugConvexHullEdges{};
        Core::Std::span<const glm::vec3>                 SpatialDebugPointMarkers{};

        // RUNTIME-089 Slice B — runtime/editor selection snapshot identity,
        // aggregated by `RenderExtractionCache::ExtractAndSubmit` from the
        // runtime-owned `SelectionController`. `SubmitRuntimeSnapshots` copies
        // these into stable renderer storage and `ExtractRenderWorld` surfaces
        // them as `RenderWorld::Selection` so `SelectionOutlinePass` can outline
        // selected/hovered renderables without graphics reading live ECS. The
        // identity-only fields (selected ids, hovered id, has-hovered) are
        // filled here; the outline styling on `SelectionSnapshot` keeps its
        // recipe defaults. Default-empty when no controller is wired.
        Core::Std::span<const std::uint32_t> SelectionSelectedStableIds{};
        std::uint32_t                  SelectionHoveredStableId{0u};
        bool                           SelectionHasHovered{false};
    };

    // Identifies a pipeline the renderer publishes to external callers.
    // `Count` is a bound, not a pipeline: it is unmapped and fails closed like
    // any other value outside this list. Pipelines the renderer keeps entirely
    // private (for example, depth prepass or present) carry no identifier.
    export enum class RendererPipelineId : std::uint8_t
    {
        DefaultDebugSurface,
        ForwardSurface,
        ForwardLine,
        ForwardPoint,
        Shadow,
        DeferredGBuffer,
        DeferredLighting,
        SelectionEntityId,
        SelectionEntityIdOutline,
        SelectionFaceId,
        SelectionEdgeId,
        SelectionPointId,
        SelectionOutline,
        PostProcessToneMap,
        PostProcessBloomDownsample,
        PostProcessBloomUpsample,
        PostProcessFXAA,
        PostProcessSMAAEdge,
        PostProcessSMAABlend,
        PostProcessSMAAResolve,
        PostProcessHistogram,
        HZBBuild,
        ClusterGridBuild,
        ClusterLightAssignment,
        Count,
    };

    export extern "C++"
    {
    class IRenderer
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

        // Runtime snapshot payloads are copied into renderer-owned stable
        // storage. `storageSlot` is a small caller-owned ring index (slot 0 is
        // the legacy/default path); pipelined extraction passes the
        // RenderWorldPool back slot here and later extracts from the acquired
        // front/previous-front slot. The RuntimeRenderSnapshotBatch shape stays
        // unchanged: only the renderer-side retained lifetime is multi-buffered.
        virtual void SubmitRuntimeSnapshots(const RuntimeRenderSnapshotBatch& snapshots,
                                            std::uint32_t storageSlot = 0u) = 0;

        // GRAPHICS-079 Slice A — runtime composition hands the engine-owned
        // `ImGuiOverlaySystem` (the producer the `RUNTIME-090` adapter submits
        // to) to the renderer so the consumer-side `Pass.ImGui` reads the same
        // overlay state. Runtime owns composition; this is the allowed
        // `runtime -> graphics` handoff edge. Passing `nullptr` detaches the
        // overlay so the `ImGuiPass` route reports `SkippedUnavailable`.
        virtual void SetImGuiOverlaySystem(ImGuiOverlaySystem* overlay) noexcept = 0;
        // GRAPHICS-079 Slice B — diagnostic observer for runtime composition
        // tests. It reports whether the renderer currently has an ImGui
        // consumer bound to a runtime-owned overlay; it does not expose the
        // borrowed overlay or allow graphics to call back into runtime.
        [[nodiscard]] virtual bool HasImGuiOverlaySystem() const noexcept = 0;

        // Runtime-owned GPU work that must record inside the renderer's frame
        // command context without creating a second swapchain present. Graphics
        // owns the invocation point; callbacks run in registration order, record
        // only RHI commands, must not retain the borrowed command context, and
        // should unregister outside the recording callback.
        [[nodiscard]] virtual RuntimeFrameCommandHookHandle
            RegisterRuntimeFrameCommandHook(RuntimeFrameCommandHook hook) = 0;
        virtual void UnregisterRuntimeFrameCommandHook(
            RuntimeFrameCommandHookHandle handle) noexcept = 0;

        [[nodiscard]] virtual RenderWorld ExtractRenderWorld(
            const RenderFrameInput& input,
            std::uint32_t storageSlot = 0u) = 0;

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
        [[nodiscard]] virtual SelectionSystem&       GetSelectionSystem() = 0;
        [[nodiscard]] virtual PostProcessSystem&     GetPostProcessSystem() = 0;
        [[nodiscard]] virtual ShadowSystem&          GetShadowSystem()    = 0;
        [[nodiscard]] virtual HZBSystem&             GetHZBSystem()       = 0;
        [[nodiscard]] virtual const RenderGraphFrameStats& GetLastRenderGraphStats() const = 0;
        virtual void SetTransientAliasingEnabled(bool enabled) noexcept = 0;
        [[nodiscard]] virtual bool IsTransientAliasingEnabled() const noexcept = 0;
        virtual void SetRenderGraphDebugDumpEnabled(bool enabled) noexcept = 0;
        [[nodiscard]] virtual bool GetRenderGraphDebugDumpEnabled() const noexcept = 0;
        virtual void SetParallelRenderGraphRecordingEnabled(bool enabled) noexcept = 0;
        [[nodiscard]] virtual bool IsParallelRenderGraphRecordingEnabled() const noexcept = 0;

        // Device-side handle for a published pipeline. Invalid when `id` is
        // unmapped, when the renderer has no pipeline manager yet, or when the
        // operational device path has not published that lease — callers must
        // treat an invalid handle as "not available this frame", not as an
        // error. This query never builds a descriptor, so hot paths can call it
        // per frame.
        [[nodiscard]] virtual RHI::PipelineHandle GetPipeline(
            RendererPipelineId id) const noexcept = 0;

        // Canonical descriptor a published pipeline is compiled from,
        // independent of whether its lease exists, so contract tests can assert
        // byte-identical republish across InitializeOperationalPassResources()
        // and RebuildOperationalResources(). `nullopt` means `id` is unmapped:
        // `RHI::PipelineDesc` has no invalid state a returned value could carry.
        [[nodiscard]] virtual Core::Std::optional<RHI::PipelineDesc> GetPipelineDesc(
            RendererPipelineId id) const noexcept = 0;

        // GRAPHICS-074 (Slice D.1) — accessor for the renderer-owned host-
        // visible `Picking.Readback` buffer. The buffer is sized for
        // `8 * frames-in-flight` bytes (one 4-byte `EntityId` word + one
        // 4-byte `EncodedSelectionId` word per in-flight frame slot per
        // `GRAPHICS-012Q`'s `EncodedSelectionId` payload layout), allocated
        // with `HostVisible = true` and `BufferUsage::TransferDst` so the
        // recipe (Slice D.2) can import it as the destination of a
        // `CopyTextureToBuffer(EntityId/PrimitiveId, ...)` after the
        // selection-ID sub-passes record, and the renderer (Slice D.3) can
        // map it on `BeginFrame()` once the issuing frame has completed.
        // Handle is invalid until an operational device path allocates the
        // lease; size accessor returns the allocation size in bytes (0 when
        // the buffer is not yet allocated). Slices D.2/D.3 wire the
        // recipe-side import, per-frame copy, drain, and
        // `PublishPickResult` / `PublishNoHit` routing.
        [[nodiscard]] virtual RHI::BufferHandle GetPickingReadbackBuffer() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t GetPickingReadbackBufferSize() const noexcept = 0;

        // GRAPHICS-075 Slice E.2 — accessor for the renderer-owned host-
        // visible `Histogram.Readback` buffer. Sized for
        // `1024 * frames-in-flight` bytes (256 uint32 bins per slot, one
        // slot per in-flight frame), allocated with `HostVisible = true`
        // and `BufferUsage::TransferDst` so the recipe can import it as the
        // destination of a `CopyBuffer(PostProcess.Histogram →
        // Histogram.Readback)` after the histogram dispatch, and the
        // renderer can map it on `BeginFrame()` once the issuing frame has
        // completed. Handle is invalid until an operational device path
        // allocates the lease; size accessor returns the allocation size in
        // bytes (0 when the buffer is not yet allocated). The drain
        // publishes the 256-bin payload to
        // `PostProcessSystem::PublishHistogramReadback(...)`.
        [[nodiscard]] virtual RHI::BufferHandle GetHistogramReadbackBuffer() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t GetHistogramReadbackBufferSize() const noexcept = 0;

        // GRAPHICS-072 (Slice A) — test seam for the default recipe's runtime
        // lighting path. `DeriveDefaultFrameRecipeFeatures` derives a default
        // of `Forward` so the legacy contract tests stay green; the renderer
        // overrides that derivation with this stored value when set to
        // `Deferred` or `Hybrid`, allowing contract tests to drive the
        // deferred surface/composition executor branches without re-deriving
        // features at the call site. Default is `Forward`.
        virtual void SetLightingPath(FrameRecipeLightingPath path) noexcept = 0;
        [[nodiscard]] virtual FrameRecipeLightingPath GetLightingPath() const noexcept = 0;

        // GRAPHICS-106 — fail-closed recipe override seam. The renderer keeps
        // `FrameRecipe*` as the live frame driver; this wrapper carries a
        // validated `RenderRecipeDescriptor` plus explicit optional-slot
        // disables from config/UI/agent lanes. Applying an override can only
        // turn off optional feature gates at the default-recipe build site.
        virtual void SetActiveFrameRecipeOverride(
            Core::Std::optional<FrameRecipeOverride> recipeOverride) = 0;
        virtual void ClearActiveFrameRecipeOverride() noexcept = 0;
        [[nodiscard]] virtual const Core::Std::optional<FrameRecipeOverride>&
        GetActiveFrameRecipeOverride() const noexcept = 0;

        // GRAPHICS-076E — opt-in backbuffer-to-host readback wiring for the
        // canonical default recipe's visible-triangle parity harness. The
        // caller owns the buffer lifetime and passes a raw HostVisible +
        // TransferDst handle. The renderer records the
        // Present→TransferSrc→CopyTextureToBuffer→Present triplet when the
        // device is operational. Calling with an invalid handle disables the
        // path (default post-Initialize state).
        virtual void SetDefaultRecipeBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetDefaultRecipeBackbufferReadbackBuffer() const noexcept = 0;

        // GRAPHICS-077E — opt-in backbuffer-to-host readback wiring for
        // transient-debug pixel parity. The caller owns the HostVisible +
        // TransferDst buffer lifetime. The renderer records the copy triplet
        // only on operational frames where `TransientDebugSurfacePass`
        // recorded; invalid handle disables the path.
        virtual void SetTransientDebugBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetTransientDebugBackbufferReadbackBuffer() const noexcept = 0;

        // GRAPHICS-078E — opt-in backbuffer-to-host readback wiring for
        // visualization-overlay pixel parity. The caller owns the
        // HostVisible + TransferDst buffer lifetime. The renderer records the
        // copy triplet only on operational frames where
        // `VisualizationOverlayPass` recorded; invalid handle disables the
        // path.
        virtual void SetVisualizationOverlayBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetVisualizationOverlayBackbufferReadbackBuffer() const noexcept = 0;

        // GRAPHICS-076 Slice B — public seam for the renderer-owned
        // `DebugViewSystem`'s `RequestedResourceName` setting. Runtime /
        // editor callers translate UI selections into canonical
        // `FrameRecipeIntrospection::Resources[i].Name` keys (per the
        // GRAPHICS-013BQ §"UI-name to FrameRecipeIntrospection mapping"
        // decision) and pass them in here; contract tests use this seam
        // to force the fallback path so the
        // `RenderGraphFrameStats::DebugViewFallbackInvocationCount`
        // diagnostic can be observed deterministically. The `Enabled`
        // field is driven by the renderer from
        // `world.DebugOverlayEnabled || world.DebugPrimitives.HasTransientDebug`
        // each frame (so a stale `Enabled = true` from this setter
        // cannot keep the pass live across frames where the world has
        // turned the overlay off); callers should treat
        // `RequestedResourceName` as the durable field they control.
        virtual void SetDebugViewRequestedResourceName(Core::Std::string name) = 0;

        [[nodiscard]] virtual Core::Std::string GetDebugViewRequestedResourceName() const = 0;

        // GRAPHICS-122 — copied runtime/editor request for the single retained
        // UV-space target. Appended to minimise polymorphic slot churn.
        // Backends that do not implement the optional view retain the default
        // honest CPU-layout fallback.
        virtual void SubmitUvViewRequest(UvViewRequest request);
        [[nodiscard]] virtual UvViewOutput GetUvViewOutput() const;
    };
    }

    export Core::Std::unique_ptr<IRenderer> CreateRenderer();
}
