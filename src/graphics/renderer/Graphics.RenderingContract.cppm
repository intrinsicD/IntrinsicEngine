module;

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.RenderingContract;

namespace Extrinsic::Graphics
{
    export enum class RendererPurpose : std::uint8_t
    {
        Unknown = 0,
        Realtime,
        Offline,
        Preview,
        Picking,
        Metrics,
    };

    export enum class SnapshotKind : std::uint8_t
    {
        Unknown = 0,
        FullScene,
        SelectedEntity,
        EntitySet,
        StreamingChunk,
        OfflinePackage,
        BenchmarkPackage,
    };

    export enum class SnapshotScope : std::uint8_t
    {
        Unknown = 0,
        FullScene,
        Selection,
        EntitySet,
        Chunk,
        Offline,
        Benchmark,
    };

    export enum class RendererUpdateMode : std::uint8_t
    {
        Static = 0,
        PerFrame,
        Streaming,
        OnDemand,
    };

    export enum class RenderDataCategory : std::uint8_t
    {
        Geometry = 0,
        Materials,
        Transforms,
        Cameras,
        Lights,
        Visibility,
        Environment,
        Picking,
        Diagnostics,
    };

    export enum class RendererCapability : std::uint8_t
    {
        Surface = 0,
        Lines,
        Points,
        Shadows,
        Picking,
        Readback,
        Headless,
        Interactive,
        DebugView,
        VisibilityRecipe,
        LightingRecipe,
    };

    export enum class RenderOutputKind : std::uint8_t
    {
        Color = 0,
        Depth,
        EntityId,
        PrimitiveId,
        Metrics,
        ReadbackBuffer,
        Artifact,
    };

    export enum class RenderingFallbackPolicy : std::uint8_t
    {
        FailClosed = 0,
        Degrade,
        SubstituteDefaults,
        PreservePrevious,
    };

    export enum class SnapshotValidationState : std::uint8_t
    {
        Unknown = 0,
        Valid,
        Invalid,
        Stale,
    };

    export enum class SnapshotLifetimePolicy : std::uint8_t
    {
        FrameTransient = 0,
        Cached,
        Persistent,
        External,
    };

    export enum class BindingSemanticRole : std::uint8_t
    {
        Geometry = 0,
        Material,
        Camera,
        Light,
        Visibility,
        Environment,
        Output,
        Debug,
    };

    export enum class BindingSourceDomain : std::uint8_t
    {
        Unknown = 0,
        MeshVertex,
        MeshFace,
        GraphNode,
        GraphEdge,
        PointCloudPoint,
        Scene,
        Generated,
        Runtime,
    };

    export enum class BindingValueType : std::uint8_t
    {
        Unknown = 0,
        Float,
        UInt,
        Vec2,
        Vec3,
        Vec4,
        Mat4,
        Texture2D,
        Buffer,
        AccelerationStructure,
    };

    export enum class BindingRequirement : std::uint8_t
    {
        Required = 0,
        Optional,
    };

    export enum class BindingColorSpace : std::uint8_t
    {
        None = 0,
        Linear,
        SRGB,
        NormalizedData,
    };

    export enum class RecipeSlotKind : std::uint8_t
    {
        FixedCore = 0,
        Extension,
    };

    export enum class ViewKind : std::uint8_t
    {
        Camera = 0,
        NonCamera,
        Picking,
        Metrics,
        Preview,
    };

    export enum class OutputTargetKind : std::uint8_t
    {
        Window = 0,
        OffscreenTexture,
        File,
        ReadbackBuffer,
        PublishedArtifact,
    };

    export enum class InteractionMode : std::uint8_t
    {
        Interactive = 0,
        Headless,
    };

    export enum class RenderArtifactStatus : std::uint8_t
    {
        Declared = 0,
        Available,
        Stale,
        Missing,
        Failed,
        Published,
        Discarded,
    };

    export enum class RenderArtifactLifetime : std::uint8_t
    {
        Transient = 0,
        Cached,
        Published,
    };

    export enum class RenderArtifactLifecycleClass : std::uint8_t
    {
        Invalid = 0,
        Declared,
        TransientAvailable,
        CachedAvailable,
        Published,
        Stale,
        Missing,
        Failed,
        Discarded,
    };

    export enum class RenderingContractDiagnosticSeverity : std::uint8_t
    {
        Info = 0,
        Warning,
        Error,
    };

    export enum class RenderingContractDiagnosticCode : std::uint8_t
    {
        None = 0,
        EmptyRendererId,
        UnknownRendererPurpose,
        MissingSupportedSnapshotScope,
        MissingSupportedSnapshotKind,
        MissingUpdateMode,
        MissingRendererOutput,
        EmptySnapshotId,
        SnapshotRendererMismatch,
        UnsupportedSnapshotScope,
        UnsupportedSnapshotKind,
        InvalidSnapshotState,
        StaleSnapshot,
        MissingSnapshotData,
        DegradedSnapshot,
        EmptyBindingRole,
        MissingRequiredBinding,
        UnsupportedBindingCapability,
        EmptyRecipeId,
        UnknownRecipeSlot,
        UnsupportedRecipeCapability,
        DisallowedRecipeBinding,
        EmptyViewRecipeId,
        InvalidViewport,
        InvalidRenderScale,
        UnsupportedOutput,
        UnsupportedReadbackRequest,
        EmptyArtifactId,
        ArtifactRendererMismatch,
        ArtifactSnapshotMissing,
        ArtifactViewRecipeMissing,
        UndeclaredArtifactOutput,
    };

    export struct RendererOutputDescriptor
    {
        std::string Name{};
        RenderOutputKind Kind = RenderOutputKind::Color;
        bool Required = true;
    };

    export struct RendererDescriptor
    {
        std::string Id{};
        RendererPurpose Purpose = RendererPurpose::Unknown;
        std::vector<SnapshotScope> SupportedSnapshotScopes{};
        std::vector<SnapshotKind> SupportedSnapshotKinds{};
        std::vector<RendererUpdateMode> UpdateModes{};
        std::vector<RenderDataCategory> RequiredDataCategories{};
        std::vector<RenderDataCategory> OptionalDataCategories{};
        std::vector<RendererCapability> SupportedCapabilities{};
        std::vector<RendererOutputDescriptor> Outputs{};
        std::vector<std::string> DeclaredRecipeSlots{};
        RenderingFallbackPolicy FallbackPolicy = RenderingFallbackPolicy::FailClosed;
    };

    export struct SnapshotEnvelope
    {
        std::string Id{};
        SnapshotKind Kind = SnapshotKind::Unknown;
        SnapshotScope Scope = SnapshotScope::Unknown;
        std::string ProducerRendererId{};
        std::string ConsumerRendererId{};
        std::vector<std::string> SourceRevisions{};
        std::vector<std::string> DependencyHashes{};
        SnapshotValidationState ValidationState = SnapshotValidationState::Unknown;
        bool Stale = false;
        bool MissingData = false;
        bool Generated = false;
        bool Degraded = false;
        SnapshotLifetimePolicy Lifetime = SnapshotLifetimePolicy::FrameTransient;
        std::vector<std::string> Diagnostics{};
        std::string ReplayMetadata{};
        std::string ExportMetadata{};
    };

    export struct BindingIntent
    {
        BindingSemanticRole Role = BindingSemanticRole::Geometry;
        std::string SemanticName{};
        RenderDataCategory Category = RenderDataCategory::Geometry;
        BindingSourceDomain SourceDomain = BindingSourceDomain::Unknown;
        std::string SourceIdentity{};
        std::string SourceRevision{};
        BindingValueType ValueType = BindingValueType::Unknown;
        std::string ValueFormat{};
        BindingRequirement Requirement = BindingRequirement::Required;
        RenderingFallbackPolicy FallbackPolicy = RenderingFallbackPolicy::FailClosed;
        BindingColorSpace ColorSpace = BindingColorSpace::None;
        std::string Units{};
        bool HasRange = false;
        double MinValue = 0.0;
        double MaxValue = 0.0;
        std::string ConsumerRole{};
        std::string ConsumerPass{};
        std::string ConsumerLens{};
        std::optional<RendererCapability> RequiredCapability{};
    };

    export struct BindingSet
    {
        std::vector<BindingIntent> Intents{};
    };

    export struct RecipeExtensionSlotDescriptor
    {
        std::string StableName{};
        RecipeSlotKind Kind = RecipeSlotKind::Extension;
        std::string SchemaId{};
        std::string Defaults{};
        std::vector<RendererCapability> RequiredCapabilities{};
        std::vector<std::string> AllowedBindingRoles{};
        std::vector<std::string> UsedBindingRoles{};
        std::vector<std::string> ValidationRules{};
        RenderingFallbackPolicy FallbackPolicy = RenderingFallbackPolicy::FailClosed;
        std::vector<std::string> Diagnostics{};
    };

    export struct RenderRecipeDescriptor
    {
        std::string RecipeId{};
        std::string FixedCoreName{};
        std::vector<RecipeExtensionSlotDescriptor> Slots{};
    };

    export struct ViewOutputDescriptor
    {
        std::string Name{};
        RenderOutputKind Kind = RenderOutputKind::Color;
        std::string Format{};
        bool Required = true;
    };

    export struct ViewOutputRecipeDescriptor
    {
        std::string RecipeId{};
        ViewKind View = ViewKind::Camera;
        std::uint32_t ViewportWidth = 1u;
        std::uint32_t ViewportHeight = 1u;
        float RenderScale = 1.0f;
        OutputTargetKind Target = OutputTargetKind::Window;
        bool CaptureRequested = false;
        bool ReadbackRequested = false;
        InteractionMode Mode = InteractionMode::Interactive;
        std::vector<ViewOutputDescriptor> Outputs{};
    };

    export struct RenderArtifactMetadata
    {
        std::string ArtifactId{};
        std::string RendererId{};
        std::string SnapshotId{};
        std::string ViewOutputRecipeId{};
        std::vector<std::string> SourceRevisions{};
        RenderArtifactStatus Status = RenderArtifactStatus::Declared;
        RenderArtifactLifetime Lifetime = RenderArtifactLifetime::Transient;
        std::string Purpose{};
        std::vector<std::string> Diagnostics{};
    };

    export struct RenderingContractDiagnostic
    {
        RenderingContractDiagnosticCode Code = RenderingContractDiagnosticCode::None;
        RenderingContractDiagnosticSeverity Severity = RenderingContractDiagnosticSeverity::Info;
        std::string Subject{};
        std::string Message{};
    };

    export struct RenderingContractValidationResult
    {
        std::vector<RenderingContractDiagnostic> Diagnostics{};
    };

    export [[nodiscard]] std::string_view ToString(RendererPurpose value) noexcept;
    export [[nodiscard]] std::string_view ToString(SnapshotKind value) noexcept;
    export [[nodiscard]] std::string_view ToString(SnapshotScope value) noexcept;
    export [[nodiscard]] std::string_view ToString(RendererUpdateMode value) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderDataCategory value) noexcept;
    export [[nodiscard]] std::string_view ToString(RendererCapability value) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderOutputKind value) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderingContractDiagnosticCode value) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderingContractDiagnosticSeverity value) noexcept;

    export [[nodiscard]] bool HasErrors(const RenderingContractValidationResult& result) noexcept;
    export [[nodiscard]] bool IsCompatible(const RenderingContractValidationResult& result) noexcept;
    export [[nodiscard]] std::uint32_t CountBySeverity(const RenderingContractValidationResult& result,
                                                       RenderingContractDiagnosticSeverity severity) noexcept;
    export [[nodiscard]] bool HasDiagnostic(const RenderingContractValidationResult& result,
                                            RenderingContractDiagnosticCode code) noexcept;
    export void AppendDiagnostics(RenderingContractValidationResult& out,
                                  const RenderingContractValidationResult& in);
    export [[nodiscard]] RenderingContractValidationResult MergeDiagnostics(
        std::initializer_list<RenderingContractValidationResult> results);

    export [[nodiscard]] RenderingContractValidationResult ValidateRendererDescriptor(
        const RendererDescriptor& descriptor);
    export [[nodiscard]] RenderingContractValidationResult ValidateSnapshotEnvelope(
        const RendererDescriptor& renderer,
        const SnapshotEnvelope& snapshot);
    export [[nodiscard]] RenderingContractValidationResult ValidateBindingSet(
        const RendererDescriptor& renderer,
        const BindingSet& bindings);
    export [[nodiscard]] RenderingContractValidationResult ValidateRenderRecipeDescriptor(
        const RendererDescriptor& renderer,
        const RenderRecipeDescriptor& recipe);
    export [[nodiscard]] RenderingContractValidationResult ValidateViewOutputRecipe(
        const RendererDescriptor& renderer,
        const ViewOutputRecipeDescriptor& recipe);
    export [[nodiscard]] RenderingContractValidationResult ValidateRenderArtifactMetadata(
        const RendererDescriptor& renderer,
        const ViewOutputRecipeDescriptor& viewRecipe,
        const RenderArtifactMetadata& artifact);
    export [[nodiscard]] RenderingContractValidationResult ValidateRenderingContract(
        const RendererDescriptor& renderer,
        const SnapshotEnvelope& snapshot,
        const BindingSet& bindings,
        const RenderRecipeDescriptor& recipe,
        const ViewOutputRecipeDescriptor& viewRecipe);
    export [[nodiscard]] RenderArtifactLifecycleClass ClassifyRenderArtifactLifecycle(
        const RenderArtifactMetadata& artifact) noexcept;
}
