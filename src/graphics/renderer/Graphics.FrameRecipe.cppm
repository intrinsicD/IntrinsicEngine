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
    };

    export struct FrameRecipeFeatures
    {
        FrameRecipeLightingPath LightingPath{FrameRecipeLightingPath::Deferred};
        bool EnableDepthPrepass{true};
        bool EnablePicking{false};
        bool EnableShadows{false};
        bool EnableSelectionOutline{false};
        bool EnableDebugView{false};
        bool EnablePostProcess{true};
        // GRAPHICS-075 Slice D.2a — when set, `BuildDefaultFrameRecipe`
        // flips `presentSource` from `SceneColorLDR` to
        // `PostProcess.AATemp.Resolved` so the AA-resolved color reaches
        // present. Recipe-build keeps allocating the three AA transients
        // unconditionally (their allocation is gated on `EnablePostProcess`),
        // but the present routing only consumes the resolved target when
        // `PostProcessSettings::AntiAliasing != None`. The renderer derives
        // this flag from `PostProcessSystem::GetSettings().AntiAliasing`.
        bool EnableAntiAliasing{false};
        bool EnableImGui{true};
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

    // GRAPHICS-032A — stable label for the opt-in minimal-debug-surface recipe.
    // Tests and diagnostics match this label rather than transient allocator IDs.
    export inline constexpr std::string_view kMinimalDebugSurfaceRecipeLabel = "recipe.minimal-debug-surface";

    // GRAPHICS-032A — stable pass labels for the minimal-debug-surface recipe.
    // The two passes are the only recording passes in this recipe.
    export inline constexpr std::string_view kMinimalDebugSurfacePassName = "Pass.Surface.MinimalDebug";
    export inline constexpr std::string_view kMinimalDebugPresentPassName = "Pass.Present.MinimalDebug";

    export [[nodiscard]] FrameRecipeFeatures DeriveDefaultFrameRecipeFeatures(const RenderWorld& world);

    export [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features);

    export [[nodiscard]] FrameRecipeIntrospection DescribeMinimalDebugSurfaceRecipe();

    export [[nodiscard]] RenderGraphValidationResult ValidateRecipeCompiledGraph(
        const FrameRecipeIntrospection& recipe,
        const CompiledRenderGraph& compiled);

    export [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeShadowSizing& shadowSizing = {});

    export [[nodiscard]] FrameRecipeBuildResult BuildMinimalDebugSurfaceRecipe(RenderGraph& graph,
                                                                               const FrameRecipeImports& imports,
                                                                               const FrameRecipeSizing& sizing);
}
