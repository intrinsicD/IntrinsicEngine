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
        PostProcessAATemp,
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
                                                                        const FrameRecipeSizing& sizing);

    export [[nodiscard]] FrameRecipeBuildResult BuildMinimalDebugSurfaceRecipe(RenderGraph& graph,
                                                                               const FrameRecipeImports& imports,
                                                                               const FrameRecipeSizing& sizing);
}
