module;

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Graphics.FrameRecipe;

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    export enum class FrameRecipeLightingPath : std::uint8_t
    {
        Forward = 0,
        Deferred,
        Hybrid,
    };

    export enum class FrameRecipeAAMode : std::uint8_t
    {
        NoAA = 0,
        FXAA,
        SMAA,
        TAA,
        ExternalReconstructor,
    };

    export enum class FrameRecipePassKind : std::uint8_t
    {
        Culling = 0,
        Picking,
        DepthPrepass,
        Shadow,
        Surface,
        Composition,
        Line,
        Point,
        PostProcess,
        // GRAPHICS-075 Slice E.1 — the histogram is a *compute* dispatch
        // and Vulkan forbids `vkCmdDispatch` inside an active render-pass
        // scope, so it cannot share the `"PostProcessPass"` umbrella's
        // render-pass scope (which hosts bloom + tonemap fragment work).
        // The histogram therefore lives in its own ordered graph pass
        // declared before `"PostProcessPass"` with
        // `Read(SceneColorHDR, ShaderRead)` +
        // `Write(PostProcess.Histogram, BufferUsage::ShaderWrite)`.
        PostProcessHistogram,
        // GRAPHICS-075 Slice D.2a — the single `"PostProcessAAPass"` graph
        // pass splits into three ordered AA passes so edge / blend / resolve
        // pipelines can target format-incompatible color attachments
        // (`RG8_UNORM` / `RGBA8_UNORM` / backbuffer format). FXAA records
        // under `PostProcessAAResolve` only; SMAA records under all three.
        PostProcessAAEdge,
        PostProcessAABlend,
        PostProcessAAResolve,
        SelectionOutline,
        DebugView,
        ImGui,
        Present,
        // GRAPHICS-077 Slice A — scaffold-only `TransientDebugSurfacePass`
        // that the recipe declares between the lit-composition family and
        // `PostProcessHistogramPass` when `EnableTransientDebugSurface` is
        // set. Slice A holds no pipelines and no helper; the executor
        // branch routes through `RecordTransientDebugSurfacePass(...)` and
        // reports `SkippedUnavailable` on operational devices,
        // `SkippedNonOperational` otherwise. Slice B wires the triangle
        // lane, Slice C wires line + point, Slice D is the opt-in
        // `gpu;vulkan` smoke. Append-only at the end of the enum to keep
        // prior numeric values stable.
        TransientDebugSurface,
        // GRAPHICS-078 Slice A — scaffold-only `VisualizationOverlayPass`
        // that the recipe declares immediately after
        // `TransientDebugSurfacePass` (and before
        // `PostProcessHistogramPass`) when `EnableVisualizationOverlay`
        // is set. Same fail-closed shape as the GRAPHICS-077 transient-
        // debug pass: Slice A holds no pipelines and no helper; the
        // executor branch routes through
        // `RecordVisualizationOverlayPass(...)` and reports
        // `SkippedUnavailable` on operational devices,
        // `SkippedNonOperational` otherwise. Slice B wires the
        // vector-field lane, Slice C wires the isoline lane, Slice D is
        // the opt-in `gpu;vulkan` smoke. Append-only at the end of the
        // enum to keep prior numeric values stable.
        VisualizationOverlay,
        // GRAPHICS-038B — compute HZB build pass. Declared after
        // `DepthPrepass`, reads `SceneDepth` as a shader input, and writes
        // the renderer-owned retained `HZB.Current` import. Append-only to
        // keep prior numeric values stable.
        HZBBuild,
        // GRAPHICS-039A — compute cluster-grid build pass. Declared after
        // the depth/HZB build band when enabled, writes the renderer-owned
        // `ClusterGrid.AABBs` storage-buffer import, and records the froxel
        // AABB build command shape. Append-only to keep prior numeric values
        // stable.
        ClusterGridBuild,
        // GRAPHICS-039B — compute light-to-cluster assignment pass. Declared
        // after `ClusterGridBuildPass`, reads `ClusterGrid.AABBs` plus the
        // snapshot-authored `GpuWorld.Lights` buffer, and writes the packed
        // `ClusterLights.Headers` / `ClusterLights.Indices` imports plus the
        // shader-visible `ClusterLights.Counter` atomic allocator.
        LightClusterAssignment,
        // GRAPHICS-040C — compute-style temporal reconstruction pass. Declared
        // after lit HDR composition/debug overlays and before postprocess so
        // TAA/external reconstruction can read jittered HDR/depth/motion plus
        // retained history and publish a reconstructed HDR input for tonemap.
        Reconstruction,
    };

    export enum class FrameRecipeResourceKind : std::uint8_t
    {
        Backbuffer = 0,
        SceneDepth,
        EntityId,
        PrimitiveId,
        SceneNormal,
        Albedo,
        Material0,
        SceneColorHDR,
        ShadowAtlas,
        SceneColorLDR,
        SelectionOutline,
        DebugViewRGBA,
        SceneTable,
        InstanceStatic,
        InstanceDynamic,
        EntityConfig,
        GeometryRecords,
        Bounds,
        Lights,
        MaterialBuffer,
        SurfaceOpaqueIndexedArgs,
        SurfaceOpaqueCount,
        LinesIndexedArgs,
        LinesCount,
        PointsNonIndexedArgs,
        PointsCount,
        PickingReadback,
        PostProcessBloomScratch,
        PostProcessHistogram,
        // GRAPHICS-075 Slice D.2a — the single `PostProcess.AATemp`
        // transient splits into three matched-format AA attachments so
        // edge / blend / resolve graph passes can each Write a single
        // matched-format color attachment. The resolved attachment is the
        // backbuffer-format target that `presentSource` flips to when AA
        // is enabled.
        PostProcessAATempEdges,
        PostProcessAATempWeights,
        PostProcessAATempResolved,
        // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
        // `Histogram.Readback` buffer imported through
        // `FrameRecipeImports::HistogramReadback`. The recipe authorises the
        // `"PostProcessHistogramPass"` executor branch to record a
        // `CopyBuffer(PostProcess.Histogram → Histogram.Readback @ slot * 1024)`
        // after the compute dispatch; the renderer drains completed slots on
        // `BeginFrame()` and forwards the 256-bin payload to
        // `PostProcessSystem::PublishHistogramReadback(...)`.
        HistogramReadback,
        // GRAPHICS-038B — renderer-owned retained HZB write target imported
        // from `HZBSystem::CurrentHZB()`.
        HZBCurrent,
        // GRAPHICS-039A — renderer-owned cluster AABB storage buffer imported
        // from the clustered-light grid owner. The build pass writes one
        // right-handed view-space AABB per froxel cell.
        ClusterGridAABBs,
        // GRAPHICS-039B — renderer-owned clustered-light assignment outputs.
        // `ClusterLights.Headers` stores `{ offset, count }` per froxel cell;
        // `ClusterLights.Indices` stores the packed uint light-index lists;
        // `ClusterLights.Counter` stores the shader atomic allocator cursor.
        ClusterLightHeaders,
        ClusterLightIndices,
        ClusterLightCounter,
        // GRAPHICS-040A — framegraph-managed screen-space motion-vector
        // color target. The engine RHI name for `R16G16_SFLOAT` is
        // `RG16_FLOAT`.
        MotionVectors,
        // GRAPHICS-040C — retained reconstruction history imports plus the
        // frame-transient reconstructed HDR target consumed by postprocess.
        ReconstructionHistoryPrevious,
        ReconstructionHistoryCurrent,
        ReconstructionResolvedHDR,
    };

    export struct FrameRecipeFeatures
    {
        FrameRecipeLightingPath LightingPath{FrameRecipeLightingPath::Deferred};
        bool EnableDepthPrepass{true};
        // GRAPHICS-038B — opt into the HZB build pass when the renderer has a
        // valid retained `HZB.Current` import. Defaults off so standalone
        // recipe descriptions remain behavior-preserving unless the renderer
        // or a focused contract test explicitly enables the HZB slice.
        bool EnableHZBBuild{false};
        // GRAPHICS-039A — opt into the cluster-grid AABB build pass when the
        // renderer has a valid `ClusterGrid.AABBs` import. Defaults off until
        // the clustered-light consumers land.
        bool EnableClusterGridBuild{false};
        // GRAPHICS-039B — opt into the light-to-cluster assignment pass once
        // the renderer has valid header/index imports. Requires
        // `EnableClusterGridBuild` because the assignment consumes the same
        // frame's cluster AABBs.
        bool EnableClusterLightAssignment{false};
        bool EnablePicking{false};
        bool EnableShadows{false};
        bool EnableSelectionOutline{false};
        bool EnableDebugView{false};
        bool EnablePostProcess{true};
        // GRAPHICS-075 Slice D.2a — spatial-AA present routing gate. The
        // renderer sets this only when the selected spatial mode's pipelines
        // are available, so present consumes `PostProcess.AATemp.Resolved` for
        // usable FXAA/SMAA output and falls back to `SceneColorLDR` otherwise.
        bool EnableAntiAliasing{false};
        bool EnableImGui{true};
        // GRAPHICS-077 Slice A — recipe-side gate for the new
        // `TransientDebugSurfacePass`. Derived in
        // `DeriveDefaultFrameRecipeFeatures(...)` from
        // `!world.DebugPrimitives.Lines.empty() ||
        // !world.DebugPrimitives.Points.empty() ||
        // !world.DebugPrimitives.Triangles.empty()` so the pass is
        // omitted entirely from `RenderGraphFrameStats::CommandRecords`
        // when no transient debug primitives exist for the frame. Slice A
        // has no pipelines and no helper; Slices B/C wire the per-lane
        // pipelines + upload + recording paths.
        bool EnableTransientDebugSurface{false};
        // GRAPHICS-078 Slice A — recipe-side gate for the new
        // `VisualizationOverlayPass`. Derived in
        // `DeriveDefaultFrameRecipeFeatures(...)` from
        // `!world.Visualization.VectorFields.empty() ||
        // !world.Visualization.Isolines.empty()` so the pass is omitted
        // entirely from `RenderGraphFrameStats::CommandRecords` when no
        // visualization overlay packets exist for the frame. Slice A
        // has no pipelines and no helper; Slices B/C wire the per-kind
        // pipelines + upload + recording paths.
        bool EnableVisualizationOverlay{false};
    };

    export struct FrameRecipeTemporalOptions
    {
        // GRAPHICS-040A — golden-image determinism mode. The jitter helper
        // consumes the matching camera flag; the recipe suppresses temporal
        // motion/history surfaces while this is set.
        bool NoJitterNoHistory{false};
        // GRAPHICS-040A/040C — explicit opt-in for non-selector motion-vector
        // consumers; temporal AA/reconstructor modes also force this target.
        bool EnableMotionVectors{false};
    };

    export struct FrameRecipeAAOptions
    {
        // GRAPHICS-040C — explicit recipe selector. `NoAA` is the default,
        // `FXAA` compiles only the resolve pass, `SMAA` compiles edge/blend/
        // resolve, and temporal modes compile `ReconstructionPass` plus
        // motion/history resources before postprocess.
        FrameRecipeAAMode Mode{FrameRecipeAAMode::NoAA};
        // Optional pre-reconstruction render resolution. Zero means "match
        // output", preserving existing call sites while allowing temporal
        // reconstruction to declare input/output extent splits.
        std::uint32_t InputWidth{0u};
        std::uint32_t InputHeight{0u};
        // Retained reconstruction history ping-pong images supplied by the
        // renderer when `Mode` is temporal.
        RHI::TextureHandle ReconstructionHistoryPrevious{};
        RHI::TextureHandle ReconstructionHistoryCurrent{};
    };

    export struct FrameRecipeSizing
    {
        std::uint32_t Width{1u};
        std::uint32_t Height{1u};
        RHI::Format BackbufferFormat{RHI::Format::RGBA8_UNORM};
        RHI::Format DepthFormat{RHI::Format::D32_FLOAT};
    };

    export struct FrameRecipeImports
    {
        RHI::TextureHandle Backbuffer{};
        RHI::BufferHandle SceneTable{};
        RHI::BufferHandle InstanceStatic{};
        RHI::BufferHandle InstanceDynamic{};
        RHI::BufferHandle EntityConfig{};
        RHI::BufferHandle GeometryRecords{};
        RHI::BufferHandle Bounds{};
        RHI::BufferHandle Lights{};
        RHI::BufferHandle MaterialBuffer{};
        RHI::BufferHandle SurfaceOpaqueIndexedArgs{};
        RHI::BufferHandle SurfaceOpaqueCount{};
        RHI::BufferHandle LinesIndexedArgs{};
        RHI::BufferHandle LinesCount{};
        RHI::BufferHandle PointsNonIndexedArgs{};
        RHI::BufferHandle PointsCount{};
        // GRAPHICS-073 Slice B — optional `ShadowSystem`-owned atlas. When
        // valid, `BuildDefaultFrameRecipe` imports this texture as the
        // ShadowAtlas resource instead of allocating a transient depth target.
        // When invalid, the recipe falls back to the GRAPHICS-073 Slice A
        // transient path (`graph.CreateTexture("ShadowAtlas", ...)`).
        RHI::TextureHandle ShadowAtlas{};
        // GRAPHICS-074 Slice D.2 — renderer-owned host-visible
        // `Picking.Readback` buffer (allocated by Slice D.1's
        // `InitializeOperationalPassResources`). When `pickingActive` is true
        // (`EnablePicking && EnableDepthPrepass`), `BuildDefaultFrameRecipe`
        // imports this handle as the canonical destination for the
        // `PickingPass` texture-to-buffer copies via `ImportBuffer(...,
        // BufferState::TransferDst, BufferState::HostReadback)`. The handle
        // must be valid when `pickingActive` is true; the renderer always
        // populates it from `m_PickingReadbackBuffer` and the picking
        // feature itself requires an operational publisher to have wired
        // the buffer first.
        RHI::BufferHandle PickingReadback{};
        // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
        // `Histogram.Readback` buffer (allocated by Slice E.2's
        // `InitializeOperationalPassResources`). Sized for
        // `1024 * frames-in-flight` bytes (256 uint32 bins per slot, one slot
        // per in-flight frame). When `EnablePostProcess` is true and the
        // handle is valid, `BuildDefaultFrameRecipe` imports it as the
        // destination of the per-frame
        // `CopyBuffer(PostProcess.Histogram → Histogram.Readback)` recorded
        // by the `"PostProcessHistogramPass"` executor branch.
        RHI::BufferHandle HistogramReadback{};
        // GRAPHICS-038B — retained HZB write target owned by HZBSystem.
        RHI::TextureHandle HZBCurrent{};
        // GRAPHICS-039A — retained cluster AABB storage buffer written by the
        // cluster-grid build pass and read by the future light-assignment pass.
        RHI::BufferHandle ClusterGridAABBs{};
        // GRAPHICS-039B — retained clustered-light assignment outputs.
        RHI::BufferHandle ClusterLightHeaders{};
        RHI::BufferHandle ClusterLightIndices{};
        RHI::BufferHandle ClusterLightCounter{};
    };

    // GRAPHICS-073 Slice B — typed sizing seam for the shadow atlas. When
    // populated, `BuildDefaultFrameRecipe` derives the atlas dimensions from
    // `(AtlasResolution * CascadeCount, AtlasResolution)` rather than the
    // viewport-sized `FrameRecipeSizing` fallback inherited from Slice A.
    // Leaving `AtlasResolution = 0` keeps the viewport-sized transient
    // fallback so headless contract tests need not plumb a `ShadowSystem`.
    export struct FrameRecipeShadowSizing
    {
        std::uint32_t AtlasResolution{0u};
        std::uint32_t CascadeCount{0u};
    };

    export struct FrameRecipePassDeclaration
    {
        FrameRecipePassKind Kind{FrameRecipePassKind::Culling};
        std::string_view Name{};
        bool Enabled{false};
        bool FinalizesBackbuffer{false};
        std::vector<std::string_view> Reads{};
        std::vector<std::string_view> Writes{};
    };

    export struct FrameRecipeResourceDeclaration
    {
        FrameRecipeResourceKind Kind{FrameRecipeResourceKind::Backbuffer};
        std::string_view Name{};
        bool Enabled{false};
        bool Imported{false};
        bool Backbuffer{false};
        bool Optional{false};
        bool ImportedWriteAllowed{false};
    };

    export struct FrameRecipeIntrospection
    {
        std::vector<FrameRecipePassDeclaration> Passes{};
        std::vector<FrameRecipeResourceDeclaration> Resources{};
    };

    export struct FrameRecipeBuildResult
    {
        bool Succeeded{false};
        std::uint32_t DeclaredPassCount{0u};
        std::uint32_t DeclaredResourceCount{0u};
        std::uint32_t MissingPrerequisiteCount{0u};
        std::string Diagnostic{};
    };

    export [[nodiscard]] FrameRecipeFeatures DeriveDefaultFrameRecipeFeatures(const RenderWorld& world);

    export [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features);
    export [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features,
                                                                             FrameRecipeTemporalOptions temporalOptions);
    export [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features,
                                                                             const FrameRecipeAAOptions& aaOptions,
                                                                             FrameRecipeTemporalOptions temporalOptions = {});

    export [[nodiscard]] RenderGraphValidationResult ValidateRecipeCompiledGraph(
        const FrameRecipeIntrospection& recipe,
        const CompiledRenderGraph& compiled);

    export [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeShadowSizing& shadowSizing = {});
    export [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeShadowSizing& shadowSizing,
                                                                        FrameRecipeTemporalOptions temporalOptions);
    export [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeAAOptions& aaOptions,
                                                                        const FrameRecipeShadowSizing& shadowSizing = {},
                                                                        FrameRecipeTemporalOptions temporalOptions = {});
}
