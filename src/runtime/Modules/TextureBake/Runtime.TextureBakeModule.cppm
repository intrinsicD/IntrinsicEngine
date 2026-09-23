// Exposes property texture baking and generated outputs to runtime composition.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.TextureBakeModule;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties.Types;

namespace Extrinsic::Runtime
{
    // Reserved output: rebinding the selected surface property replaces this texture.
    export inline constexpr std::string_view kSurfaceAppearanceTextureOutput = "appearance.color";

    // Largest bake width or height. UV generation accepts larger atlas
    // extents; baking one fails as InvalidResolution before allocating.
    export inline constexpr std::uint32_t kPropertyTextureBakeMaxExtent = 8192u;

    export enum class PropertyTextureBakeStatus : std::uint8_t
    {
        Success,
        Scheduled,
        NonOperationalBackend,
        MissingScene,
        MissingAssetService,
        StaleEntity,
        NonMeshSource,
        InvalidResolution,
        InvalidPadding,
        InvalidRange,
        MissingProperty,
        UnsupportedPropertyType,
        MismatchedPropertyCount,
        UnsupportedSourceDomain,
        MissingTexcoords,
        NonFiniteTexcoord,
        NonFinitePropertyValue,
        DegenerateAllTriangles,
        DegenerateUvTriangles,
        OverlappingUvCharts,
        UnderresolvedAtlas,
        ZeroCoverageBake,
        BakeFailed,
        AssetLoadFailed,
        CommandFailed,
        JobSubmitFailed,
        StaleCompletion,
    };

    export enum class PropertyTextureBakeExecutionMode : std::uint8_t
    {
        PropertyRasterGpu,
    };

    export enum class PropertyTextureBakeStorage : std::uint8_t
    {
        Auto,
        RawFloat,
        EncodedRgba,
    };

    export enum class PropertyTextureBakeEncoding : std::uint8_t
    {
        Auto,
        ScalarColormap,
        LinearScalar,
        LabelPalette,
        Vector2,
        Vector3,
        Normal,
        RgbaColor,
    };

    export [[nodiscard]] PropertyTextureBakeEncoding ResolveSurfaceAppearanceEncoding(
        const Graphics::Components::VisualizationConfig& config,
        Geometry::PropertyValueKind kind) noexcept;

    export enum class PropertyTextureBakeRangePolicy : std::uint8_t
    {
        AutoFinite,
        Manual,
    };

    export enum class PropertyTextureBakeOutputState : std::uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    export struct PropertyTextureBakeRepresentation
    {
        PropertyTextureBakeStorage Storage{
            PropertyTextureBakeStorage::RawFloat};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Auto};
    };

    export [[nodiscard]] PropertyTextureBakeRepresentation
        ResolvePropertyTextureBakeRepresentation(
            Geometry::PropertyValueKind valueKind,
            PropertyTextureBakeStorage requestedStorage,
            PropertyTextureBakeEncoding requestedEncoding) noexcept;

    export [[nodiscard]] bool IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind valueKind,
        PropertyTextureBakeStorage storage,
        PropertyTextureBakeEncoding encoding) noexcept;

    export struct PropertyTextureBakeRequest
    {
        WorldHandle World{DefaultWorldHandle};
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Source{};
        // Empty name selects the canonical atlas: a complete corner
        // `h:texcoord`, else vertex `v:texcoord`. A named MeshHalfedge or
        // MeshVertex vec2 reference binds exactly that property.
        GeometryPropertyRef Texcoords{};
        std::uint64_t ExpectedSourceGeneration{0u};
        std::uint64_t ExpectedPropertyGeneration{0u};
        PropertyTextureBakeStorage Storage{
            PropertyTextureBakeStorage::Auto};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Auto};
        PropertyTextureBakeRangePolicy RangePolicy{
            PropertyTextureBakeRangePolicy::AutoFinite};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        Graphics::Colormap::Type EncodingColormap{
            Graphics::Colormap::Type::Viridis};
        // Surface appearance uses the entity's recorded generated-atlas
        // extent (MeshUvAtlasExtent), else this default.
        std::uint32_t Width{1024u};
        std::uint32_t Height{1024u};
        // Zero bakes exactly Width x Height. Otherwise, while some UV chart
        // covers no texel centre, both sides double (aspect preserved) up to
        // this bound; the record reports the adopted extent. Coverage stays
        // strict: no chart may be left unresolved at the adopted extent.
        std::uint32_t MaxAdaptiveExtent{0u};
        // Chebyshev gutter around each UV chart, in texels, for every
        // storage. Outputs have one mip level; see
        // Graphics.PropertyTextureBake for the raster contract.
        std::uint32_t PaddingTexels{0u};
        std::string OutputName{};
        Assets::AssetId ExistingGeneratedTexture{};
    };

    export struct PropertyTextureBakeResult
    {
        PropertyTextureBakeStatus Status{
            PropertyTextureBakeStatus::Success};
        PropertyTextureBakeExecutionMode ExecutionMode{
            PropertyTextureBakeExecutionMode::PropertyRasterGpu};
        PropertyTextureBakeOutputState State{
            PropertyTextureBakeOutputState::Pending};
        Assets::AssetId GeneratedTexture{};
        JobToken Job{};
        std::uint64_t Generation{0u};
        std::uint64_t SourceGeneration{0u};
        std::string GeneratedAssetPath{};
        std::string OutputName{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == PropertyTextureBakeStatus::Success ||
                   Status == PropertyTextureBakeStatus::Scheduled;
        }
    };

    export enum class PropertyTextureBakeFreshness : std::uint8_t
    {
        // Not evaluated yet, or the record carries no source identity.
        Unknown,
        Fresh,
        TopologyChanged,
        UvChanged,
        PositionsChanged,
        PropertyChanged,
        // The bound UV/source property or mesh topology no longer resolves.
        SourceUnavailable,
    };

    export [[nodiscard]] const char* DebugNameForPropertyTextureBakeFreshness(
        PropertyTextureBakeFreshness freshness) noexcept;

    // Content identity of the source a bake rasterized. Fingerprints are
    // deterministic 64-bit hashes of the exact bytes consumed, so they survive
    // save/load and recover after undo restores the same content.
    export struct PropertyTextureBakeSourceIdentity
    {
        GeometryPropertyRef ResolvedTexcoords{};
        // Resolved UV of every triangle corner in surface order.
        std::uint64_t UvFingerprint{0u};
        std::uint64_t PositionFingerprint{0u};
        // Triangle corner vertices and triangle-to-face correspondence.
        std::uint64_t TopologyFingerprint{0u};
        // Source property domain, name, kind and typed values.
        std::uint64_t PropertyFingerprint{0u};

        friend bool operator==(
            const PropertyTextureBakeSourceIdentity&,
            const PropertyTextureBakeSourceIdentity&) = default;
    };

    export [[nodiscard]] PropertyTextureBakeFreshness
        ComparePropertyTextureBakeSourceIdentity(
            const PropertyTextureBakeSourceIdentity& baked,
            const PropertyTextureBakeSourceIdentity& current) noexcept;

    export struct PropertyTextureBakeRecord
    {
        std::string OutputName{};
        GeometryPropertyRef Source{};
        GeometryPropertyRef Texcoords{};
        PropertyTextureBakeStorage Storage{
            PropertyTextureBakeStorage::Auto};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Auto};
        Graphics::Colormap::Type EncodingColormap{
            Graphics::Colormap::Type::Viridis};
        Assets::AssetId Texture{};
        // Paired R32F chart coverage; zero marks no data independently of alpha.
        Assets::AssetId CoverageTexture{};
        std::size_t ExpectedElementCount{0u};
        std::uint64_t SourceGeneration{0u};
        std::uint64_t PropertyGeneration{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t PaddingTexels{0u};
        std::uint64_t Generation{0u};
        PropertyTextureBakeOutputState State{
            PropertyTextureBakeOutputState::Pending};
        std::string Diagnostic{};
        PropertyTextureBakeRangePolicy RangePolicy{
            PropertyTextureBakeRangePolicy::AutoFinite};
        // `Texcoords` is the requested binding (empty = canonical atlas);
        // `ResolvedTexcoords` is the property the bake actually rasterized.
        GeometryPropertyRef ResolvedTexcoords{};
        std::uint64_t UvFingerprint{0u};
        std::uint64_t PositionFingerprint{0u};
        std::uint64_t TopologyFingerprint{0u};
        std::uint64_t PropertyFingerprint{0u};
        PropertyTextureBakeFreshness Freshness{
            PropertyTextureBakeFreshness::Unknown};
        // Runtime-only Geometry::PropertyRevision digest observed when
        // `Freshness` was last evaluated; never persist it.
        std::uint64_t ObservedRevisionToken{0u};
        // Texel centres inside atlas triangles and edge-connected UV charts
        // measured at `Width` x `Height`.
        std::uint64_t CoveredTexels{0u};
        std::uint32_t ChartCount{0u};
        // Runtime-only revision token observed when an automatic producer's
        // request was rejected before scheduling; zero otherwise. The
        // producer resubmits only after this token or its request changes.
        std::uint64_t RejectedRevisionToken{0u};

        [[nodiscard]] PropertyTextureBakeSourceIdentity SourceIdentity()
            const
        {
            return PropertyTextureBakeSourceIdentity{
                .ResolvedTexcoords = ResolvedTexcoords,
                .UvFingerprint = UvFingerprint,
                .PositionFingerprint = PositionFingerprint,
                .TopologyFingerprint = TopologyFingerprint,
                .PropertyFingerprint = PropertyFingerprint,
            };
        }
    };

    // Returns a property's current Geometry::PropertyRevision, or nullopt
    // when the element domain or property is absent.
    export using PropertyTextureBakeRevisionLookup =
        std::function<std::optional<std::uint64_t>(
            GeometryElementDomain,
            std::string_view)>;

    // Digest of the content revisions of every property a record depends on.
    // A token equal to `ObservedRevisionToken` means the evaluated freshness
    // still describes the live source.
    export [[nodiscard]] std::uint64_t ComputePropertyTextureBakeRevisionToken(
        const PropertyTextureBakeRecord& record,
        const PropertyTextureBakeRevisionLookup& lookup);

    // Consumers bind a baked texture only when it is Ready, evaluated Fresh
    // and no dependency changed since that evaluation.
    export [[nodiscard]] bool IsPropertyTextureBakeRecordBindable(
        const PropertyTextureBakeRecord& record,
        std::uint64_t currentRevisionToken) noexcept;

    export struct PropertyTextureBakeOutputs
    {
        std::vector<PropertyTextureBakeRecord> Records{};
        std::uint64_t Generation{1u};
    };

    export [[nodiscard]] const char* DebugNameForPropertyTextureBakeStatus(
        PropertyTextureBakeStatus status) noexcept;

    export struct TextureBakeBindingChanged
    {
        WorldHandle World{};
        std::uint64_t BindingEpoch{0u};
    };

    export struct TextureBakeModuleStats
    {
        std::uint64_t BakeRequests{0u};
        std::uint64_t BakeRequestsAccepted{0u};
        std::uint64_t BakeRequestsRejected{0u};
        std::uint64_t BindingChanges{0u};
    };

    export enum class TextureBakeMutationStatus : std::uint8_t
    {
        Success,
        MissingScene,
        StaleEntity,
        MissingTexture,
        DuplicateName,
        InvalidName,
        IncompatibleTarget,
        AssetDestroyFailed,
        CommandFailed,
    };

    export struct TextureBakeMutationResult
    {
        TextureBakeMutationStatus Status{
            TextureBakeMutationStatus::Success};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == TextureBakeMutationStatus::Success;
        }
    };

    // Copied records; each record's Freshness is re-evaluated against the
    // live source when the snapshot is taken.
    export struct TextureBakeSnapshot
    {
        std::vector<PropertyTextureBakeRecord> Textures{};
        bool GpuOperational{false};
        std::string Diagnostic{};
    };

    export class TextureBakeService
    {
    public:
        TextureBakeService();
        ~TextureBakeService();

        TextureBakeService(const TextureBakeService&) = delete;
        TextureBakeService& operator=(const TextureBakeService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] PropertyTextureBakeResult Bake(
            const PropertyTextureBakeRequest& request);
        [[nodiscard]] TextureBakeModuleStats Stats() const noexcept;
        // Test seam: replaces the 64 MiB retained source-snapshot budget.
        void SetSourceSnapshotBudgetForTest(std::size_t bytes) noexcept;
        [[nodiscard]] TextureBakeSnapshot Snapshot(
            std::uint32_t stableEntityId) const;
        [[nodiscard]] TextureBakeMutationResult Rename(
            std::uint32_t stableEntityId,
            std::string_view currentName,
            std::string_view newName);
        [[nodiscard]] TextureBakeMutationResult Remove(
            std::uint32_t stableEntityId,
            std::string_view outputName);

      private:
        friend class TextureBakeModule;

        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };

    export class TextureBakeModule final : public IRuntimeModule
    {
    public:
        TextureBakeModule();
        ~TextureBakeModule() override;

        TextureBakeModule(const TextureBakeModule&) = delete;
        TextureBakeModule& operator=(const TextureBakeModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
