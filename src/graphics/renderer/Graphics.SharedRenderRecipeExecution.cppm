module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.SharedRenderRecipeExecution;

import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.RenderWorld;

export namespace Extrinsic::Graphics
{
    enum class SharedRecipeProductKind : std::uint8_t
    {
        VisibleItemSet = 0,
        RejectedItemDiagnostics,
        GroupingKeys,
        BatchGroups,
        InstanceGroups,
        LodSelections,
        SpatialPartitions,
        AccelerationStructureBuildRequests,
        LightSet,
        EmissiveGeometry,
        EnvironmentMap,
        ProbeSet,
        VolumeSet,
        Tags,
        QualitySettings,
        ShadowIntent,
        ProbeIntent,
        GlobalIlluminationIntent,
        DebugMode,
        Fallbacks,
    };

    enum class SharedRecipeDiagnosticSeverity : std::uint8_t
    {
        Info = 0,
        Warning,
        Error,
    };

    enum class SharedRecipeDiagnosticCode : std::uint8_t
    {
        None = 0,
        EmptyVisibilityInput,
        EmptyLightingInput,
        InvalidRenderable,
        MissingGeometry,
        MissingInstance,
        NonFiniteBounds,
        NotVisible,
        UnsupportedRenderDomain,
        UnsupportedProduct,
        StaleInput,
        DegradedOutput,
        InvalidLight,
        UnsupportedLight,
        MissingEnvironment,
        FallbackUsed,
        MissingRendererCapability,
        ProductNotProduced,
    };

    enum class VisibilityRecipeDomain : std::uint8_t
    {
        Surface = 0,
        Line,
        Point,
        Shadow,
        Selection,
    };

    enum class LightingRecipeResolvedLightType : std::uint8_t
    {
        Directional = 0,
        Point,
        Spot,
        FallbackDirectional,
    };

    struct SharedRecipeDiagnostic
    {
        SharedRecipeDiagnosticCode Code{SharedRecipeDiagnosticCode::None};
        SharedRecipeDiagnosticSeverity Severity{SharedRecipeDiagnosticSeverity::Info};
        std::string Subject{};
        std::string Message{};
    };

    struct VisibilityRecipeOptions
    {
        bool IncludeSurface{true};
        bool IncludeLines{true};
        bool IncludePoints{true};
        bool IncludeShadowCasters{true};
        bool IncludeSelectionCandidates{true};
        bool RequestAccelerationStructures{false};
        float NearLodDistance{25.0f};
        float FarLodDistance{125.0f};
        float SpatialPartitionCellSize{32.0f};
    };

    struct VisibilityRecipeVisibleItem
    {
        std::uint32_t StableId{0u};
        VisibilityRecipeDomain Domain{VisibilityRecipeDomain::Surface};
        std::uint32_t GroupKey{0u};
        std::uint32_t BatchGroup{0u};
        std::uint32_t InstanceGroup{0u};
        std::uint32_t LodLevel{0u};
        std::uint32_t SpatialPartition{0u};
        float SortDepth{0.0f};
        bool AccelerationStructureRequested{false};
    };

    struct VisibilityRecipeRejectedItem
    {
        std::uint32_t StableId{0u};
        SharedRecipeDiagnosticCode Reason{SharedRecipeDiagnosticCode::InvalidRenderable};
        std::string Message{};
    };

    struct VisibilityRecipeAccelerationStructureRequest
    {
        std::uint32_t StableId{0u};
        VisibilityRecipeDomain Domain{VisibilityRecipeDomain::Surface};
        std::uint32_t GeometryIndex{0u};
        std::uint32_t InstanceIndex{0u};
    };

    struct VisibilityRecipeExecutionResult
    {
        std::vector<SharedRecipeProductKind> Products{};
        std::vector<VisibilityRecipeVisibleItem> VisibleItems{};
        std::vector<VisibilityRecipeRejectedItem> RejectedItems{};
        std::vector<VisibilityRecipeAccelerationStructureRequest> AccelerationStructures{};
        std::vector<SharedRecipeDiagnostic> Diagnostics{};
        std::uint32_t SurfaceItemCount{0u};
        std::uint32_t LineItemCount{0u};
        std::uint32_t PointItemCount{0u};
        std::uint32_t ShadowItemCount{0u};
        std::uint32_t SelectionItemCount{0u};
        bool StaleInput{false};
        bool Degraded{false};
    };

    struct LightingRecipeOptions
    {
        bool EnableFallbackDirectional{true};
        LightSnapshot FallbackDirectional{};
        bool HasEnvironmentMap{false};
        std::string EnvironmentMapId{};
        std::vector<std::string> ProbeIds{};
        std::vector<std::string> VolumeIds{};
        std::vector<std::string> Tags{};
        std::vector<std::uint32_t> EmissiveGeometryStableIds{};
        std::string QualityPreset{"balanced"};
        bool RequestShadowIntents{true};
        bool RequestProbeIntents{false};
        bool RequestGlobalIlluminationIntent{false};
        bool DebugMode{false};
        std::uint32_t MaxLights{4096u};
    };

    struct LightingRecipeResolvedLight
    {
        LightingRecipeResolvedLightType Type{LightingRecipeResolvedLightType::Directional};
        glm::vec3 Position{0.0f};
        float Range{0.0f};
        glm::vec3 Direction{0.0f, -1.0f, 0.0f};
        float Intensity{0.0f};
        glm::vec3 Color{1.0f};
        float InnerConeCos{0.0f};
        float OuterConeCos{0.0f};
    };

    struct EnvironmentRecipeProduct
    {
        bool HasEnvironmentMap{false};
        std::string EnvironmentMapId{};
        bool UsedFallbackEnvironment{false};
        std::vector<std::string> ProbeIds{};
        std::vector<std::string> VolumeIds{};
        std::vector<std::string> Tags{};
        std::string QualityPreset{};
        bool DebugMode{false};
    };

    struct LightingRecipeIntent
    {
        SharedRecipeProductKind Product{SharedRecipeProductKind::ShadowIntent};
        std::string Name{};
        std::uint32_t Count{0u};
    };

    struct LightingRecipeExecutionResult
    {
        std::vector<SharedRecipeProductKind> Products{};
        std::vector<LightingRecipeResolvedLight> Lights{};
        std::vector<std::uint32_t> EmissiveGeometryStableIds{};
        EnvironmentRecipeProduct Environment{};
        std::vector<LightingRecipeIntent> Intents{};
        std::vector<SharedRecipeDiagnostic> Diagnostics{};
        std::uint32_t DirectionalLightCount{0u};
        std::uint32_t PointLightCount{0u};
        std::uint32_t SpotLightCount{0u};
        std::uint32_t RejectedLightCount{0u};
        bool UsedFallbackDirectional{false};
        bool StaleInput{false};
        bool Degraded{false};
    };

    struct SharedRecipeRendererProductDeclaration
    {
        RendererDescriptor Renderer{};
        std::vector<SharedRecipeProductKind> ConsumedProducts{};
    };

    struct SharedRecipeCompatibilityResult
    {
        std::vector<SharedRecipeProductKind> SupportedProducts{};
        std::vector<SharedRecipeProductKind> UnsupportedProducts{};
        std::vector<SharedRecipeDiagnostic> Diagnostics{};

        [[nodiscard]] bool Compatible() const noexcept;
    };

    [[nodiscard]] std::string_view ToString(SharedRecipeProductKind value) noexcept;
    [[nodiscard]] std::string_view ToString(SharedRecipeDiagnosticCode value) noexcept;
    [[nodiscard]] std::string_view ToString(VisibilityRecipeDomain value) noexcept;
    [[nodiscard]] std::string_view ToString(LightingRecipeResolvedLightType value) noexcept;

    [[nodiscard]] bool HasDiagnostic(
        std::span<const SharedRecipeDiagnostic> diagnostics,
        SharedRecipeDiagnosticCode code) noexcept;
    [[nodiscard]] std::uint32_t CountDiagnostics(
        std::span<const SharedRecipeDiagnostic> diagnostics,
        SharedRecipeDiagnosticCode code) noexcept;

    [[nodiscard]] VisibilityRecipeExecutionResult ExecuteVisibilityRecipe(
        const RenderWorld& world,
        const SnapshotEnvelope& snapshot,
        const VisibilityRecipeOptions& options = {});

    [[nodiscard]] LightingRecipeExecutionResult ExecuteLightingRecipe(
        const RenderWorld& world,
        const SnapshotEnvelope& snapshot,
        const LightingRecipeOptions& options = {});

    [[nodiscard]] SharedRecipeCompatibilityResult CheckSharedRecipeCompatibility(
        const SharedRecipeRendererProductDeclaration& declaration,
        std::span<const SharedRecipeProductKind> producedProducts = {});
}
