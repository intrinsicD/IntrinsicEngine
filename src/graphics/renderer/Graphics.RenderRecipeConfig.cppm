// Render-recipe config lane data: the serializable schema identity, parse and
// validation diagnostics, the side-effect-free preview/load results, and the
// validated `FrameRecipeOverride` those results produce for the renderer.
//
// Plain records and free functions only; no renderer or framegraph dependency.
module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.RenderRecipeConfig;

import Extrinsic.Graphics.RenderingContract;

namespace Extrinsic::Graphics
{
    export inline constexpr std::string_view kRenderRecipeConfigSchemaId =
        "intrinsic.graphics.render-recipe-config";
    export inline constexpr std::uint32_t kRenderRecipeConfigSchemaVersion = 1u;

    export enum class RenderRecipeConfigState : std::uint8_t
    {
        Valid = 0,
        Invalid,
        Unsupported,
        Stale,
        Degraded,
        FallbackApplied,
    };

    export enum class RenderRecipeConfigDiagnosticCode : std::uint8_t
    {
        None = 0,
        EmptyDocument,
        LoadError,
        ParseError,
        InvalidSchema,
        UnsupportedVersion,
        RendererMismatch,
        FixedCoreMutation,
        UnknownRecipeSlot,
        UnsupportedCapability,
        UnsafeBindingDomain,
        UnknownBindingRole,
        RequiredBindingOverride,
        DisallowedBindingRole,
        InvalidDefaults,
        InvalidViewOutput,
        UnsupportedOutput,
        FallbackApplied,
        StaleConfig,
        DegradedConfig,
        ContractValidationFailed,
    };

    export struct RenderRecipeConfigDiagnostic
    {
        RenderRecipeConfigState State = RenderRecipeConfigState::Invalid;
        RenderingContractDiagnosticSeverity Severity = RenderingContractDiagnosticSeverity::Error;
        RenderRecipeConfigDiagnosticCode Code = RenderRecipeConfigDiagnosticCode::None;
        std::string Subject{};
        std::string Message{};
    };

    export struct RenderRecipeConfigContext
    {
        RendererDescriptor Renderer{};
        RenderRecipeDescriptor BaseRecipe{};
        ViewOutputRecipeDescriptor BaseViewOutput{};
        BindingSet BaseBindings{};
    };

    export struct RenderRecipeConfigPreview
    {
        RenderRecipeDescriptor Recipe{};
        ViewOutputRecipeDescriptor ViewOutput{};
        BindingSet Bindings{};
        std::vector<std::string> DisabledExtensionSlots{};
        bool SideEffectFree = true;
        std::uint32_t ParsedSlotCount = 0u;
        std::uint32_t ParsedBindingOverrideCount = 0u;
    };

    export struct RenderRecipeConfigParseOptions
    {
        std::string SourceId{"<memory>"};
        bool AllowStaleConfig = false;
    };

    export struct RenderRecipeConfigLoadResult
    {
        RenderRecipeConfigState State = RenderRecipeConfigState::Invalid;
        std::string SourceId{};
        std::uint32_t SchemaVersion = 0u;
        std::string RendererId{};
        RenderRecipeConfigPreview Preview{};
        std::vector<RenderRecipeConfigDiagnostic> Diagnostics{};
        RenderingContractValidationResult ContractDiagnostics{};
    };

    export enum class FrameRecipeOverrideDiagnosticCode : std::uint8_t
    {
        None = 0,
        EmptyRecipeId,
        FixedCoreMutation,
        UnknownSlot,
        FixedCoreSlotDisabled,
        UnsupportedSlotDisable,
        UnsupportedCapability,
    };

    export struct FrameRecipeOverrideDiagnostic
    {
        FrameRecipeOverrideDiagnosticCode Code{FrameRecipeOverrideDiagnosticCode::None};
        std::string Subject{};
        std::string Message{};
    };

    // Fail-closed override carried from the config/UI/agent lanes to the
    // renderer. It pairs a validated `RenderRecipeDescriptor` with explicit
    // optional-slot disables; applying it can only turn off optional feature
    // gates at the default-recipe build site, never replace the fixed core.
    export struct FrameRecipeOverride
    {
        RenderRecipeDescriptor Recipe{};
        std::vector<std::string> DisabledExtensionSlots{};
        std::string SourceId{};
    };

    export [[nodiscard]] std::string_view ToString(RenderRecipeConfigState value) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderRecipeConfigDiagnosticCode value) noexcept;
    export [[nodiscard]] bool HasErrors(const RenderRecipeConfigLoadResult& result) noexcept;
    export [[nodiscard]] bool IsConfigUsable(const RenderRecipeConfigLoadResult& result) noexcept;
    export [[nodiscard]] bool HasDiagnostic(const RenderRecipeConfigLoadResult& result,
                                            RenderRecipeConfigDiagnosticCode code) noexcept;
    export [[nodiscard]] std::uint32_t CountByState(const RenderRecipeConfigLoadResult& result,
                                                    RenderRecipeConfigState state) noexcept;

    export [[nodiscard]] RenderRecipeConfigLoadResult PreviewRenderRecipeConfig(
        std::string_view document,
        const RenderRecipeConfigContext& context,
        const RenderRecipeConfigParseOptions& options = {});

    export [[nodiscard]] RenderRecipeConfigLoadResult LoadRenderRecipeConfigFile(
        std::string_view path,
        const RenderRecipeConfigContext& context,
        const RenderRecipeConfigParseOptions& options = {});
}
