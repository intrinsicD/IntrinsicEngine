module;

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.SandboxEditorUi;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.SelectionController;
import Geometry.UvAtlas;

namespace Extrinsic::Runtime::Detail
{
    inline constexpr std::size_t kSandboxEditorPanelWindowCount = 9u;
}

export namespace Extrinsic::Runtime
{
    enum class SandboxEditorDiagnosticCode : std::uint8_t
    {
        MissingScene,
        MissingSelectionController,
        MissingImGuiAdapter,
        AssetImportUnavailable,
        AssetImportFailed,
        SceneFileUnavailable,
        SceneFileFailed,
        NoSelectedEntity,
        UnsupportedGeometryDomain,
        CameraRenderCommandsUnavailable,
        VisualizationCommandsUnavailable,
        InvalidVisualizationProperty,
        GeometryProcessingFailed,
        RenderGraphStatsUnavailable,
        EditorCommandHistoryUnavailable,
    };

    enum class SandboxEditorCommandStatus : std::uint8_t
    {
        Applied,
        NoChange,
        MissingScene,
        MissingSelectionController,
        MissingCameraControllerRegistry,
        MissingAssetImportCommands,
        MissingSceneFileCommands,
        MissingPrimitiveViewCommands,
        MissingVisualizationCommands,
        AssetImportFailed,
        SceneNewFailed,
        SceneSaveFailed,
        SceneLoadFailed,
        SceneCloseFailed,
        StaleEntity,
        MissingTransform,
        UnsupportedGeometryDomain,
        InvalidVisualizationProperty,
        InvalidProcessingParameters,
        GeometryProcessingFailed,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorDiagnosticCode(
        SandboxEditorDiagnosticCode code) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorCommandStatus(
        SandboxEditorCommandStatus status) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryDomain(
        ECS::Components::GeometrySources::Domain domain) noexcept;

    enum class SandboxEditorDomainWindowKind : std::uint8_t
    {
        Mesh,
        Graph,
        PointCloud,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorDomainWindowKind(
        SandboxEditorDomainWindowKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorPrimitiveKind(
        RefinedPrimitiveKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorCameraControllerKind(
        Core::Config::CameraControllerKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorSpatialDebugKind(
        ECS::Components::SpatialDebugGeometryKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationColorSource(
        Graphics::Components::VisualizationConfig::ColorSource source) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationDomain(
        Graphics::Components::VisualizationConfig::Domain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationAdapterBindingKind(
        RenderExtractionCache::VisualizationAdapterBindingKind kind) noexcept;

    enum class SandboxEditorVisualizationPropertyDomain : std::uint8_t
    {
        MeshVertices,
        MeshEdges,
        MeshFaces,
        GraphVertices,
        GraphEdges,
        PointCloudPoints,
    };

    enum class SandboxEditorVisualizationPropertyValueKind : std::uint8_t
    {
        ScalarFloat,
        ScalarDouble,
        Vec3,
        Vec4,
        UInt32,
    };

    enum class SandboxEditorVisualizationPropertyPreset : std::uint8_t
    {
        Scalar,
        Isoline,
        ColorBuffer,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationPropertyDomain(
        SandboxEditorVisualizationPropertyDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationPropertyValueKind(
        SandboxEditorVisualizationPropertyValueKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationPropertyPreset(
        SandboxEditorVisualizationPropertyPreset preset) noexcept;

    enum class SandboxEditorGeometryProcessingDomain : std::uint32_t
    {
        None = 0u,
        MeshVertices = 1u << 0u,
        MeshEdges = 1u << 1u,
        MeshHalfedges = 1u << 2u,
        MeshFaces = 1u << 3u,
        GraphVertices = 1u << 4u,
        GraphEdges = 1u << 5u,
        GraphHalfedges = 1u << 6u,
        PointCloudPoints = 1u << 7u,
    };

    [[nodiscard]] constexpr SandboxEditorGeometryProcessingDomain operator|(
        const SandboxEditorGeometryProcessingDomain lhs,
        const SandboxEditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<SandboxEditorGeometryProcessingDomain>(
            static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr SandboxEditorGeometryProcessingDomain operator&(
        const SandboxEditorGeometryProcessingDomain lhs,
        const SandboxEditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<SandboxEditorGeometryProcessingDomain>(
            static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
    }

    constexpr SandboxEditorGeometryProcessingDomain& operator|=(
        SandboxEditorGeometryProcessingDomain& lhs,
        const SandboxEditorGeometryProcessingDomain rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    [[nodiscard]] constexpr bool HasAnySandboxEditorGeometryProcessingDomain(
        const SandboxEditorGeometryProcessingDomain domains,
        const SandboxEditorGeometryProcessingDomain query) noexcept
    {
        return static_cast<std::uint32_t>(domains & query) != 0u;
    }

    enum class SandboxEditorGeometryProcessingAlgorithm : std::uint8_t
    {
        KMeans,
        Remeshing,
        Simplification,
        Smoothing,
        Subdivision,
        Repair,
        NormalEstimation,
        ShortestPath,
        ConvexHull,
        SurfaceReconstruction,
        VectorHeat,
        Parameterization,
        BooleanCSG,
        Registration,
        BilateralFilter,
        OutlierEstimation,
        KernelDensity,
    };

    struct SandboxEditorGeometryProcessingCapabilities
    {
        SandboxEditorGeometryProcessingDomain Domains{
            SandboxEditorGeometryProcessingDomain::None};
        bool HasEditableSurfaceMesh{false};

        [[nodiscard]] bool HasAny() const noexcept
        {
            return HasEditableSurfaceMesh ||
                   Domains != SandboxEditorGeometryProcessingDomain::None;
        }
    };

    struct SandboxEditorGeometryProcessingEntry
    {
        SandboxEditorGeometryProcessingAlgorithm Algorithm{
            SandboxEditorGeometryProcessingAlgorithm::KMeans};
        SandboxEditorGeometryProcessingDomain Domains{
            SandboxEditorGeometryProcessingDomain::None};
    };

    [[nodiscard]] SandboxEditorGeometryProcessingDomain
    GetSandboxEditorSupportedGeometryProcessingDomains(
        SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept;

    [[nodiscard]] bool SupportsSandboxEditorGeometryProcessingDomain(
        SandboxEditorGeometryProcessingAlgorithm algorithm,
        SandboxEditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] SandboxEditorGeometryProcessingCapabilities
    GetSandboxEditorGeometryProcessingCapabilities(
        const ECS::Scene::Registry& registry,
        ECS::EntityHandle entity);

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        SandboxEditorGeometryProcessingCapabilities capabilities);

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const ECS::Scene::Registry& registry,
        ECS::EntityHandle entity);

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingDomain>
    GetAvailableSandboxEditorKMeansDomains(
        const ECS::Scene::Registry& registry,
        ECS::EntityHandle entity);

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryProcessingDomain(
        SandboxEditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorGeometryProcessingAlgorithm(
        SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept;

    struct SandboxEditorKMeansCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorGeometryProcessingDomain Domain{
            SandboxEditorGeometryProcessingDomain::PointCloudPoints};
        std::uint32_t ClusterCount{8u};
        std::uint32_t MaxIterations{32u};
        std::uint32_t Seed{42u};
        bool UseHierarchicalInitialization{true};
    };

    struct SandboxEditorKMeansResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorGeometryProcessingDomain Domain{
            SandboxEditorGeometryProcessingDomain::None};
        std::uint32_t LabelCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        float Inertia{0.0f};
        std::uint32_t MaxDistanceIndex{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorDiagnostic
    {
        SandboxEditorDiagnosticCode Code{SandboxEditorDiagnosticCode::MissingScene};
        std::string                 Message{};
    };

    struct SandboxEditorEntityRow
    {
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t     StableEntityId{0u};
        std::string       Name{};
        bool              Selectable{false};
        bool              Selected{false};
        bool              Hovered{false};
        bool              HasDurableStableId{false};
        ECS::Components::StableId DurableStableId{};
    };

    struct SandboxEditorTransformModel
    {
        bool      HasLocalTransform{false};
        glm::vec3 LocalPosition{0.0f};
        glm::quat LocalRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 LocalScale{1.0f};
        bool      HasWorldTransform{false};
        glm::vec3 WorldPosition{0.0f};
    };

    struct SandboxEditorRenderHintModel
    {
        bool HasRenderSurface{false};
        std::string SurfaceDomain{};
        Graphics::Components::RenderSurface::SourceDomain SurfaceDomainValue{
            Graphics::Components::RenderSurface::SourceDomain::Vertex};
        bool HasRenderEdges{false};
        std::string EdgeDomain{};
        Graphics::Components::RenderEdges::SourceDomain EdgeDomainValue{
            Graphics::Components::RenderEdges::SourceDomain::Vertex};
        bool        HasUniformEdgeWidth{false};
        float       UniformEdgeWidth{0.0f};
        bool        HasNamedEdgeWidth{false};
        std::string EdgeWidthName{};
        bool HasRenderPoints{false};
        std::string PointRenderType{};
        Graphics::Components::RenderPoints::RenderType PointRenderTypeValue{
            Graphics::Components::RenderPoints::RenderType::Sphere};
        bool        HasUniformPointSize{false};
        float       UniformPointSize{0.0f};
        bool        HasNamedPointSize{false};
        std::string PointSizeName{};
    };

    struct SandboxEditorGeometryDomainModel
    {
        ECS::Components::GeometrySources::Domain Domain{
            ECS::Components::GeometrySources::Domain::None};
        bool        Valid{false};
        std::size_t VertexCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t HalfedgeCount{0u};
        std::size_t FaceCount{0u};
        std::size_t NodeCount{0u};
    };

    struct SandboxEditorProgressivePropertyOptionModel
    {
        ProgressivePropertyBindingDescriptor Descriptor{};
        ProgressivePropertyValueKind ActualValueKind{
            ProgressivePropertyValueKind::Unknown};
        std::size_t ElementCount{0u};
        bool Compatible{false};
        std::string DisabledReason{};
    };

    enum class SandboxEditorPropertyCatalogDomain : std::uint8_t
    {
        MeshVertices,
        MeshEdges,
        MeshHalfedges,
        MeshFaces,
        GraphVertices,
        GraphEdges,
        PointCloudPoints,
    };

    enum class SandboxEditorPropertyCatalogValueKind : std::uint8_t
    {
        Unknown,
        ScalarFloat,
        ScalarDouble,
        UInt32,
        Vec2,
        Vec3,
        Vec4,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorPropertyCatalogDomain(
        SandboxEditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorPropertyCatalogValueKind(
        SandboxEditorPropertyCatalogValueKind kind) noexcept;

    struct SandboxEditorPropertyValuePreview
    {
        bool HasValue{false};
        std::size_t ElementIndex{0u};
        std::string Text{};
    };

    struct SandboxEditorPropertyCatalogRow
    {
        std::string Name{};
        SandboxEditorPropertyCatalogDomain Domain{
            SandboxEditorPropertyCatalogDomain::MeshVertices};
        SandboxEditorPropertyCatalogValueKind ValueKind{
            SandboxEditorPropertyCatalogValueKind::Unknown};
        std::size_t ElementCount{0u};
        std::uint8_t ComponentCount{0u};
        bool Supported{false};
        bool Bindable{false};
        bool Canonical{false};
        bool Internal{false};
        bool Connectivity{false};
        bool Generated{false};
        std::string UnsupportedReason{};
        ProgressivePropertyBindingDescriptor Descriptor{};
        SandboxEditorPropertyValuePreview Preview{};
    };

    struct SandboxEditorPropertyBindingTargetModel
    {
        ProgressiveRenderLane Lane{ProgressiveRenderLane::Surface};
        std::string PresentationKey{};
        ProgressivePresentationKind PresentationKind{
            ProgressivePresentationKind::SurfaceMaterial};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveSlotSourceKind SourceKind{
            ProgressiveSlotSourceKind::UniformDefault};
        ProgressiveGeometryDomain RequiredDomain{
            ProgressiveGeometryDomain::Unknown};
        ProgressivePropertyValueKind ExpectedValueKind{
            ProgressivePropertyValueKind::Any};
        std::size_t ExpectedElementCount{0u};
        std::vector<SandboxEditorProgressivePropertyOptionModel> Options{};
    };

    struct SandboxEditorPropertyCatalogModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        std::vector<SandboxEditorPropertyCatalogRow> Rows{};
        std::vector<SandboxEditorPropertyBindingTargetModel> BindingTargets{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorProgressiveSlotModel
    {
        ProgressiveRenderLane Lane{ProgressiveRenderLane::Surface};
        std::string PresentationKey{};
        ProgressivePresentationKind PresentationKind{
            ProgressivePresentationKind::SurfaceMaterial};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveSlotSourceKind SourceKind{
            ProgressiveSlotSourceKind::UniformDefault};
        ProgressiveReadinessState Readiness{ProgressiveReadinessState::Unset};
        ProgressiveDefaultValue UniformDefault{};
        ProgressivePropertyBindingDescriptor Property{};
        ProgressivePropertyResolution PropertyResolution{};
        Assets::AssetId AuthoredTexture{};
        Assets::AssetId GeneratedTexture{};
        Assets::AssetId TextureAsset{};
        bool Enabled{false};
        bool UsesUniformDefault{false};
        bool TextureReady{false};
        bool PropertyBufferReady{false};
        bool PreviousOutputRetained{false};
        bool Unsupported{false};
        std::string Diagnostic{};
        std::vector<SandboxEditorProgressivePropertyOptionModel> PropertyOptions{};
    };

    struct SandboxEditorProgressiveJobDependencyModel
    {
        DerivedJobHandle Job{};
        std::string Reason{};
    };

    struct SandboxEditorProgressiveJobModel
    {
        DerivedJobHandle Handle{};
        DerivedJobKey Key{};
        std::string Name{};
        ProgressiveJobDomain RequestedJobDomain{ProgressiveJobDomain::Cpu};
        ProgressiveJobDomain ResolvedJobDomain{ProgressiveJobDomain::Cpu};
        DerivedJobStatus Status{DerivedJobStatus::Queued};
        std::vector<SandboxEditorProgressiveJobDependencyModel> Dependencies{};
        float NormalizedProgress{0.0f};
        bool ProgressDeterminate{true};
        bool PreviousOutputRetained{false};
        std::uint64_t PayloadToken{0u};
        std::uint64_t ElapsedMilliseconds{0u};
        std::string Diagnostic{};
    };

    struct SandboxEditorProgressiveCompositionSummary
    {
        bool HasChildren{false};
        std::uint32_t ChildCount{0u};
        std::uint32_t ChildBindingsCount{0u};
        std::uint32_t ChildSlotCount{0u};
        std::uint32_t ChildPendingSlotCount{0u};
        std::uint32_t ChildFailedSlotCount{0u};
        std::uint32_t ChildJobCount{0u};
        std::uint32_t ChildActiveJobCount{0u};
        std::uint32_t ChildFailedJobCount{0u};
    };

    struct SandboxEditorProgressiveRenderDataModel
    {
        bool HasBindings{false};
        ProgressiveEntityShape Shape{ProgressiveEntityShape::Unknown};
        std::uint64_t BindingGeneration{0u};
        ProgressivePresentationExtractionStats Stats{};
        std::vector<SandboxEditorProgressiveSlotModel> Slots{};
        std::vector<SandboxEditorProgressiveJobModel> Jobs{};
        SandboxEditorProgressiveCompositionSummary Composition{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    enum class SandboxEditorBoundRenderStateRowKind : std::uint8_t
    {
        RenderHint,
        ProgressiveSlot,
        DerivedJob,
        CompositionSummary,
        DisabledCommand,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorBoundRenderStateRowKind(
        SandboxEditorBoundRenderStateRowKind kind) noexcept;

    struct SandboxEditorBoundRenderStateRow
    {
        SandboxEditorBoundRenderStateRowKind Kind{
            SandboxEditorBoundRenderStateRowKind::ProgressiveSlot};
        std::string Label{};
        ProgressiveRenderLane Lane{ProgressiveRenderLane::Surface};
        std::string PresentationKey{};
        ProgressivePresentationKind PresentationKind{
            ProgressivePresentationKind::SurfaceMaterial};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveSlotSourceKind SourceKind{
            ProgressiveSlotSourceKind::UniformDefault};
        ProgressiveReadinessState Readiness{ProgressiveReadinessState::Unset};
        ProgressivePropertyBindingDescriptor Property{};
        ProgressivePropertyResolution PropertyResolution{};
        Assets::AssetId AuthoredTexture{};
        Assets::AssetId GeneratedTexture{};
        Assets::AssetId TextureAsset{};
        DerivedJobHandle Job{};
        DerivedJobStatus JobStatus{DerivedJobStatus::Queued};
        float JobProgress{0.0f};
        bool JobProgressDeterminate{true};
        bool Enabled{false};
        bool UsesUniformDefault{false};
        bool TextureReady{false};
        bool PropertyBufferReady{false};
        bool PreviousOutputRetained{false};
        bool Unsupported{false};
        bool HasCatalogMatch{false};
        std::optional<std::size_t> CatalogRowIndex{};
        std::string SourceDescription{};
        std::string DisabledReason{};
        std::string Diagnostic{};
    };

    struct SandboxEditorBoundRenderStateModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ProgressiveEntityShape Shape{ProgressiveEntityShape::Unknown};
        std::uint64_t BindingGeneration{0u};
        std::vector<SandboxEditorBoundRenderStateRow> Rows{};
        SandboxEditorProgressiveCompositionSummary Composition{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    enum class SandboxEditorTextureBakeSourceCategory : std::uint8_t
    {
        Bakeable,
        Internal,
        Connectivity,
        Unsupported,
        WrongDomain,
    };

    struct SandboxEditorTextureBakeSourceRow
    {
        std::string Name{};
        SandboxEditorPropertyCatalogDomain CatalogDomain{
            SandboxEditorPropertyCatalogDomain::MeshVertices};
        ProgressiveGeometryDomain BakeDomain{ProgressiveGeometryDomain::Unknown};
        SandboxEditorPropertyCatalogValueKind ValueKind{
            SandboxEditorPropertyCatalogValueKind::Unknown};
        ProgressivePropertyValueKind ExpectedValueKind{
            ProgressivePropertyValueKind::Any};
        std::size_t ElementCount{0u};
        SandboxEditorTextureBakeSourceCategory Category{
            SandboxEditorTextureBakeSourceCategory::Unsupported};
        bool Bakeable{false};
        std::string DisabledReason{};
        ProgressivePropertyBindingDescriptor Descriptor{};
    };

    struct SandboxEditorUvDiagnosticsModel
    {
        bool HasSelectedEntity{false};
        bool IsMesh{false};
        bool HasTexcoords{false};
        bool TexcoordCountMatchesVertices{false};
        bool TexcoordsFinite{false};
        std::string TexcoordPropertyName{"v:texcoord"};
        std::size_t VertexCount{0u};
        std::size_t TexcoordCount{0u};
        std::size_t FaceCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint32_t ChartCount{0u};
        std::uint32_t SeamSplitVertexCount{0u};
        std::string Provenance{};
        std::string BackendId{};
        std::string LastFailure{};
        bool CheckerPreviewAvailable{false};
        bool UvRegenerationAvailable{false};
        std::string UvRegenerationDisabledReason{};
    };

    struct SandboxEditorTextureBakeControlsModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        bool IsMesh{false};
        bool HasRuntimeBakeCommand{false};
        bool CanBake{false};
        std::string DisabledReason{};
        ProgressiveSlotSemantic DefaultTargetSemantic{
            ProgressiveSlotSemantic::Albedo};
        MeshAttributeTextureBakeEncoder DefaultEncoder{
            MeshAttributeTextureBakeEncoder::Auto};
        std::uint32_t DefaultWidth{64u};
        std::uint32_t DefaultHeight{64u};
        SandboxEditorUvDiagnosticsModel Uv{};
        std::vector<SandboxEditorTextureBakeSourceRow> Sources{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorInspectorModel
    {
        bool                             HasEntity{false};
        SandboxEditorEntityRow          Entity{};
        SandboxEditorTransformModel     Transform{};
        SandboxEditorRenderHintModel    RenderHints{};
        SandboxEditorGeometryDomainModel Geometry{};
        SandboxEditorPropertyCatalogModel PropertyCatalog{};
        SandboxEditorProgressiveRenderDataModel Progressive{};
        SandboxEditorBoundRenderStateModel BoundState{};
        SandboxEditorTextureBakeControlsModel TextureBake{};
        SandboxEditorGeometryProcessingCapabilities Processing{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPrimitiveDetailModel
    {
        bool HasPrimitive{false};
        PrimitiveSelectionResult Primitive{};
        bool HasFaceId{false};
        bool HasEdgeId{false};
        bool HasVertexId{false};
        bool HasPointId{false};
    };

    struct SandboxEditorSelectionModel
    {
        std::vector<std::uint32_t> SelectedStableIds{};
        std::vector<SandboxEditorEntityRow> SelectedEntities{};
        bool                       HasHovered{false};
        std::uint32_t              HoveredStableId{0u};
        bool                       HasHoveredEntity{false};
        SandboxEditorEntityRow     HoveredEntity{};
        SandboxEditorPrimitiveDetailModel Primitive{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    enum class SandboxEditorSceneFileOperation : std::uint8_t
    {
        New,
        Save,
        Load,
        Close,
    };

    struct SandboxEditorSceneFileCommand
    {
        std::string Path{};
    };

    struct SandboxEditorSceneFileResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorSceneFileOperation Operation{SandboxEditorSceneFileOperation::Save};
        SceneSerializationStats Stats{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorSceneFileCommandSurface
    {
        std::function<SandboxEditorSceneFileResult()> New{};
        std::function<SandboxEditorSceneFileResult(
            const SandboxEditorSceneFileCommand&)> Save{};
        std::function<SandboxEditorSceneFileResult(
            const SandboxEditorSceneFileCommand&)> Load{};
        std::function<SandboxEditorSceneFileResult()> Close{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Save) && static_cast<bool>(Load);
        }

        [[nodiscard]] bool LifecycleAvailable() const noexcept
        {
            return static_cast<bool>(New) && static_cast<bool>(Close);
        }
    };

    struct SandboxEditorSceneFileModel
    {
        bool        Enabled{false};
        bool        LifecycleEnabled{false};
        bool        PathEntryEnabled{true};
        bool        NativeDialogsAvailable{false};
        bool        CanNew{false};
        bool        CanClose{false};
        bool        CanSave{false};
        bool        CanOpen{false};
        std::string PendingPath{};
        std::optional<SandboxEditorSceneFileResult> LastResult{};
        std::string StatusText{};
        std::string FileDialogBoundaryText{
            "Native file dialogs are deferred; use path entry or dropped paths."};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorDocumentModel
    {
        bool HistoryAvailable{false};
        bool Dirty{false};
        bool CanUndo{false};
        bool CanRedo{false};
        bool HasActivePath{false};
        std::string ActivePath{};
        std::string UndoLabel{};
        std::string RedoLabel{};
        std::uint64_t Revision{0u};
        std::uint64_t SavedRevision{0u};
        std::string StatusText{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorFileImportCommand
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    struct SandboxEditorFileImportResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t GeneratedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        std::uint64_t GeneratedTextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorAssetImportCommandSurface
    {
        std::function<SandboxEditorFileImportResult(
            const SandboxEditorFileImportCommand&)> Import{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Import);
        }
    };

    struct SandboxEditorFileImportModel
    {
        bool        Enabled{false};
        std::string PendingPath{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::optional<SandboxEditorFileImportResult> LastResult{};
        std::string StatusText{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorAssetImportQueueCommandSurface
    {
        std::function<std::size_t()> ClearCompleted{};
        std::function<Core::Result(RuntimeAssetIngestHandle)> Cancel{};

        [[nodiscard]] bool ClearAvailable() const noexcept
        {
            return static_cast<bool>(ClearCompleted);
        }

        [[nodiscard]] bool CancelAvailable() const noexcept
        {
            return static_cast<bool>(Cancel);
        }
    };

    struct SandboxEditorAssetImportQueueRow
    {
        RuntimeAssetIngestHandle Operation{};
        std::uint64_t Sequence{0u};
        RuntimeAssetIngestSource Source{RuntimeAssetIngestSource::ManualImport};
        std::string SourcePath{};
        std::string PathBasename{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Assets::AssetId Asset{};
        RuntimeAssetImportQueueStage Stage{RuntimeAssetImportQueueStage::Queued};
        RuntimeAssetImportQueueTerminalStatus TerminalStatus{
            RuntimeAssetImportQueueTerminalStatus::None};
        bool ProgressDeterminate{true};
        float NormalizedProgress{0.0f};
        std::string StageText{};
        std::string DiagnosticText{};
        double ElapsedSeconds{0.0};
        bool CanCancel{false};
        std::string CancelDisabledReason{};
    };

    struct SandboxEditorAssetImportQueueModel
    {
        std::vector<SandboxEditorAssetImportQueueRow> Rows{};
        std::size_t ActiveCount{0u};
        std::size_t TerminalCount{0u};
        bool ClearCompletedAvailable{false};
        bool CanClearCompleted{false};
        std::string ClearCompletedDisabledReason{};
        std::string StatusText{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorRenderGraphPassModel
    {
        std::string Name{};
        bool HasTypedId{false};
        std::uint32_t TypedId{0u};
        std::string Status{};
    };

    struct SandboxEditorRenderGraphModel
    {
        bool Enabled{false};
        bool CompileSucceeded{false};
        bool ExecuteSucceeded{false};
        bool DeviceOperational{false};
        std::uint32_t PassCount{0u};
        std::uint32_t CulledPassCount{0u};
        std::uint32_t ResourceCount{0u};
        std::uint32_t BarrierCount{0u};
        std::uint32_t QueueHandoffEdgeCount{0u};
        std::uint32_t CrossQueueTimelineEdgeCount{0u};
        std::uint32_t CrossQueueTimelineSignalCount{0u};
        std::uint32_t CrossQueueTimelineWaitCount{0u};
        std::uint32_t CrossQueueOwnershipTransferCount{0u};
        std::uint64_t TransientMemoryEstimateBytes{0u};
        std::uint64_t CompileTimeMicros{0u};
        std::uint64_t ExecuteTimeMicros{0u};
        std::uint32_t CommandPassesRecorded{0u};
        std::uint32_t CommandPassesSkipped{0u};
        std::uint32_t CommandPassesSkippedNonOperational{0u};
        std::uint32_t CommandPassesSkippedUnavailable{0u};
        std::uint32_t AsyncComputeUtilizedFrames{0u};
        std::string StatusText{};
        std::string Diagnostic{};
        std::string LifecycleDiagnostic{};
        std::string DebugDump{};
        std::vector<SandboxEditorRenderGraphPassModel> CommandPasses{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPrimitiveViewSettings
    {
        bool EnableEdgeView{false};
        bool EnableVertexView{false};
        MeshVertexViewRenderMode VertexRenderMode{
            MeshVertexViewRenderMode::ImpostorSphere};
        float VertexPointRadiusPx{6.0f};

        [[nodiscard]] bool AnyEnabled() const noexcept
        {
            return EnableEdgeView || EnableVertexView;
        }
    };

    struct SandboxEditorPrimitiveViewCommandSurface
    {
        std::function<SandboxEditorPrimitiveViewSettings(std::uint32_t)> GetSettings{};
        std::function<void(std::uint32_t, SandboxEditorPrimitiveViewSettings)> SetSettings{};
        std::function<void(std::uint32_t)> ClearSettings{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(GetSettings) &&
                   static_cast<bool>(SetSettings) &&
                   static_cast<bool>(ClearSettings);
        }
    };

    struct SandboxEditorVisualizationAdapterBindingCommandSurface
    {
        std::function<std::optional<RenderExtractionCache::VisualizationAdapterBinding>(std::uint32_t)>
            GetBinding{};
        std::function<void(std::uint32_t, RenderExtractionCache::VisualizationAdapterBinding)>
            SetBinding{};
        std::function<void(std::uint32_t)> ClearBinding{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(GetBinding) &&
                   static_cast<bool>(SetBinding) &&
                   static_cast<bool>(ClearBinding);
        }
    };

    struct SandboxEditorCameraRenderModel
    {
        bool CameraControlsAvailable{false};
        bool RenderSettingsAvailable{false};
        bool PrimitiveViewControlsAvailable{false};
        bool HasMainCameraController{false};
        Core::Config::CameraControllerKind MainCameraControllerKind{
            Core::Config::CameraControllerKind::Orbit};
        bool HasPrimitiveViewEntity{false};
        std::uint32_t PrimitiveViewStableId{0u};
        SandboxEditorPrimitiveViewSettings PrimitiveView{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorSpatialDebugBindingModel
    {
        bool HasBinding{false};
        ECS::Components::SpatialDebugGeometryKind Kind{
            ECS::Components::SpatialDebugGeometryKind::Bvh};
        std::uint64_t RegistryKey{0u};
        bool LeafOnly{false};
        bool OccupancyOnly{false};
        std::uint32_t MaxDepth{32u};
    };

    struct SandboxEditorVisualizationConfigModel
    {
        bool HasConfig{false};
        Graphics::Components::VisualizationConfig::ColorSource Source{
            Graphics::Components::VisualizationConfig::ColorSource::Material};
        glm::vec4 Color{1.0f, 0.6f, 0.0f, 1.0f};
        std::string ScalarFieldName{};
        Graphics::Components::VisualizationConfig::Domain ScalarDomain{
            Graphics::Components::VisualizationConfig::Domain::Vertex};
        std::string ColorBufferName{};
        bool ScalarAutoRange{true};
        float ScalarRangeMin{0.0f};
        float ScalarRangeMax{1.0f};
        std::uint32_t ScalarBinCount{0u};
        std::uint32_t IsolineCount{0u};
    };

    struct SandboxEditorVisualizationPropertyInfo
    {
        std::string Name{};
        SandboxEditorVisualizationPropertyDomain Domain{
            SandboxEditorVisualizationPropertyDomain::MeshVertices};
        SandboxEditorVisualizationPropertyValueKind ValueKind{
            SandboxEditorVisualizationPropertyValueKind::ScalarFloat};
        std::size_t ElementCount{0u};
        bool ScalarPresetAvailable{false};
        bool IsolinePresetAvailable{false};
        bool ColorBufferPresetAvailable{false};
        bool VectorFieldCandidate{false};
    };

    struct SandboxEditorVisualizationAdapterBindingModel
    {
        bool HasBinding{false};
        std::uint64_t AdapterKey{0u};
        std::uint64_t BufferBDA{0u};
        RenderExtractionCache::VisualizationAdapterBindingKind Kind{
            RenderExtractionCache::VisualizationAdapterBindingKind::Scalar};
        VisualizationAdapterOptions Options{};
    };

    struct SandboxEditorVisualizationModel
    {
        bool GeometryDomainControlsAvailable{false};
        bool AdapterBindingControlsAvailable{false};
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        SandboxEditorSpatialDebugBindingModel SpatialDebug{};
        SandboxEditorVisualizationConfigModel Visualization{};
        std::vector<SandboxEditorVisualizationPropertyInfo> Properties{};
        SandboxEditorVisualizationAdapterBindingModel AdapterBinding{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorGeometryProcessingModel
    {
        bool HasSelectedEntity{false};
        SandboxEditorGeometryProcessingCapabilities Capabilities{};
        std::vector<SandboxEditorGeometryProcessingEntry> Entries{};
        std::vector<SandboxEditorGeometryProcessingDomain> KMeansDomains{};
        std::optional<SandboxEditorKMeansResult> LastKMeansResult{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorDomainWindowModel
    {
        SandboxEditorDomainWindowKind Kind{SandboxEditorDomainWindowKind::Mesh};
        ECS::Components::GeometrySources::Domain ExpectedDomain{
            ECS::Components::GeometrySources::Domain::Mesh};
        bool HasSelectedEntity{false};
        SandboxEditorEntityRow SelectedEntity{};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        bool DomainMatches{false};
        SandboxEditorRenderHintModel RenderHints{};
        bool PrimitiveViewControlsAvailable{false};
        bool HasPrimitiveViewSettings{false};
        SandboxEditorPrimitiveViewSettings PrimitiveView{};
        bool VisualizationControlsAvailable{false};
        SandboxEditorVisualizationModel Visualization{};
        SandboxEditorPropertyCatalogModel PropertyCatalog{};
        SandboxEditorBoundRenderStateModel BoundState{};
        SandboxEditorTextureBakeControlsModel TextureBake{};
        SandboxEditorGeometryProcessingModel Processing{};
        SandboxEditorPrimitiveDetailModel Primitive{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorPanelFrame
    {
        std::vector<SandboxEditorEntityRow> Hierarchy{};
        SandboxEditorInspectorModel         Inspector{};
        SandboxEditorSelectionModel         Selection{};
        SandboxEditorDocumentModel          Document{};
        SandboxEditorSceneFileModel        SceneFile{};
        SandboxEditorFileImportModel        FileImport{};
        SandboxEditorAssetImportQueueModel  AssetImportQueue{};
        SandboxEditorRenderGraphModel       RenderGraph{};
        SandboxEditorCameraRenderModel      CameraRender{};
        SandboxEditorVisualizationModel     Visualization{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        SelectionController*  Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        Assets::AssetService* AssetService{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        SandboxEditorAssetImportCommandSurface AssetImportCommands{};
        SandboxEditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        SandboxEditorSceneFileCommandSurface SceneFileCommands{};
        SandboxEditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        SandboxEditorVisualizationAdapterBindingCommandSurface VisualizationAdapterBindings{};
        RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        const DerivedJobQueueSnapshot* DerivedJobs{nullptr};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        const SandboxEditorFileImportResult* LastAssetImportResult{nullptr};
        const SandboxEditorSceneFileResult* LastSceneFileResult{nullptr};
        const SandboxEditorKMeansResult* LastKMeansResult{nullptr};
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool SceneFileCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
        bool VisualizationCommandsAvailable{false};
    };

    struct SandboxEditorTransformEditCommand
    {
        std::uint32_t StableEntityId{0u};
        bool SetPosition{false};
        glm::vec3 Position{0.0f};
        bool SetRotation{false};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool SetScale{false};
        glm::vec3 Scale{1.0f};
    };

    struct SandboxEditorCameraControllerCommand
    {
        CameraControllerSlot Slot{CameraControllerSlot::Main};
        Core::Config::CameraControllerKind Kind{
            Core::Config::CameraControllerKind::Orbit};
        bool PreserveCurrentView{true};
        Core::Extent2D Viewport{};
    };

    struct SandboxEditorPrimitiveViewCommand
    {
        std::uint32_t StableEntityId{0u};
        bool SetEdgeView{false};
        bool EnableEdgeView{false};
        bool SetVertexView{false};
        bool EnableVertexView{false};
        bool SetVertexRenderMode{false};
        MeshVertexViewRenderMode VertexRenderMode{
            MeshVertexViewRenderMode::ImpostorSphere};
        bool SetVertexPointRadius{false};
        float VertexPointRadiusPx{6.0f};
    };

    struct SandboxEditorRenderHintCommand
    {
        std::uint32_t StableEntityId{0u};

        bool SetSurface{false};
        bool EnableSurface{false};
        Graphics::Components::RenderSurface::SourceDomain SurfaceDomain{
            Graphics::Components::RenderSurface::SourceDomain::Vertex};

        bool SetEdges{false};
        bool EnableEdges{false};
        Graphics::Components::RenderEdges::SourceDomain EdgeDomain{
            Graphics::Components::RenderEdges::SourceDomain::Vertex};
        bool SetUniformEdgeWidth{false};
        float UniformEdgeWidth{1.0f};

        bool SetPoints{false};
        bool EnablePoints{false};
        Graphics::Components::RenderPoints::RenderType PointType{
            Graphics::Components::RenderPoints::RenderType::Sphere};
        bool SetPointRenderType{false};
        bool SetUniformPointSize{false};
        float UniformPointSize{6.0f};
    };

    struct SandboxEditorSpatialDebugBindingCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableBinding{true};
        ECS::Components::SpatialDebugGeometryKind Kind{
            ECS::Components::SpatialDebugGeometryKind::Bvh};
        std::uint64_t RegistryKey{0u};
        bool LeafOnly{false};
        bool OccupancyOnly{false};
        std::uint32_t MaxDepth{32u};
    };

    struct SandboxEditorVisualizationConfigCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableConfig{true};
        Graphics::Components::VisualizationConfig::ColorSource Source{
            Graphics::Components::VisualizationConfig::ColorSource::UniformColor};
        glm::vec4 Color{1.0f, 0.6f, 0.0f, 1.0f};
        std::string ScalarFieldName{};
        Graphics::Components::VisualizationConfig::Domain ScalarDomain{
            Graphics::Components::VisualizationConfig::Domain::Vertex};
        std::string ColorBufferName{};
        bool ScalarAutoRange{true};
        float ScalarRangeMin{0.0f};
        float ScalarRangeMax{1.0f};
        std::uint32_t ScalarBinCount{0u};
        std::uint32_t IsolineCount{0u};
    };

    struct SandboxEditorVisualizationPropertyCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorVisualizationPropertyDomain Domain{
            SandboxEditorVisualizationPropertyDomain::MeshVertices};
        SandboxEditorVisualizationPropertyPreset Preset{
            SandboxEditorVisualizationPropertyPreset::Scalar};
        std::string PropertyName{};
        bool ScalarAutoRange{true};
        float ScalarRangeMin{0.0f};
        float ScalarRangeMax{1.0f};
        std::uint32_t ScalarBinCount{0u};
        std::uint32_t IsolineCount{12u};
    };

    struct SandboxEditorVisualizationAdapterBindingCommand
    {
        std::uint32_t StableEntityId{0u};
        bool EnableBinding{true};
        std::uint64_t AdapterKey{0u};
        std::uint64_t BufferBDA{0u};
        RenderExtractionCache::VisualizationAdapterBindingKind Kind{
            RenderExtractionCache::VisualizationAdapterBindingKind::Scalar};
        VisualizationAdapterOptions Options{};
    };

    struct SandboxEditorProgressiveSlotDefaultCommand
    {
        std::uint32_t StableEntityId{0u};
        std::string PresentationKey{};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveDefaultValue Value{};
        bool Enabled{true};
    };

    struct SandboxEditorProgressiveSlotPropertyCommand
    {
        std::uint32_t StableEntityId{0u};
        std::string PresentationKey{};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveSlotSourceKind SourceKind{ProgressiveSlotSourceKind::PropertyBake};
        ProgressiveGeometryDomain Domain{ProgressiveGeometryDomain::Unknown};
        ProgressivePropertyValueKind ExpectedValueKind{
            ProgressivePropertyValueKind::Any};
        std::string PropertyName{};
    };

    struct SandboxEditorTextureBakeCommand
    {
        std::uint32_t StableEntityId{0u};
        std::string PresentationKey{"mesh.surface"};
        ProgressiveSlotSemantic TargetSemantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveGeometryDomain SourceDomain{ProgressiveGeometryDomain::MeshVertex};
        ProgressivePropertyValueKind ExpectedValueKind{
            ProgressivePropertyValueKind::Any};
        std::string PropertyName{};
        MeshAttributeTextureBakeEncoder Encoder{
            MeshAttributeTextureBakeEncoder::Auto};
        MeshAttributeTextureBakeRangePolicy RangePolicy{
            MeshAttributeTextureBakeRangePolicy::AutoFinite};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        std::string GeneratedKey{};
        bool BindGeneratedTexture{true};
    };

    struct SandboxEditorTextureBakeCommandResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SelectedMeshTextureBakeStatus BakeStatus{
            SelectedMeshTextureBakeStatus::Success};
        Assets::AssetId GeneratedTexture{};
        DerivedJobHandle Job{};
        bool Scheduled{false};
        bool BoundGeneratedTexture{false};
        std::string GeneratedAssetPath{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied ||
                   Status == SandboxEditorCommandStatus::NoChange;
        }
    };

    struct SandboxEditorUvRegenerationCommand
    {
        std::uint32_t StableEntityId{0u};
        bool PreserveValidAuthoredUvs{false};
        bool ForceRegenerate{true};
        std::uint32_t Resolution{1024u};
        std::uint32_t Padding{2u};
        float TexelsPerUnit{0.0f};
        std::string BackendName{"xatlas"};
    };

    struct SandboxEditorUvRegenerationCommandResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Geometry::UvAtlas::UvAtlasStatus UvStatus{
            Geometry::UvAtlas::UvAtlasStatus::Success};
        Geometry::UvAtlas::UvAtlasProvenance Provenance{
            Geometry::UvAtlas::UvAtlasProvenance::None};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint32_t ChartCount{0u};
        std::size_t SeamSplitVertexCount{0u};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    [[nodiscard]] SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context);

    [[nodiscard]] SandboxEditorDomainWindowModel BuildSandboxEditorDomainWindowModel(
        const SandboxEditorContext& context,
        SandboxEditorDomainWindowKind kind);

    bool SelectSandboxEditorEntity(const SandboxEditorContext& context,
                                   std::uint32_t stableEntityId);

    SandboxEditorFileImportResult ApplySandboxEditorFileImportCommand(
        const SandboxEditorContext& context,
        const SandboxEditorFileImportCommand& command);

    SandboxEditorSceneFileResult ApplySandboxEditorSceneSaveCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command);

    SandboxEditorSceneFileResult ApplySandboxEditorSceneLoadCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command);

    SandboxEditorSceneFileResult ApplySandboxEditorNewSceneCommand(
        const SandboxEditorContext& context);

    SandboxEditorSceneFileResult ApplySandboxEditorCloseSceneCommand(
        const SandboxEditorContext& context);

    SandboxEditorCommandStatus ApplySandboxEditorTransformEdit(
        const SandboxEditorContext& context,
        const SandboxEditorTransformEditCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorCameraControllerCommand(
        const SandboxEditorContext& context,
        const SandboxEditorCameraControllerCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorPrimitiveViewCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPrimitiveViewCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorRenderHintCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRenderHintCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorSpatialDebugBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSpatialDebugBindingCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationConfigCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationPropertyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationPropertyCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationAdapterBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationAdapterBindingCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorProgressiveSlotDefaultCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressiveSlotDefaultCommand& command);

    SandboxEditorCommandStatus ApplySandboxEditorProgressiveSlotPropertyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressiveSlotPropertyCommand& command);

    SandboxEditorTextureBakeCommandResult ApplySandboxEditorTextureBakeCommand(
        const SandboxEditorContext& context,
        const SandboxEditorTextureBakeCommand& command);

    SandboxEditorUvRegenerationCommandResult ApplySandboxEditorUvRegenerationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorUvRegenerationCommand& command);

    SandboxEditorKMeansResult ApplySandboxEditorKMeansCommand(
        const SandboxEditorContext& context,
        const SandboxEditorKMeansCommand& command);

    void DrawSandboxEditorPanelFrame(const SandboxEditorPanelFrame& frame);

    class SandboxEditorUi
    {
    public:
        SandboxEditorUi() = default;
        ~SandboxEditorUi();

        SandboxEditorUi(const SandboxEditorUi&)            = delete;
        SandboxEditorUi& operator=(const SandboxEditorUi&) = delete;
        SandboxEditorUi(SandboxEditorUi&&)                 = delete;
        SandboxEditorUi& operator=(SandboxEditorUi&&)      = delete;

        void Attach(Engine& engine);
        void Detach();

        [[nodiscard]] bool IsAttached() const noexcept { return m_Engine != nullptr; }
        [[nodiscard]] const SandboxEditorPanelFrame& GetLastFrame() const noexcept
        {
            return m_LastFrame;
        }

    private:
        Engine*                 m_Engine{nullptr};
        SandboxEditorPanelFrame m_LastFrame{};
        std::array<char, 1024>  m_ImportPathBuffer{};
        std::array<char, 1024>  m_ScenePathBuffer{};
        std::array<bool, Detail::kSandboxEditorPanelWindowCount>
            m_PanelWindowOpen{};
        std::array<bool, 15>    m_DomainWindowOpen{};
        Assets::AssetPayloadKind m_ImportPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::uint64_t m_LastObservedRuntimeImportSequence{0};
        std::optional<SandboxEditorFileImportResult> m_LastImportResult{};
        std::optional<SandboxEditorSceneFileResult> m_LastSceneFileResult{};
        std::optional<SandboxEditorKMeansResult> m_LastKMeansResult{};
        SandboxEditorGeometryProcessingDomain m_KMeansDomain{
            SandboxEditorGeometryProcessingDomain::None};
        std::int32_t m_KMeansClusterCount{8};
        std::int32_t m_KMeansMaxIterations{32};
        std::int32_t m_KMeansSeed{42};
        bool m_KMeansUseHierarchicalInitialization{true};
        std::int32_t m_TextureBakeSourceIndex{0};
        std::int32_t m_TextureBakeTargetSemanticIndex{0};
        std::int32_t m_TextureBakeEncoderIndex{0};
        std::int32_t m_TextureBakeWidth{64};
        std::int32_t m_TextureBakeHeight{64};
        std::int32_t m_UvAtlasResolution{1024};
        std::int32_t m_UvAtlasPadding{2};
        float m_UvAtlasTexelsPerUnit{0.0f};
        bool m_UvAtlasForceRegenerate{true};
        bool m_UvAtlasPreserveAuthored{false};
    };
}
