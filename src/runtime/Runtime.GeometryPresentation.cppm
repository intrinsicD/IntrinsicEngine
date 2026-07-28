module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.GeometryPresentation;

export import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
export import Extrinsic.Runtime.GeometryAvailability;

export namespace Extrinsic::Runtime
{
    enum class GeometryPresentationShape : std::uint8_t
    {
        Unknown,
        Composition,
        Mesh,
        Graph,
        PointCloud,
        Procedural,
    };

    enum class GeometryPresentationKind : std::uint8_t
    {
        SurfaceMaterial,
        PointPresentation,
        LinePresentation,
    };

    enum class GeometryPresentationSlotSemantic : std::uint8_t
    {
        Albedo,
        Normal,
        Roughness,
        Metallic,
        ScalarField,
        Displacement,
        PointColor,
        PointScalarField,
        PointSize,
        PointNormalOrientation,
        LineColor,
        LineScalarField,
        LineWidth,
    };

    enum class GeometryPresentationSourceKind : std::uint8_t
    {
        UniformDefault,
        AuthoredTextureAsset,
        GeneratedTextureAsset,
        PropertyBake,
        PropertyBuffer,
    };

    enum class GeometryPresentationReadiness : std::uint8_t
    {
        Unset,
        DefaultValue,
        Pending,
        Ready,
        Failed,
        Unsupported,
        Stale,
    };

    enum class GeometryPresentationFallback : std::uint8_t
    {
        None,
        UniformDefault,
        PreviousGeneratedTexture,
    };

    enum class GeometryGeneratedOutputPolicy : std::uint8_t
    {
        SessionCache,
        DeterministicChildAsset,
        PersistOnSave,
    };

    enum class GeometryPresentationProvenance : std::uint8_t
    {
        None,
        AuthoredAsset,
        GeneratedTextureAsset,
        PropertyBuffer,
        UniformDefault,
        PropertyBinding,
    };

    struct GeometryPresentationDefaultValue
    {
        Geometry::PropertyValueKind Kind{Geometry::PropertyValueKind::Vec4};
        glm::vec4 Vector{1.0f, 1.0f, 1.0f, 1.0f};
        double Scalar{1.0};
        std::uint32_t UInt{0u};
    };

    // Desired authoring state only. Runtime readiness, generated outputs,
    // diagnostics, and generations live in GeometryPresentationRuntimeState.
    struct GeometryPresentationSlotRecipe
    {
        GeometryPresentationSlotSemantic Semantic{
            GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationSourceKind SourceKind{
            GeometryPresentationSourceKind::UniformDefault};
        GeometryPresentationDefaultValue UniformDefault{};
        GeometryPropertyRef Property{};
        Assets::AssetId TextureAsset{};
        GeometryGeneratedOutputPolicy GeneratedPolicy{
            GeometryGeneratedOutputPolicy::DeterministicChildAsset};
        bool Enabled{true};
    };

    struct GeometryPresentationBindingRecipe
    {
        std::string Key{};
        GeometryPresentationKind Kind{
            GeometryPresentationKind::SurfaceMaterial};
        std::vector<GeometryPresentationSlotRecipe> Slots{};
    };

    struct GeometryPresentationLaneRecipe
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        std::string PresentationKey{};
    };

    struct GeometryPresentationRecipe
    {
        GeometryPresentationShape Shape{GeometryPresentationShape::Unknown};
        std::vector<GeometryPresentationLaneRecipe> Lanes{};
        std::vector<GeometryPresentationBindingRecipe> Presentations{};
    };

    // Runtime-only observations keyed by authored presentation + slot
    // semantic. This sidecar is plain copied data and is never serialized.
    struct GeometryPresentationSlotStatus
    {
        std::string PresentationKey{};
        GeometryPresentationSlotSemantic Semantic{
            GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationReadiness Readiness{
            GeometryPresentationReadiness::Unset};
        Assets::AssetId GeneratedTexture{};
        GeometryPresentationProvenance Provenance{
            GeometryPresentationProvenance::None};
        std::uint64_t SourceGeneration{0u};
        std::uint64_t OutputGeneration{0u};
        std::string Diagnostic{};
    };

    struct GeometryPresentationRuntimeState
    {
        std::uint64_t RecipeGeneration{1u};
        std::vector<GeometryPresentationSlotStatus> Slots{};
    };

    struct GeometryPresentationPropertyOption
    {
        GeometryPropertyRef Property{};
        std::size_t ElementCount{0u};
        std::uint64_t SourceGeneration{0u};
        bool Compatible{false};
        std::string DisabledReason{};
    };

    struct GeometryPresentationSlotSnapshot
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        std::string PresentationKey{};
        GeometryPresentationKind PresentationKind{
            GeometryPresentationKind::SurfaceMaterial};
        GeometryPresentationSlotSemantic Semantic{
            GeometryPresentationSlotSemantic::Albedo};
        GeometryPresentationSourceKind SourceKind{
            GeometryPresentationSourceKind::UniformDefault};
        GeometryPresentationReadiness Readiness{
            GeometryPresentationReadiness::Unset};
        GeometryPresentationFallback Fallback{
            GeometryPresentationFallback::None};
        GeometryPresentationDefaultValue UniformDefault{};
        GeometryPropertyRef Property{};
        GeometryPropertyResolution PropertyResolution{};
        Assets::AssetId TextureAsset{};
        GeometryPresentationProvenance Provenance{
            GeometryPresentationProvenance::None};
        std::uint64_t SourceGeneration{0u};
        std::uint64_t OutputGeneration{0u};
        bool Enabled{false};
        bool UsesUniformDefault{false};
        bool TextureReady{false};
        bool PropertyBufferReady{false};
        bool PreviousOutputRetained{false};
        bool Unsupported{false};
        std::string Diagnostic{};
    };

    struct GeometryPresentationSnapshotStats
    {
        std::uint32_t LaneCount{0u};
        std::uint32_t SlotCount{0u};
        std::uint32_t DefaultSlotCount{0u};
        std::uint32_t ReadyTextureSlotCount{0u};
        std::uint32_t PendingSlotCount{0u};
        std::uint32_t FailedSlotCount{0u};
        std::uint32_t UnsupportedSlotCount{0u};
        std::uint32_t PropertyBufferReadyCount{0u};
        std::uint32_t PreviousOutputRetainedCount{0u};
        std::uint32_t DiagnosticCount{0u};
    };

    // Fully copied effective state. It contains no ECS entity, borrowed
    // property view, job token, graphics/RHI handle, or live service pointer.
    struct GeometryPresentationSnapshot
    {
        GeometryPresentationShape Shape{GeometryPresentationShape::Unknown};
        std::uint64_t RecipeGeneration{0u};
        std::uint64_t ObservedSourceGeneration{0u};
        std::vector<GeometryPresentationSlotSnapshot> Slots{};
        GeometryPresentationSnapshotStats Stats{};
    };

    [[nodiscard]] std::string_view ToString(
        GeometryPresentationShape value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationKind value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationSlotSemantic value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationSourceKind value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationReadiness value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationFallback value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryGeneratedOutputPolicy value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationProvenance value) noexcept;

    [[nodiscard]] bool TryParseGeometryPresentationShape(
        std::string_view value,
        GeometryPresentationShape& out) noexcept;
    [[nodiscard]] bool TryParseGeometryPresentationKind(
        std::string_view value,
        GeometryPresentationKind& out) noexcept;
    [[nodiscard]] bool TryParseGeometryPresentationSlotSemantic(
        std::string_view value,
        GeometryPresentationSlotSemantic& out) noexcept;
    [[nodiscard]] bool TryParseGeometryPresentationSourceKind(
        std::string_view value,
        GeometryPresentationSourceKind& out) noexcept;
    [[nodiscard]] bool TryParseGeometryGeneratedOutputPolicy(
        std::string_view value,
        GeometryGeneratedOutputPolicy& out) noexcept;

    [[nodiscard]] GeometryGeneratedOutputPolicy
    DefaultGeometryGeneratedOutputPolicyFor(
        GeometryPresentationSourceKind sourceKind) noexcept;

    [[nodiscard]] std::vector<GeometryPresentationPropertyOption>
    EnumerateGeometryPresentationPropertyOptions(
        const ECS::Components::GeometrySources::ConstSourceView& source,
        GeometryElementDomain domain,
        GeometryPropertyValueKindFilter expectedValueKind = std::nullopt,
        std::uint64_t observedSourceGeneration = 0u);

    [[nodiscard]] GeometryPresentationLaneRecipe* FindGeometryPresentationLane(
        GeometryPresentationRecipe& recipe,
        GeometryRenderLane lane) noexcept;
    [[nodiscard]] const GeometryPresentationLaneRecipe*
    FindGeometryPresentationLane(
        const GeometryPresentationRecipe& recipe,
        GeometryRenderLane lane) noexcept;

    [[nodiscard]] GeometryPresentationBindingRecipe*
    FindGeometryPresentationBinding(
        GeometryPresentationRecipe& recipe,
        std::string_view key) noexcept;
    [[nodiscard]] const GeometryPresentationBindingRecipe*
    FindGeometryPresentationBinding(
        const GeometryPresentationRecipe& recipe,
        std::string_view key) noexcept;

    [[nodiscard]] GeometryPresentationSlotRecipe* FindGeometryPresentationSlot(
        GeometryPresentationBindingRecipe& presentation,
        GeometryPresentationSlotSemantic semantic) noexcept;
    [[nodiscard]] const GeometryPresentationSlotRecipe*
    FindGeometryPresentationSlot(
        const GeometryPresentationBindingRecipe& presentation,
        GeometryPresentationSlotSemantic semantic) noexcept;

    [[nodiscard]] GeometryPresentationSlotStatus*
    FindGeometryPresentationSlotStatus(
        GeometryPresentationRuntimeState& state,
        std::string_view presentationKey,
        GeometryPresentationSlotSemantic semantic) noexcept;
    [[nodiscard]] const GeometryPresentationSlotStatus*
    FindGeometryPresentationSlotStatus(
        const GeometryPresentationRuntimeState& state,
        std::string_view presentationKey,
        GeometryPresentationSlotSemantic semantic) noexcept;

    [[nodiscard]] bool IsSurfaceTextureSemantic(
        GeometryPresentationSlotSemantic semantic) noexcept;

    [[nodiscard]] GeometryPresentationSnapshot
    BuildGeometryPresentationSnapshot(
        const ECS::Components::GeometrySources::ConstSourceView& source,
        const GeometryPresentationRecipe& recipe,
        const GeometryPresentationRuntimeState& runtimeState = {},
        std::uint64_t observedSourceGeneration = 0u);
}
