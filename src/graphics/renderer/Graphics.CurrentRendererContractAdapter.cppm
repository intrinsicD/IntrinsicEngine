module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.CurrentRendererContractAdapter;

import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.RenderWorld;

namespace Extrinsic::Graphics
{
    export inline constexpr std::string_view kCurrentRendererContractId =
        "extrinsic.graphics.current-renderer";
    export inline constexpr std::string_view kCurrentRendererSnapshotProducerId =
        "extrinsic.runtime.render-extraction";
    export inline constexpr std::string_view kCurrentRendererDefaultSnapshotId =
        "current-renderer.frame-snapshot";
    export inline constexpr std::string_view kCurrentRendererDefaultRecipeId =
        "current-renderer.default-frame-recipe";
    export inline constexpr std::string_view kCurrentRendererDefaultViewRecipeId =
        "current-renderer.default-view-output";

    export struct CurrentRendererSnapshotOptions
    {
        std::string SnapshotId{std::string{kCurrentRendererDefaultSnapshotId}};
        SnapshotKind Kind{SnapshotKind::FullScene};
        SnapshotScope Scope{SnapshotScope::FullScene};
        std::uint64_t FrameIndex{0u};
    };

    export struct CurrentRendererOutputOptions
    {
        std::string RecipeId{std::string{kCurrentRendererDefaultViewRecipeId}};
        OutputTargetKind Target{OutputTargetKind::Window};
        InteractionMode Mode{InteractionMode::Interactive};
        bool CaptureRequested{false};
        bool ReadbackRequested{false};
        bool IncludePickingOutputs{false};
        bool IncludeMetricsOutput{false};
    };

    export struct CurrentRendererContract
    {
        RendererDescriptor Renderer{};
        SnapshotEnvelope Snapshot{};
        BindingSet Bindings{};
        RenderRecipeDescriptor Recipe{};
        ViewOutputRecipeDescriptor ViewOutput{};
        RenderingContractValidationResult Diagnostics{};
    };

    export [[nodiscard]] RendererDescriptor MakeCurrentRendererDescriptor();
    export [[nodiscard]] BindingSet MakeCurrentRendererBindingSet();
    export [[nodiscard]] RenderRecipeDescriptor MakeCurrentRendererRecipeDescriptor();

    export [[nodiscard]] SnapshotEnvelope MakeCurrentRendererSnapshotEnvelope(
        const RenderFrameInput& input,
        const CurrentRendererSnapshotOptions& options = {});
    export [[nodiscard]] SnapshotEnvelope MakeCurrentRendererSnapshotEnvelope(
        const RenderWorld& world,
        const CurrentRendererSnapshotOptions& options = {});

    export [[nodiscard]] ViewOutputRecipeDescriptor MakeCurrentRendererViewOutputRecipe(
        const RenderFrameInput& input,
        const CurrentRendererOutputOptions& options = {});
    export [[nodiscard]] ViewOutputRecipeDescriptor MakeCurrentRendererViewOutputRecipe(
        const RenderWorld& world,
        const CurrentRendererOutputOptions& options = {});

    export [[nodiscard]] CurrentRendererContract MakeCurrentRendererContract(
        const RenderFrameInput& input,
        const CurrentRendererSnapshotOptions& snapshotOptions = {},
        const CurrentRendererOutputOptions& outputOptions = {});
    export [[nodiscard]] CurrentRendererContract MakeCurrentRendererContract(
        const RenderWorld& world,
        const CurrentRendererSnapshotOptions& snapshotOptions = {},
        const CurrentRendererOutputOptions& outputOptions = {});

    export [[nodiscard]] RenderingContractValidationResult ValidateCurrentRendererContract(
        const CurrentRendererContract& contract);
}
