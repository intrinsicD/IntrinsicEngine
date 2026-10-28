// Authored per-entity geometry presentation (lane bindings, slots and vector
// fields) plus copied runtime snapshots of its effective state.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec4.hpp>

export module Extrinsic.Runtime.GeometryPresentation;

export import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Runtime.ConfigFieldSpec;
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
        PointNormalOrientation,
        // Point/line color and per-element point size / line width are
        // render-attribute bindings (RUNTIME-315, RUNTIME-318): the overlay
        // and the render hints, not presentation slots.
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

    enum class GeometryPresentationNormalSpace : std::uint8_t
    {
        Object,
        World,
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

        [[nodiscard]] friend bool operator==(
            const GeometryPresentationDefaultValue&,
            const GeometryPresentationDefaultValue&) = default;
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
        std::string GeneratedOutputName{};
        Graphics::Colormap::Type TextureColormap{
            Graphics::Colormap::Type::Viridis};
        GeometryPresentationNormalSpace NormalSpace{
            GeometryPresentationNormalSpace::Object};
        GeometryGeneratedOutputPolicy GeneratedPolicy{
            GeometryGeneratedOutputPolicy::DeterministicChildAsset};
        bool Enabled{true};

        [[nodiscard]] friend bool operator==(
            const GeometryPresentationSlotRecipe&,
            const GeometryPresentationSlotRecipe&) = default;
    };

    struct GeometryPresentationBindingRecipe
    {
        std::string Key{};
        GeometryPresentationKind Kind{
            GeometryPresentationKind::SurfaceMaterial};
        std::vector<GeometryPresentationSlotRecipe> Slots{};

        [[nodiscard]] friend bool operator==(
            const GeometryPresentationBindingRecipe&,
            const GeometryPresentationBindingRecipe&) = default;
    };

    struct GeometryPresentationLaneRecipe
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        std::string PresentationKey{};

        [[nodiscard]] friend bool operator==(
            const GeometryPresentationLaneRecipe&,
            const GeometryPresentationLaneRecipe&) = default;
    };

    enum class GeometryVectorFieldLengthMode : std::uint8_t
    {
        // Every glyph is `Length` object-space units long.
        Normalized,
        // Glyph = `Length` * raw property vector (object space).
        Raw,
    };

    // One authored vector-field display. Identity is the vec3 property
    // (domain + name); at most one layer exists per property. Glyph anchors
    // are derived at render time from the domain (vertex positions, edge
    // midpoints, face centers) and never written to the geometry. Vectors are
    // ordinary object-space vectors, transformed with the entity's matrix.
    struct GeometryVectorFieldLayerRecipe
    {
        GeometryPropertyRef Vector{};
        GeometryVectorFieldLengthMode LengthMode{
            GeometryVectorFieldLengthMode::Normalized};
        float Length{1.0f};
        float LineWidthPx{2.0f};
        glm::vec4 Color{1.0f, 0.5f, 0.0f, 1.0f};
        bool DepthTested{true};
        // Draw every `Stride`-th live element; `MaxGlyphs` > 0 widens the
        // stride further so at most that many glyphs are drawn. With a valid
        // UI-081 glyph order the layer draws the first ceil(L / Stride) rows
        // of the order instead, at most MaxGlyphs.
        std::uint32_t Stride{1u};
        std::uint32_t MaxGlyphs{0u};
        bool Enabled{true};
        // UI-083: optional per-arrow inputs on the vector's own domain (empty: none). The length
        // property (any scalar kind) multiplies `Length`; the color property is a scalar drawn
        // through `ColorMap` or a vec3/vec4 drawn as RGB(A). Invalid values: no arrow for the
        // length, the uniform `Color` for the color.
        std::string LengthProperty{};
        std::string ColorProperty{};
        Graphics::Colormap::Type ColorMap{Graphics::Colormap::Type::Viridis};
        bool ColorAutoRange{true};
        float ColorRangeMin{0.0f};
        float ColorRangeMax{1.0f};

        [[nodiscard]] friend bool operator==(
            const GeometryVectorFieldLayerRecipe&,
            const GeometryVectorFieldLayerRecipe&) = default;
    };

    inline constexpr std::size_t kMaxGeometryVectorFieldLayers = 32u;
    inline constexpr std::uint32_t kMaxGeometryVectorFieldStride = 1u << 24u;

    struct GeometryPresentationRecipe
    {
        GeometryPresentationShape Shape{GeometryPresentationShape::Unknown};
        std::vector<GeometryPresentationLaneRecipe> Lanes{};
        std::vector<GeometryPresentationBindingRecipe> Presentations{};
        // Vector fields are independent of lane visibility: hiding the
        // surface, edges or points keeps these layers drawn.
        std::vector<GeometryVectorFieldLayerRecipe> VectorFields{};

        [[nodiscard]] friend bool operator==(
            const GeometryPresentationRecipe&,
            const GeometryPresentationRecipe&) = default;
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
        std::string GeneratedOutputName{};
        Assets::AssetId GeneratedTexture{};
        GeometryPresentationProvenance Provenance{
            GeometryPresentationProvenance::None};
        std::uint64_t SourceGeneration{0u};
        std::uint64_t OutputGeneration{0u};
        std::string Diagnostic{};

        [[nodiscard]] friend bool operator==(
            const GeometryPresentationSlotStatus&,
            const GeometryPresentationSlotStatus&) = default;
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
        std::string GeneratedOutputName{};
        Graphics::Colormap::Type TextureColormap{
            Graphics::Colormap::Type::Viridis};
        GeometryPresentationNormalSpace NormalSpace{
            GeometryPresentationNormalSpace::Object};
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
        GeometryPresentationNormalSpace value) noexcept;
    [[nodiscard]] std::string_view ToString(
        GeometryPresentationProvenance value) noexcept;

    [[nodiscard]] bool TryParseGeometryPresentationShape(
        std::string_view value,
        GeometryPresentationShape& out) noexcept;
    [[nodiscard]] bool TryParseGeometryRenderLane(
        std::string_view value,
        GeometryRenderLane& out) noexcept;
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
    [[nodiscard]] bool TryParseGeometryPresentationNormalSpace(
        std::string_view value,
        GeometryPresentationNormalSpace& out) noexcept;

    // Domains whose elements have a well-defined glyph anchor.
    [[nodiscard]] bool SupportsGeometryVectorFieldDomain(
        GeometryElementDomain domain) noexcept;

    // RUNTIME-327: the declared style fields of a layer (agent names, types, bounds, enum
    // tokens), one per recipe field after `Vector`. The agent schema, scene keys and the
    // validator below come from the same rows; panels read names and bounds here.
    [[nodiscard]] std::span<const ConfigFieldSpec> GeometryVectorFieldStyleFields() noexcept;

    // Validates authored values only (no geometry access): supported domain,
    // named vec3 property, every style field against its declared spec, and a
    // manual color range with min < max. Returns false with a user-facing reason.
    [[nodiscard]] bool ValidateGeometryVectorFieldLayer(
        const GeometryVectorFieldLayerRecipe& layer,
        std::string& reason);
    // Per-layer validation plus unique property identity and the layer cap.
    [[nodiscard]] bool ValidateGeometryVectorFieldLayers(
        std::span<const GeometryVectorFieldLayerRecipe> layers,
        std::string& reason);

    [[nodiscard]] GeometryVectorFieldLayerRecipe* FindGeometryVectorFieldLayer(
        GeometryPresentationRecipe& recipe,
        GeometryElementDomain domain,
        std::string_view propertyName) noexcept;
    [[nodiscard]] const GeometryVectorFieldLayerRecipe*
    FindGeometryVectorFieldLayer(
        const GeometryPresentationRecipe& recipe,
        GeometryElementDomain domain,
        std::string_view propertyName) noexcept;

    // UI-081 progressive arrow order of an element domain: a float rank property ("v:", "e:" or
    // "f:glyph_rank") that Compute arrow order publishes, ranks 0..L-1 over the live rows and -1
    // on deleted ones. Vector-field layers on the domain draw a prefix of it.
    [[nodiscard]] std::string_view GeometryGlyphOrderPropertyName(GeometryElementDomain domain) noexcept;
    // shortcut: float ranks are exact only below 2^24 rows, so larger domains get no order.
    inline constexpr std::size_t kGeometryGlyphOrderRowLimit = std::size_t{1} << 24u;
    // The live rows sorted by rank when their ranks are exactly 0..L-1, each once; nullopt when the
    // order is missing or stale (an element was added or deleted since it was computed).
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> SortGeometryGlyphOrderRows(
        const Geometry::PropertySet& properties, GeometryElementDomain domain);
    // L for a valid order, 0 for none or a stale one; memoized on the rank and deletion revisions
    // so a per-frame editor model does not rescan the domain.
    [[nodiscard]] std::uint32_t CountGeometryGlyphOrderRows(
        const Geometry::PropertySet& properties, GeometryElementDomain domain);

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

namespace Extrinsic::Runtime
{
    // The first style field that violates its declared spec (Runtime.GeometryVectorFieldStyle.cpp).
    [[nodiscard]] std::optional<std::string> CheckGeometryVectorFieldStyle(const GeometryVectorFieldLayerRecipe& layer);
}
