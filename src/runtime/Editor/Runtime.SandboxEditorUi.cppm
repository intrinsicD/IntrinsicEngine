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
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.KMeansGpuJobQueue;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
    import Extrinsic.Runtime.SceneSerialization;
    import Extrinsic.Runtime.SelectedMeshTextureBake;
    import Extrinsic.Runtime.SelectionController;
    import Geometry.Graph.Vertex.Normals;
    import Geometry.HalfedgeMesh.Vertices.Normals;
    import Geometry.PointCloud.Normals;
    import Geometry.PointCloud.Utils;
    import Geometry.Smoothing;
    import Geometry.UvAtlas;

namespace Extrinsic::Runtime::Detail
{
    inline constexpr std::size_t kSandboxEditorPanelWindowCount = 11u;
    inline constexpr std::size_t kSandboxEditorDomainWindowCount = 42u;
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
        RenderRecipeCommandsUnavailable,
        InvalidVisualizationProperty,
        InvalidVertexChannelBinding,
        GeometryProcessingFailed,
        RenderGraphStatsUnavailable,
        EditorCommandHistoryUnavailable,
    };

    enum class SandboxEditorCommandStatus : std::uint8_t
    {
        Applied,
        Pending,
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
        InvalidVertexChannelBinding,
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

    enum class SandboxEditorVisualizationTarget : std::uint8_t
    {
        Entity,
        Surface,
        Edges,
        Points,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationPropertyDomain(
        SandboxEditorVisualizationPropertyDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationPropertyValueKind(
        SandboxEditorVisualizationPropertyValueKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationPropertyPreset(
        SandboxEditorVisualizationPropertyPreset preset) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorVisualizationTarget(
        SandboxEditorVisualizationTarget target) noexcept;

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
        MeshDenoise,
        Curvature,
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
        StatisticalOutlierRemoval,
        RadiusOutlierRemoval,
        ProgressivePoissonSampling,
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

    struct SandboxEditorGeometryProcessingMenuItem
    {
        SandboxEditorGeometryProcessingDomain Domain{
            SandboxEditorGeometryProcessingDomain::None};
        const char* Label{""};
        bool HasNormalsMethod{false};
        bool HasDenoiseMethod{false};
        bool HasCurvatureMethod{false};
        bool HasRemeshMethod{false};
        bool HasSubdivideMethod{false};
        bool HasSimplifyMethod{false};
    };

    [[nodiscard]] std::vector<SandboxEditorGeometryProcessingMenuItem>
    GetSandboxEditorGeometryProcessingMenuItems(
        SandboxEditorDomainWindowKind kind);

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

    enum class SandboxEditorKMeansBackend : std::uint8_t
    {
        CpuReference,
        VulkanCompute,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorKMeansBackend(
        SandboxEditorKMeansBackend backend) noexcept;

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
        SandboxEditorKMeansBackend Backend{
            SandboxEditorKMeansBackend::CpuReference};
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
        SandboxEditorKMeansBackend RequestedBackend{
            SandboxEditorKMeansBackend::CpuReference};
        SandboxEditorKMeansBackend ActualBackend{
            SandboxEditorKMeansBackend::CpuReference};
        std::string RequestedBackendId{};
        std::string RequestedBackendDisplayName{};
        std::string BackendId{};
        std::string BackendDisplayName{};
        bool FellBackToCpu{false};
        std::string BackendFallbackReason{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    enum class SandboxEditorProgressivePoissonChannel : std::uint8_t
    {
        Level,
        Phase,
        SplatRadius,
        PrefixVisible,
    };

    enum class SandboxEditorProgressivePoissonBackend : std::uint8_t
    {
        CpuReference,
        VulkanCompute,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorProgressivePoissonChannel(
        SandboxEditorProgressivePoissonChannel channel) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorProgressivePoissonBackend(
        SandboxEditorProgressivePoissonBackend backend) noexcept;

    [[nodiscard]] SandboxEditorProgressivePoissonChannel
    MakeSandboxEditorProgressivePoissonChannel(
        Core::Config::ProgressivePoissonPlaygroundChannel channel) noexcept;

    [[nodiscard]] Core::Config::ProgressivePoissonPlaygroundChannel
    MakeCoreProgressivePoissonPlaygroundChannel(
        SandboxEditorProgressivePoissonChannel channel) noexcept;

    [[nodiscard]] SandboxEditorProgressivePoissonBackend
    MakeSandboxEditorProgressivePoissonBackend(
        Core::Config::ProgressivePoissonPlaygroundBackend backend) noexcept;

    [[nodiscard]] Core::Config::ProgressivePoissonPlaygroundBackend
    MakeCoreProgressivePoissonPlaygroundBackend(
        SandboxEditorProgressivePoissonBackend backend) noexcept;

    struct SandboxEditorProgressivePoissonConfig
    {
        std::uint32_t Dimension{3u};
        std::uint32_t GridWidth{4u};
        std::uint32_t MaxLevels{16u};
        float HashLoadFactor{0.25f};
        float RadiusAlpha{-1.0f};
        bool RandomizeGridOrigin{true};
        std::uint32_t GridOriginSeed{1337u};
        bool ShuffleWithinLevels{true};
        std::uint32_t ShuffleSeed{0x51ed270bu};
        std::uint32_t PrefixCount{0u}; // 0 means all accepted points.
        SandboxEditorProgressivePoissonChannel Channel{
            SandboxEditorProgressivePoissonChannel::Level};
        SandboxEditorProgressivePoissonBackend Backend{
            SandboxEditorProgressivePoissonBackend::CpuReference};
        std::uint32_t MeshSurfaceSampleCount{4096u};
        std::uint32_t MeshSurfaceSampleSeed{1337u};
        double MeshSurfaceMinTriangleArea{1.0e-14};
        bool MeshSurfaceInterpolateNormals{true};
    };

    [[nodiscard]] SandboxEditorProgressivePoissonConfig
    MakeSandboxEditorProgressivePoissonConfig(
        const Core::Config::ProgressivePoissonPlaygroundConfig& config) noexcept;

    [[nodiscard]] Core::Config::ProgressivePoissonPlaygroundConfig
    MakeCoreProgressivePoissonPlaygroundConfig(
        const SandboxEditorProgressivePoissonConfig& config,
        const Core::Config::ProgressivePoissonPlaygroundConfig& defaults = {}) noexcept;

    struct SandboxEditorProgressivePoissonCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorProgressivePoissonConfig Config{};
    };

    struct SandboxEditorProgressivePoissonResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorProgressivePoissonChannel Channel{
            SandboxEditorProgressivePoissonChannel::Level};
        std::uint32_t InputCount{0u};
        std::uint32_t AcceptedCount{0u};
        std::uint32_t PrefixCount{0u};
        std::uint32_t LevelCount{0u};
        SandboxEditorProgressivePoissonBackend RequestedBackend{
            SandboxEditorProgressivePoissonBackend::CpuReference};
        SandboxEditorProgressivePoissonBackend ActualBackend{
            SandboxEditorProgressivePoissonBackend::CpuReference};
        std::string RequestedBackendId{};
        std::string RequestedBackendDisplayName{};
        std::string BackendId{};
        std::string BackendDisplayName{};
        bool FellBackToCpu{false};
        std::string BackendFallbackReason{};
        std::vector<std::uint32_t> LevelAcceptedCounts{};
        float BaseRadius{0.0f};
        float UsedAlpha{0.0f};
        bool AlphaDefaulted{false};
        bool ClampedGridWidth{false};
        bool ClampedMaxLevels{false};
        bool MeshSurfaceSamplingUsed{false};
        std::uint32_t MeshSurfaceSampleCount{0u};
        std::uint32_t MeshSurfaceTotalFaceCount{0u};
        std::uint32_t MeshSurfaceAcceptedTriangleCount{0u};
        std::uint32_t MeshSurfaceRejectedFaceCount{0u};
        double MeshSurfaceArea{0.0};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    enum class SandboxEditorProgressivePoissonConfigStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        MissingConfigFacade,
        PreviewRejected,
        ApplyRejected,
    };

    struct SandboxEditorProgressivePoissonConfigCommand
    {
        Core::Config::ProgressivePoissonPlaygroundConfig Config{};
        std::string SourceId{"sandbox.progressive_poisson"};
    };

    struct SandboxEditorProgressivePoissonConfigResult
    {
        SandboxEditorProgressivePoissonConfigStatus Status{
            SandboxEditorProgressivePoissonConfigStatus::None};
        Core::Config::EngineConfigLoadResult Preview{};
        RuntimeEngineConfigApplyResult Apply{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorProgressivePoissonConfigStatus::Applied ||
                   Status == SandboxEditorProgressivePoissonConfigStatus::NoChange;
        }
    };

    enum class SandboxEditorMeshDenoiseStage : std::uint8_t
    {
        FullBilateral,
    };

    enum class SandboxEditorMeshCurvatureOutput : std::uint8_t
    {
        All,
        Mean,
        Gaussian,
        PrincipalDirections,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorMeshCurvatureOutput(
        SandboxEditorMeshCurvatureOutput output) noexcept;

    enum class SandboxEditorMeshRemeshMode : std::uint8_t
    {
        Uniform,
        Adaptive,
    };

    enum class SandboxEditorMeshRemeshSizingLaw : std::uint8_t
    {
        MeanCurvature,
        ErrorBoundedTaubin,
    };

    enum class SandboxEditorMeshSubdivideOperator : std::uint8_t
    {
        Loop,
        CatmullClark,
        Sqrt3,
    };

    // UI-028: error-metric selection for the Mesh > Processing > Simplify window.
    // Mirrors Geometry::Simplification::Metric (FA_QEM is the GEOM-014 default).
    enum class SandboxEditorMeshSimplifyMetric : std::uint8_t
    {
        ClassicalQEM,
        FA_QEM,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorMeshRemeshMode(
        SandboxEditorMeshRemeshMode mode) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorMeshRemeshSizingLaw(
        SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorMeshSubdivideOperator(
        SandboxEditorMeshSubdivideOperator op) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorMeshSimplifyMetric(
        SandboxEditorMeshSimplifyMetric metric) noexcept;

    struct SandboxEditorMeshDenoiseCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorMeshDenoiseStage Stage{
            SandboxEditorMeshDenoiseStage::FullBilateral};
        std::uint32_t NormalIterations{5u};
        std::uint32_t VertexIterations{10u};
        double SigmaSpatial{0.0};
        double SigmaRange{0.0};
        bool PreserveBoundary{true};
        double DegenerateNormalLengthEpsilon{1.0e-12};
    };

    struct SandboxEditorMeshDenoiseResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Geometry::Smoothing::DenoiseStatus DenoiseStatus{
            Geometry::Smoothing::DenoiseStatus::Success};
        SandboxEditorMeshDenoiseStage Stage{
            SandboxEditorMeshDenoiseStage::FullBilateral};
        std::uint32_t NormalIterations{0u};
        std::uint32_t VertexIterations{0u};
        double SigmaSpatial{0.0};
        double SigmaRange{0.0};
        bool PreserveBoundary{true};
        std::size_t VertexSlotCount{0u};
        std::size_t WrittenCount{0u};
        std::size_t SkippedDeletedVertexCount{0u};
        std::size_t MovedVertexCount{0u};
        std::size_t ProcessedFaceCount{0u};
        std::size_t DegenerateFaceCount{0u};
        std::size_t NonFiniteFaceCount{0u};
        std::size_t SkippedDeletedFaceCount{0u};
        std::size_t PinnedBoundaryVertexCount{0u};
        double SigmaSpatialUsed{0.0};
        double SigmaRangeUsed{0.0};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorMeshCurvatureCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorMeshCurvatureOutput Output{
            SandboxEditorMeshCurvatureOutput::All};
        bool PublishPrincipalDirections{true};
    };

    struct SandboxEditorMeshCurvatureResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorMeshCurvatureOutput Output{
            SandboxEditorMeshCurvatureOutput::All};
        bool DirectionsRequested{true};
        bool DirectionsAvailable{true};
        bool DirectionsPublished{false};
        std::size_t VertexSlotCount{0u};
        std::size_t ScalarPropertyCount{0u};
        std::size_t ScalarWrittenCount{0u};
        std::size_t DirectionPropertyCount{0u};
        std::size_t DirectionWrittenCount{0u};
        std::size_t NonFiniteScalarCount{0u};
        std::size_t NonFiniteDirectionCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorMeshRemeshCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorMeshRemeshMode Mode{
            SandboxEditorMeshRemeshMode::Uniform};
        SandboxEditorMeshRemeshSizingLaw SizingLaw{
            SandboxEditorMeshRemeshSizingLaw::MeanCurvature};
        std::uint32_t Iterations{1u};
        double TargetEdgeLength{0.0};
        double Lambda{0.5};
        double CurvatureAdaptation{1.0};
        double ApproximationError{0.01};
        bool PreserveBoundary{true};
        bool ProjectToSurface{false};
        std::uint32_t ReferenceProjectionK{16u};
        double MaxReferenceProjectionDistance{0.0};
    };

    struct SandboxEditorMeshRemeshResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorMeshRemeshMode Mode{
            SandboxEditorMeshRemeshMode::Uniform};
        SandboxEditorMeshRemeshSizingLaw SizingLaw{
            SandboxEditorMeshRemeshSizingLaw::MeanCurvature};
        std::uint32_t IterationsRequested{0u};
        std::uint32_t IterationsPerformed{0u};
        double TargetEdgeLength{0.0};
        bool ProjectToSurface{false};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        std::size_t SplitCount{0u};
        std::size_t CollapseCount{0u};
        std::size_t FlipCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorMeshSubdivideCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorMeshSubdivideOperator Operator{
            SandboxEditorMeshSubdivideOperator::Loop};
        std::uint32_t Iterations{1u};
        bool PreserveLoopFeatureEdges{false};
        std::uint32_t MaxOutputFaces{0u};
        std::string FeatureEdgePropertyName{"e:feature"};
    };

    struct SandboxEditorMeshSubdivideResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorMeshSubdivideOperator Operator{
            SandboxEditorMeshSubdivideOperator::Loop};
        std::uint32_t IterationsRequested{0u};
        std::uint32_t IterationsPerformed{0u};
        bool PreserveLoopFeatureEdges{false};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorMeshSimplifyCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorMeshSimplifyMetric Metric{
            SandboxEditorMeshSimplifyMetric::FA_QEM};
        // Stop the decimation when FaceCount() <= TargetFaces (0 = disabled).
        std::size_t TargetFaces{0u};
        // Maximum allowed error per collapse; 0 = unlimited (rely on TargetFaces).
        double MaxError{0.0};
        bool PreserveBoundary{true};
        // FA_QEM feature-aware weights (ignored under ClassicalQEM).
        double FeatureAngleThresholdDegrees{45.0};
        double NormalWeight{1.0};
        double BoundaryWeight{1.0};
        double CurvatureWeight{1.0};
        bool PreserveSharpFeatures{true};
        bool PreserveUvSeams{true};
    };

    struct SandboxEditorMeshSimplifyResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorMeshSimplifyMetric Metric{
            SandboxEditorMeshSimplifyMetric::FA_QEM};
        std::size_t TargetFaces{0u};
        double MaxError{0.0};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        std::size_t CollapseCount{0u};
        double MaxCollapseError{0.0};
        std::size_t CollapsesRejectedTopology{0u};
        std::size_t CollapsesRejectedQuality{0u};
        std::size_t SharpFeatureVerticesPinned{0u};
        std::size_t SeamVerticesPinned{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    // UI-029: ICP registration between two selected point-cloud entities.
    // Editor-local variant enum (mapped to Geometry::Registration::ICPVariant in
    // the command implementation) so the editor interface stays decoupled from
    // the geometry registration module.
    enum class SandboxEditorICPVariant : std::uint8_t
    {
        PointToPoint,
        PointToPlane,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorICPVariant(
        SandboxEditorICPVariant variant) noexcept;

    struct SandboxEditorRegistrationCommand
    {
        std::uint32_t SourceStableEntityId{0u};
        std::uint32_t TargetStableEntityId{0u};
        SandboxEditorICPVariant Variant{SandboxEditorICPVariant::PointToPoint};
        std::uint32_t MaxIterations{50u};
        // World-space correspondence cutoff; <= 0 disables the cutoff.
        double MaxCorrespondenceDistance{0.0};
        double InlierRatio{0.9};
        // Trajectory step whose cumulative source->target pose is written to the
        // source entity Transform. 0 = identity (un-registered start); values at
        // or beyond the completed iteration count clamp to the converged pose.
        std::size_t TrajectoryStep{0u};
    };

    struct SandboxEditorRegistrationResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        bool HasResult{false};
        SandboxEditorICPVariant Variant{SandboxEditorICPVariant::PointToPoint};
        std::size_t SourcePointCount{0u};
        std::size_t TargetPointCount{0u};
        std::size_t IterationsPerformed{0u};
        std::size_t TrajectoryLength{0u};
        std::size_t AppliedStep{0u};
        double FinalRMSE{0.0};
        bool Converged{false};
        std::size_t FinalInlierCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorMeshVertexNormalsCommand
    {
        std::uint32_t StableEntityId{0u};
        Geometry::HalfedgeMesh::VertexNormals::AveragingMode Weighting{
            Geometry::HalfedgeMesh::VertexNormals::AveragingMode::AreaWeighted};
        glm::vec3 FallbackNormal{0.0f, 1.0f, 0.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
    };

    struct SandboxEditorMeshVertexNormalsResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Geometry::HalfedgeMesh::VertexNormals::RecomputeStatus NormalStatus{
            Geometry::HalfedgeMesh::VertexNormals::RecomputeStatus::Success};
        Geometry::HalfedgeMesh::VertexNormals::AveragingMode Weighting{
            Geometry::HalfedgeMesh::VertexNormals::AveragingMode::AreaWeighted};
        std::size_t VertexSlotCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalVertexCount{0};
        std::size_t ProcessedFaceCount{0};
        std::size_t DegenerateFaceCount{0};
        std::size_t NonFiniteFaceCount{0};
        std::size_t InvalidTopologyFaceCount{0};
        std::size_t DegenerateCornerCount{0};
        std::size_t FallbackVertexCount{0};
        std::size_t SkippedDeletedFaceCount{0};
        std::size_t SkippedDeletedVertexCount{0};
        bool FallbackNormalWasRepaired{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorGraphVertexNormalsCommand
    {
        std::uint32_t StableEntityId{0u};
        glm::vec3 FallbackNormal{0.0f, 0.0f, 1.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
        double CollinearEigenvalueRatioEpsilon{1.0e-5};
        bool OrientTowardFallback{true};
    };

    struct SandboxEditorGraphVertexNormalsResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Geometry::Graph::VertexNormals::RecomputeStatus NormalStatus{
            Geometry::Graph::VertexNormals::RecomputeStatus::Success};
        bool OrientTowardFallback{true};
        std::size_t VertexSlotCount{0};
        std::size_t EdgeSlotCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalVertexCount{0};
        std::size_t FallbackVertexCount{0};
        std::size_t IsolatedVertexCount{0};
        std::size_t DegreeOneVertexCount{0};
        std::size_t CollinearNeighborhoodCount{0};
        std::size_t DuplicatePositionCount{0};
        std::size_t NonFinitePositionCount{0};
        std::size_t InvalidEdgeCount{0};
        std::size_t SkippedDeletedVertexCount{0};
        std::size_t SkippedDeletedEdgeCount{0};
        bool FallbackNormalWasRepaired{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    struct SandboxEditorPointCloudVertexNormalsCommand
    {
        std::uint32_t StableEntityId{0u};
        std::uint32_t KNeighbors{15u};
        std::uint32_t MinimumNeighbors{2u};
        bool UseRadiusSearch{false};
        float Radius{0.0f};
        Geometry::PointCloud::Normals::OrientationMode Orientation{
            Geometry::PointCloud::Normals::OrientationMode::MinimumSpanningTree};
        glm::vec3 FallbackNormal{0.0f, 0.0f, 1.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
        double CollinearEigenvalueRatioEpsilon{1.0e-5};
    };

    struct SandboxEditorPointCloudVertexNormalsResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        Geometry::PointCloud::Normals::RecomputeStatus NormalStatus{
            Geometry::PointCloud::Normals::RecomputeStatus::Success};
        Geometry::PointCloud::Normals::NeighborhoodBackend Backend{
            Geometry::PointCloud::Normals::NeighborhoodBackend::KDTree};
        Geometry::PointCloud::Normals::OrientationMode Orientation{
            Geometry::PointCloud::Normals::OrientationMode::MinimumSpanningTree};
        std::uint32_t KNeighbors{15u};
        std::uint32_t MinimumNeighbors{2u};
        bool UseRadiusSearch{false};
        float Radius{0.0f};
        std::size_t PointSlotCount{0};
        std::size_t FinitePointCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalPointCount{0};
        std::size_t FallbackPointCount{0};
        std::size_t DegenerateNeighborhoodCount{0};
        std::size_t TooFewNeighborCount{0};
        std::size_t CollinearNeighborhoodCount{0};
        std::size_t DuplicatePositionCount{0};
        std::size_t NonFinitePointCount{0};
        std::size_t SkippedDeletedPointCount{0};
        std::size_t SpatialQueryFailureCount{0};
        std::size_t FlippedOrientationCount{0};
        std::size_t KNNVisitedNodeCount{0};
        std::size_t KNNDistanceEvaluationCount{0};
        bool FallbackNormalWasRepaired{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorCommandStatus::Applied;
        }
    };

    // UI-027: explicit point-cloud outlier removal. Mirrors the GEOM-016
    // operators; the editor command rebuilds the entity's point GeometrySources
    // from the kept points and records an undoable history entry.
    enum class SandboxEditorPointCloudOutlierMethod : std::uint8_t
    {
        Statistical,  // GEOM-016 RemoveStatisticalOutliers (mean-kNN distance).
        Radius,       // GEOM-016 RemoveRadiusOutliers (neighbor count in radius).
    };

    struct SandboxEditorPointCloudOutlierRemovalCommand
    {
        std::uint32_t StableEntityId{0u};
        SandboxEditorPointCloudOutlierMethod Method{
            SandboxEditorPointCloudOutlierMethod::Statistical};
        std::uint32_t KNeighbors{16u};
        float         StdDevMultiplier{1.0f};
        float         SearchRadius{0.0f};
        std::uint32_t MinNeighbors{4u};
    };

    struct SandboxEditorPointCloudOutlierRemovalResult
    {
        SandboxEditorCommandStatus Status{SandboxEditorCommandStatus::NoChange};
        SandboxEditorPointCloudOutlierMethod Method{
            SandboxEditorPointCloudOutlierMethod::Statistical};
        Geometry::PointCloud::OutlierRemovalStatus GeometryStatus{
            Geometry::PointCloud::OutlierRemovalStatus::Success};
        std::size_t OriginalCount{0};
        std::size_t KeptCount{0};
        std::size_t RejectedCount{0};
        std::size_t NonFiniteCount{0};
        float       MeanDistance{0.0f};
        float       StdDevDistance{0.0f};
        float       DistanceThreshold{0.0f};
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

    struct SandboxEditorVertexChannelBindingOptionModel
    {
        std::string PropertyName{};
        SandboxEditorPropertyCatalogDomain Domain{
            SandboxEditorPropertyCatalogDomain::MeshVertices};
        SandboxEditorPropertyCatalogValueKind ValueKind{
            SandboxEditorPropertyCatalogValueKind::Unknown};
        AttributeSourceType SourceType{AttributeSourceType::Vec3};
        std::size_t ElementCount{0u};
        AttributeBindResult Resolver{};
        bool Compatible{false};
        std::string DisabledReason{};
    };

    struct SandboxEditorVertexChannelBindingTargetModel
    {
        VertexChannel Channel{VertexChannel::Normal};
        bool HasBinding{false};
        VertexChannelSourceBinding Binding{};
        AttributeBindResult Resolver{};
        std::string Diagnostic{};
        std::vector<SandboxEditorVertexChannelBindingOptionModel> Options{};
    };

    struct SandboxEditorPropertyCatalogModel
    {
        bool HasSelectedEntity{false};
        std::uint32_t SelectedStableId{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        std::vector<SandboxEditorPropertyCatalogRow> Rows{};
        std::vector<SandboxEditorPropertyBindingTargetModel> BindingTargets{};
        std::vector<SandboxEditorVertexChannelBindingTargetModel> VertexChannelTargets{};
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

    enum class SandboxEditorRenderRecipeDraftState : std::uint8_t
    {
        InactiveDraft = 0,
        Debounced,
        Validated,
        Rejected,
        Previewed,
        Activated,
        Canceled,
    };

    enum class SandboxEditorRenderRecipeCommandKind : std::uint8_t
    {
        UpdateDraft = 0,
        ValidateDraft,
        PreviewDraft,
        ActivatePreview,
        CancelDraft,
        PublishArtifact,
        ApplyArtifact,
    };

    enum class SandboxEditorRenderRecipeCommandStatus : std::uint8_t
    {
        NoChange = 0,
        DraftUpdated,
        Debounced,
        Validated,
        ValidationFailed,
        Previewed,
        PreviewFailed,
        Activated,
        Canceled,
        Published,
        Applied,
        ActivationFailed,
        MissingRecipeContext,
        MissingEditorState,
        MissingArtifactRegistry,
        ArtifactCommandFailed,
    };

    [[nodiscard]] const char* DebugNameForSandboxEditorRenderRecipeDraftState(
        SandboxEditorRenderRecipeDraftState state) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorRenderRecipeCommandKind(
        SandboxEditorRenderRecipeCommandKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForSandboxEditorRenderRecipeCommandStatus(
        SandboxEditorRenderRecipeCommandStatus status) noexcept;

    struct SandboxEditorRenderRecipeSlotModel
    {
        std::string StableName{};
        Graphics::RecipeSlotKind Kind{Graphics::RecipeSlotKind::Extension};
        std::string SchemaId{};
        std::string Defaults{};
        std::vector<std::string> RequiredCapabilities{};
        std::vector<std::string> AllowedBindingRoles{};
        std::vector<std::string> UsedBindingRoles{};
        bool DeclaredByRenderer{false};
        bool Editable{false};
        std::string DisabledReason{};
    };

    struct SandboxEditorRenderRecipeBindingOverrideModel
    {
        std::string SemanticName{};
        std::string Slot{};
        std::string SourceDomain{};
        std::string SourceIdentity{};
        std::string SourceRevision{};
        std::string ValueType{};
        std::string ValueFormat{};
        bool Required{false};
        bool Editable{false};
        std::string DisabledReason{};
    };

    struct SandboxEditorRenderRecipeOutputModel
    {
        std::string Name{};
        std::string Kind{};
        std::string Format{};
        bool Required{false};
    };

    struct SandboxEditorRenderArtifactRow
    {
        std::string ArtifactId{};
        std::string Purpose{};
        RenderArtifactPublicationKind Kind{
            RenderArtifactPublicationKind::TransientFrame};
        RenderArtifactUiStatus Status{RenderArtifactPublicationState::Unknown};
        std::string PayloadUri{};
        std::string ProducerLabel{};
        bool CanPublish{false};
        bool CanApply{false};
        std::string DisabledReason{};
    };

    struct SandboxEditorRenderRecipeEditorModel
    {
        bool Available{false};
        std::string RendererId{};
        std::string ActiveRecipeId{};
        std::string ActiveViewOutputRecipeId{};
        std::string DraftRecipeId{};
        std::string DraftSourceId{};
        SandboxEditorRenderRecipeDraftState DraftState{
            SandboxEditorRenderRecipeDraftState::InactiveDraft};
        Graphics::RenderRecipeConfigState ValidationState{
            Graphics::RenderRecipeConfigState::Invalid};
        std::uint64_t DraftRevision{0u};
        std::uint64_t ActiveRevision{0u};
        std::uint32_t ParsedSlotCount{0u};
        std::uint32_t ParsedBindingOverrideCount{0u};
        std::string ViewKind{};
        std::string OutputTarget{};
        std::string InteractionMode{};
        std::uint32_t ViewportWidth{0u};
        std::uint32_t ViewportHeight{0u};
        float RenderScale{1.0f};
        bool CaptureRequested{false};
        bool ReadbackRequested{false};
        bool CanValidate{false};
        bool CanPreview{false};
        bool CanActivate{false};
        bool CanCancel{false};
        std::vector<SandboxEditorRenderRecipeSlotModel> Slots{};
        std::vector<SandboxEditorRenderRecipeBindingOverrideModel> BindingOverrides{};
        std::vector<SandboxEditorRenderRecipeOutputModel> Outputs{};
        std::vector<SandboxEditorRenderArtifactRow> Artifacts{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
        std::vector<Graphics::RenderRecipeConfigDiagnostic> RecipeDiagnostics{};
    };

    struct SandboxEditorRenderRecipeEditorState
    {
        std::string DraftDocument{};
        std::string DraftSourceId{"sandbox-render-recipe-draft.json"};
        Graphics::RenderRecipeConfigLoadResult LastPreview{};
        bool HasLastPreview{false};
        Graphics::RenderRecipeDescriptor ActiveRecipe{};
        Graphics::ViewOutputRecipeDescriptor ActiveViewOutput{};
        Graphics::BindingSet ActiveBindings{};
        bool HasActiveOverride{false};
        SandboxEditorRenderRecipeDraftState DraftState{
            SandboxEditorRenderRecipeDraftState::InactiveDraft};
        std::uint64_t DraftRevision{0u};
        std::uint64_t ActiveRevision{0u};
    };

    struct SandboxEditorRenderRecipeCommand
    {
        SandboxEditorRenderRecipeCommandKind Kind{
            SandboxEditorRenderRecipeCommandKind::UpdateDraft};
        std::string Document{};
        std::string SourceId{"sandbox-render-recipe-draft.json"};
        bool Debounced{false};
        std::string ArtifactId{};
        std::string Provenance{"sandbox-editor"};
        std::string TargetUri{};
        std::string ProjectTarget{};
        std::string Label{};
        std::string UndoLabel{};
    };

    struct SandboxEditorRenderRecipeCommandResult
    {
        SandboxEditorRenderRecipeCommandStatus Status{
            SandboxEditorRenderRecipeCommandStatus::NoChange};
        SandboxEditorRenderRecipeDraftState DraftState{
            SandboxEditorRenderRecipeDraftState::InactiveDraft};
        Graphics::RenderRecipeConfigState ValidationState{
            Graphics::RenderRecipeConfigState::Invalid};
        RenderArtifactOperationStatus ArtifactStatus{
            RenderArtifactOperationStatus::InvalidRequest};
        RenderArtifactPublicationState ArtifactState{
            RenderArtifactPublicationState::Unknown};
        std::string ArtifactId{};
        std::uint64_t Revision{0u};
        bool ProjectMutationAuthorized{false};
        std::vector<Graphics::RenderRecipeConfigDiagnostic> RecipeDiagnostics{};
        std::vector<RenderArtifactDiagnostic> ArtifactDiagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SandboxEditorRenderRecipeCommandStatus::NoChange ||
                   Status == SandboxEditorRenderRecipeCommandStatus::DraftUpdated ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Debounced ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Validated ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Previewed ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Activated ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Canceled ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Published ||
                   Status == SandboxEditorRenderRecipeCommandStatus::Applied;
        }
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

    struct SandboxEditorKMeansGpuCommandSurface
    {
        std::function<RuntimeKMeansGpuJobSubmission(RuntimeKMeansGpuJobRequest)>
            Submit{};
        std::function<std::optional<RuntimeKMeansGpuJobResult>()>
            ConsumeCompleted{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Submit) &&
                   static_cast<bool>(ConsumeCompleted);
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
        bool TargetAvailable{false};
        SandboxEditorVisualizationTarget Target{
            SandboxEditorVisualizationTarget::Entity};
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
        bool MeshDenoiseAvailable{false};
        bool MeshCurvatureAvailable{false};
        bool MeshCurvatureDirectionsAvailable{false};
        bool MeshRemeshAvailable{false};
        bool MeshRemeshUniformAvailable{false};
        bool MeshRemeshAdaptiveAvailable{false};
        bool MeshRemeshProjectToSurfaceAvailable{false};
        bool MeshRemeshErrorBoundedSizingAvailable{false};
        bool MeshSubdivideAvailable{false};
        bool MeshSubdivideLoopAvailable{false};
        bool MeshSubdivideCatmullClarkAvailable{false};
        bool MeshSubdivideSqrt3Available{false};
        bool MeshSubdivideLoopFeatureEdgesAvailable{false};
        bool MeshSimplifyAvailable{false};
        bool MeshVertexNormalsAvailable{false};
        bool GraphVertexNormalsAvailable{false};
        bool PointCloudVertexNormalsAvailable{false};
        bool PointCloudOutlierRemovalAvailable{false};
        bool PointCloudProgressivePoissonAvailable{false};
        bool MeshProgressivePoissonAvailable{false};
        std::optional<SandboxEditorKMeansResult> LastKMeansResult{};
        std::optional<SandboxEditorMeshDenoiseResult>
            LastMeshDenoiseResult{};
        std::optional<SandboxEditorMeshCurvatureResult>
            LastMeshCurvatureResult{};
        std::optional<SandboxEditorMeshRemeshResult>
            LastMeshRemeshResult{};
        std::optional<SandboxEditorMeshSubdivideResult>
            LastMeshSubdivideResult{};
        std::optional<SandboxEditorMeshSimplifyResult>
            LastMeshSimplifyResult{};
        std::optional<SandboxEditorMeshVertexNormalsResult>
            LastMeshVertexNormalsResult{};
        std::optional<SandboxEditorGraphVertexNormalsResult>
            LastGraphVertexNormalsResult{};
        std::optional<SandboxEditorPointCloudVertexNormalsResult>
            LastPointCloudVertexNormalsResult{};
        std::optional<SandboxEditorPointCloudOutlierRemovalResult>
            LastPointCloudOutlierRemovalResult{};
        std::optional<SandboxEditorProgressivePoissonResult>
            LastProgressivePoissonResult{};
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
        bool VisualizationTargetAvailable{false};
        SandboxEditorVisualizationTarget VisualizationTarget{
            SandboxEditorVisualizationTarget::Entity};
        SandboxEditorVisualizationModel Visualization{};
        SandboxEditorPropertyCatalogModel PropertyCatalog{};
        SandboxEditorBoundRenderStateModel BoundState{};
        SandboxEditorTextureBakeControlsModel TextureBake{};
        SandboxEditorGeometryProcessingModel Processing{};
        SandboxEditorPrimitiveDetailModel Primitive{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    enum class SandboxEditorSelectedModelCacheSection : std::uint8_t
    {
        SelectedAnalysis,
        Visualization,
    };

    struct SandboxEditorSelectedModelCacheKey
    {
        SandboxEditorSelectedModelCacheSection Section{
            SandboxEditorSelectedModelCacheSection::SelectedAnalysis};
        SandboxEditorVisualizationTarget VisualizationTarget{
            SandboxEditorVisualizationTarget::Entity};
        std::uint32_t PrimaryStableId{0u};
        std::vector<std::uint32_t> SelectedStableIds{};
        std::uint64_t SelectionGeneration{0u};
        std::uint64_t PrimitiveSelectionGeneration{0u};
        ECS::Components::GeometrySources::Domain SelectedDomain{
            ECS::Components::GeometrySources::Domain::None};
        std::size_t VertexCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t HalfedgeCount{0u};
        std::size_t FaceCount{0u};
        std::size_t NodeCount{0u};
        std::uint64_t GeometryMetadataSignature{0u};
        std::uint64_t RenderHintSignature{0u};
        std::uint64_t VisualizationStateSignature{0u};
        std::uint64_t BindingGeneration{0u};
        std::uint64_t ProgressiveBindingGeneration{0u};
        std::uint64_t CommandHistoryRevision{0u};
        std::uint64_t VisualizationAdapterBindingRevision{0u};
        std::uint32_t ViewportWidth{0u};
        std::uint32_t ViewportHeight{0u};
        bool VisualizationCommandsAvailable{false};
        bool VisualizationAdapterBindingsAvailable{false};

        friend bool operator==(
            const SandboxEditorSelectedModelCacheKey&,
            const SandboxEditorSelectedModelCacheKey&) = default;
    };

    struct SandboxEditorSelectedAnalysisModel
    {
        SandboxEditorPropertyCatalogModel PropertyCatalog{};
        SandboxEditorProgressiveRenderDataModel Progressive{};
        SandboxEditorBoundRenderStateModel BoundState{};
        SandboxEditorTextureBakeControlsModel TextureBake{};
    };

    struct SandboxEditorSelectedAnalysisCacheEntry
    {
        bool Valid{false};
        SandboxEditorSelectedModelCacheKey Key{};
        SandboxEditorSelectedAnalysisModel Model{};
    };

    struct SandboxEditorVisualizationModelCacheEntry
    {
        bool Valid{false};
        SandboxEditorSelectedModelCacheKey Key{};
        SandboxEditorVisualizationModel Model{};
    };

    struct SandboxEditorSelectedModelCacheStats
    {
        std::uint32_t SelectedAnalysisCacheHits{0u};
        std::uint32_t SelectedAnalysisCacheMisses{0u};
        std::uint32_t VisualizationModelCacheHits{0u};
        std::uint32_t VisualizationModelCacheMisses{0u};
        std::uint32_t Invalidations{0u};
        std::uint32_t Entries{0u};
    };

    struct SandboxEditorSelectedModelCache
    {
        SandboxEditorSelectedAnalysisCacheEntry SelectedAnalysis{};
        std::array<SandboxEditorVisualizationModelCacheEntry, 4u>
            Visualization{};
        SandboxEditorSelectedModelCacheStats Counters{};

        void Clear() noexcept;
        [[nodiscard]] SandboxEditorSelectedModelCacheStats Stats() const noexcept;
    };

    struct SandboxEditorModelBuildRequest
    {
        bool Hierarchy{true};
        bool Inspector{true};
        bool Selection{true};
        bool Document{true};
        bool SceneFile{true};
        bool FileImport{true};
        bool AssetImportQueue{true};
        bool RenderGraph{true};
        bool RenderRecipe{true};
        bool CameraRender{true};
        bool Visualization{true};
    };

    struct SandboxEditorModelBuildStats
    {
        std::uint32_t HierarchyModelBuilds{0u};
        std::uint32_t InspectorModelBuilds{0u};
        std::uint32_t SelectionModelBuilds{0u};
        std::uint32_t PropertyCatalogModelBuilds{0u};
        std::uint32_t VertexChannelTargetBuilds{0u};
        std::uint32_t VertexChannelResolverScans{0u};
        std::uint32_t VertexChannelScratchAllocations{0u};
        std::uint64_t VertexChannelScratchBytes{0u};
        std::uint32_t ProgressiveModelBuilds{0u};
        std::uint32_t BoundStateModelBuilds{0u};
        std::uint32_t UvDiagnosticsModelBuilds{0u};
        std::uint64_t UvDiagnosticsTexcoordElementsScanned{0u};
        std::uint32_t TextureBakeModelBuilds{0u};
        std::uint64_t TextureBakeSourceRowsEnumerated{0u};
        std::uint32_t VisualizationModelBuilds{0u};
        std::uint32_t DomainWindowModelBuilds{0u};
        std::uint32_t DomainWindowModelCacheHits{0u};
        std::uint32_t SelectedAnalysisCacheHits{0u};
        std::uint32_t SelectedAnalysisCacheMisses{0u};
        std::uint32_t VisualizationModelCacheHits{0u};
        std::uint32_t VisualizationModelCacheMisses{0u};
        std::uint64_t PanelFrameModelBuildTimeNs{0u};
        std::uint64_t InspectorModelBuildTimeNs{0u};
        std::uint64_t SelectedAnalysisModelBuildTimeNs{0u};
        std::uint64_t PropertyCatalogModelBuildTimeNs{0u};
        std::uint64_t VertexChannelValidationTimeNs{0u};
        std::uint64_t UvDiagnosticsModelBuildTimeNs{0u};
        std::uint64_t TextureBakeModelBuildTimeNs{0u};
        std::uint64_t VisualizationModelBuildTimeNs{0u};
        std::uint64_t DomainWindowModelBuildTimeNs{0u};
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
        SandboxEditorRenderRecipeEditorModel RenderRecipe{};
        SandboxEditorCameraRenderModel      CameraRender{};
        SandboxEditorVisualizationModel     Visualization{};
        SandboxEditorModelBuildStats        ModelBuildStats{};
        std::vector<SandboxEditorDiagnostic> Diagnostics{};
    };

    struct SandboxEditorContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        SelectionController*  Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        Assets::AssetService* AssetService{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        std::uint64_t LastRefinedPrimitiveGeneration{0u};
        CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        RHI::IDevice* Device{nullptr};
        SandboxEditorAssetImportCommandSurface AssetImportCommands{};
        SandboxEditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        SandboxEditorSceneFileCommandSurface SceneFileCommands{};
        SandboxEditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        SandboxEditorVisualizationAdapterBindingCommandSurface VisualizationAdapterBindings{};
        std::uint64_t VisualizationAdapterBindingRevision{0u};
        SandboxEditorKMeansGpuCommandSurface KMeansGpuCommands{};
        RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        const DerivedJobQueueSnapshot* DerivedJobs{nullptr};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        const SandboxEditorFileImportResult* LastAssetImportResult{nullptr};
        const SandboxEditorSceneFileResult* LastSceneFileResult{nullptr};
        const SandboxEditorKMeansResult* LastKMeansResult{nullptr};
        const SandboxEditorMeshDenoiseResult*
            LastMeshDenoiseResult{nullptr};
        const SandboxEditorMeshCurvatureResult*
            LastMeshCurvatureResult{nullptr};
        const SandboxEditorMeshRemeshResult*
            LastMeshRemeshResult{nullptr};
        const SandboxEditorMeshSubdivideResult*
            LastMeshSubdivideResult{nullptr};
        const SandboxEditorMeshSimplifyResult*
            LastMeshSimplifyResult{nullptr};
        const SandboxEditorMeshVertexNormalsResult*
            LastMeshVertexNormalsResult{nullptr};
        const SandboxEditorGraphVertexNormalsResult*
            LastGraphVertexNormalsResult{nullptr};
        const SandboxEditorPointCloudVertexNormalsResult*
            LastPointCloudVertexNormalsResult{nullptr};
        const SandboxEditorPointCloudOutlierRemovalResult*
            LastPointCloudOutlierRemovalResult{nullptr};
        const SandboxEditorProgressivePoissonResult*
            LastProgressivePoissonResult{nullptr};
        const SandboxEditorRegistrationResult*
            LastRegistrationResult{nullptr};
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        const Graphics::RenderRecipeConfigContext* RenderRecipeContext{nullptr};
        SandboxEditorRenderRecipeEditorState* RenderRecipeEditorState{nullptr};
        const RuntimeRenderRecipeState* RenderRecipeRuntimeState{nullptr};
        const RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        SandboxEditorModelBuildStats* ModelBuildStats{nullptr};
        SandboxEditorSelectedModelCache* SelectedModelCache{nullptr};
        std::function<Graphics::RenderRecipeConfigLoadResult(
            const std::string&,
            const std::string&)>
            PreviewRenderRecipeDocument{};
        std::function<RuntimeRenderRecipeApplyResult(
            const Graphics::RenderRecipeConfigLoadResult&)>
            ApplyRenderRecipePreview{};
        std::function<Core::Config::EngineConfigLoadResult(
            const std::string&,
            const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<RuntimeEngineConfigApplyResult(
            const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        RenderArtifactRegistry* RenderArtifacts{nullptr};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool SceneFileCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
        bool VisualizationCommandsAvailable{false};
        bool RenderRecipeCommandsAvailable{false};
        bool EngineConfigCommandsAvailable{false};
        bool MeshDenoiseKernelAvailable{true};
        bool MeshCurvatureKernelAvailable{true};
        bool MeshCurvatureDirectionsAvailable{true};
        bool MeshRemeshUniformKernelAvailable{true};
        bool MeshRemeshAdaptiveKernelAvailable{true};
        bool MeshRemeshProjectToSurfaceAvailable{true};
        bool MeshRemeshErrorBoundedSizingAvailable{true};
        bool MeshSubdivideLoopKernelAvailable{true};
        bool MeshSubdivideCatmullClarkKernelAvailable{true};
        bool MeshSubdivideSqrt3KernelAvailable{true};
        bool MeshSubdivideLoopFeatureEdgesAvailable{true};
        bool MeshSimplifyKernelAvailable{true};
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
        SandboxEditorVisualizationTarget Target{
            SandboxEditorVisualizationTarget::Entity};
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
        SandboxEditorVisualizationTarget Target{
            SandboxEditorVisualizationTarget::Entity};
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

    struct SandboxEditorVertexChannelBindingCommand
    {
        std::uint32_t StableEntityId{0u};
        VertexChannel Channel{VertexChannel::Normal};
        bool EnableBinding{true};
        std::string PropertyName{};
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
    [[nodiscard]] SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context,
        const SandboxEditorModelBuildRequest& request);

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

    SandboxEditorCommandStatus ApplySandboxEditorVertexChannelBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVertexChannelBindingCommand& command);

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

    SandboxEditorMeshDenoiseResult ApplySandboxEditorMeshDenoiseCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshDenoiseCommand& command);

    SandboxEditorMeshCurvatureResult ApplySandboxEditorMeshCurvatureCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshCurvatureCommand& command);

    SandboxEditorMeshRemeshResult ApplySandboxEditorMeshRemeshCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshRemeshCommand& command);

    SandboxEditorMeshSubdivideResult ApplySandboxEditorMeshSubdivideCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshSubdivideCommand& command);

    SandboxEditorMeshSimplifyResult ApplySandboxEditorMeshSimplifyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshSimplifyCommand& command);

    SandboxEditorMeshVertexNormalsResult
    ApplySandboxEditorMeshVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshVertexNormalsCommand& command);

    SandboxEditorGraphVertexNormalsResult
    ApplySandboxEditorGraphVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorGraphVertexNormalsCommand& command);

    SandboxEditorPointCloudVertexNormalsResult
    ApplySandboxEditorPointCloudVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPointCloudVertexNormalsCommand& command);

    SandboxEditorPointCloudOutlierRemovalResult
    ApplySandboxEditorPointCloudOutlierRemovalCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPointCloudOutlierRemovalCommand& command);

    SandboxEditorRegistrationResult ApplySandboxEditorRegistrationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRegistrationCommand& command);

    SandboxEditorProgressivePoissonResult
    ApplySandboxEditorProgressivePoissonCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressivePoissonCommand& command);

    [[nodiscard]] SandboxEditorProgressivePoissonConfigResult
    ApplySandboxEditorProgressivePoissonConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressivePoissonConfigCommand& command);

    [[nodiscard]] SandboxEditorRenderRecipeEditorModel
    BuildSandboxEditorRenderRecipeEditorModel(
        const SandboxEditorContext& context);

    SandboxEditorRenderRecipeCommandResult
    ApplySandboxEditorRenderRecipeCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRenderRecipeCommand& command);

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
        SandboxEditorSelectedModelCache m_SelectedModelCache{};
        std::array<char, 1024>  m_ImportPathBuffer{};
        std::array<char, 1024>  m_ScenePathBuffer{};
        std::array<bool, Detail::kSandboxEditorPanelWindowCount>
            m_PanelWindowOpen{};
        std::array<bool, Detail::kSandboxEditorDomainWindowCount>
            m_DomainWindowOpen{};
        Assets::AssetPayloadKind m_ImportPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::uint64_t m_LastObservedRuntimeImportSequence{0};
        std::optional<SandboxEditorFileImportResult> m_LastImportResult{};
        std::optional<SandboxEditorSceneFileResult> m_LastSceneFileResult{};
        std::optional<SandboxEditorKMeansResult> m_LastKMeansResult{};
        std::optional<SandboxEditorMeshDenoiseResult>
            m_LastMeshDenoiseResult{};
        std::optional<SandboxEditorMeshCurvatureResult>
            m_LastMeshCurvatureResult{};
        std::optional<SandboxEditorMeshRemeshResult>
            m_LastMeshRemeshResult{};
        std::optional<SandboxEditorMeshSubdivideResult>
            m_LastMeshSubdivideResult{};
        std::optional<SandboxEditorMeshSimplifyResult>
            m_LastMeshSimplifyResult{};
        std::optional<SandboxEditorMeshVertexNormalsResult>
            m_LastMeshVertexNormalsResult{};
        std::optional<SandboxEditorGraphVertexNormalsResult>
            m_LastGraphVertexNormalsResult{};
        std::optional<SandboxEditorPointCloudVertexNormalsResult>
            m_LastPointCloudVertexNormalsResult{};
        std::optional<SandboxEditorPointCloudOutlierRemovalResult>
            m_LastPointCloudOutlierRemovalResult{};
        std::optional<SandboxEditorProgressivePoissonResult>
            m_LastProgressivePoissonResult{};
        std::optional<SandboxEditorProgressivePoissonConfigResult>
            m_LastProgressivePoissonConfigResult{};
        std::optional<SandboxEditorRegistrationResult>
            m_LastRegistrationResult{};
        Graphics::RenderRecipeConfigContext m_RenderRecipeContext{};
        SandboxEditorRenderRecipeEditorState m_RenderRecipeState{};
        RenderArtifactRegistry m_RenderArtifactRegistry{};
        std::array<char, 8192> m_RenderRecipeDraftBuffer{};
        SandboxEditorGeometryProcessingDomain m_KMeansDomain{
            SandboxEditorGeometryProcessingDomain::None};
        std::int32_t m_KMeansClusterCount{8};
        std::int32_t m_KMeansMaxIterations{32};
        std::int32_t m_KMeansSeed{42};
        bool m_KMeansUseHierarchicalInitialization{true};
        std::int32_t m_KMeansBackend{0};
        std::int32_t m_MeshDenoiseStage{0};
        std::int32_t m_MeshDenoiseNormalIterations{5};
        std::int32_t m_MeshDenoiseVertexIterations{10};
        float m_MeshDenoiseSigmaSpatial{0.0f};
        float m_MeshDenoiseSigmaRange{0.0f};
        bool m_MeshDenoisePreserveBoundary{true};
        std::int32_t m_MeshCurvatureOutput{0};
        bool m_MeshCurvaturePublishDirections{true};
        std::int32_t m_MeshRemeshMode{0};
        std::int32_t m_MeshRemeshSizingLaw{0};
        std::int32_t m_MeshRemeshIterations{1};
        float m_MeshRemeshTargetEdgeLength{0.0f};
        bool m_MeshRemeshProjectToSurface{false};
        std::int32_t m_MeshSubdivideOperator{0};
        std::int32_t m_MeshSubdivideIterations{1};
        bool m_MeshSubdividePreserveLoopFeatures{false};
        std::int32_t m_MeshSimplifyMetric{1};
        std::int32_t m_MeshSimplifyTargetFaces{0};
        float m_MeshSimplifyMaxError{0.0f};
        bool m_MeshSimplifyPreserveBoundary{true};
        float m_MeshSimplifyFeatureAngleThresholdDegrees{45.0f};
        float m_MeshSimplifyNormalWeight{1.0f};
        float m_MeshSimplifyBoundaryWeight{1.0f};
        float m_MeshSimplifyCurvatureWeight{1.0f};
        bool m_MeshSimplifyPreserveSharpFeatures{true};
        bool m_MeshSimplifyPreserveUvSeams{true};
        std::int32_t m_RegistrationVariant{0};
        std::int32_t m_RegistrationMaxIterations{50};
        float m_RegistrationMaxCorrespondenceDistance{0.0f};
        float m_RegistrationInlierRatio{0.9f};
        std::int32_t m_RegistrationTrajectoryStep{0};
        bool m_RegistrationSwapSourceTarget{false};
        std::int32_t m_MeshVertexNormalsWeighting{1};
        glm::vec3 m_MeshVertexNormalsFallback{0.0f, 1.0f, 0.0f};
        glm::vec3 m_GraphVertexNormalsFallback{0.0f, 0.0f, 1.0f};
        bool m_GraphVertexNormalsOrientTowardFallback{true};
        std::int32_t m_PointCloudVertexNormalsK{15};
        std::int32_t m_PointCloudVertexNormalsMinimumNeighbors{2};
        bool m_PointCloudVertexNormalsUseRadius{false};
        float m_PointCloudVertexNormalsRadius{0.0f};
        std::int32_t m_PointCloudVertexNormalsOrientation{1};
        glm::vec3 m_PointCloudVertexNormalsFallback{0.0f, 0.0f, 1.0f};
        std::int32_t m_PointCloudOutlierMethod{0};
        std::int32_t m_PointCloudOutlierKNeighbors{16};
        float m_PointCloudOutlierStdDevMultiplier{1.0f};
        float m_PointCloudOutlierSearchRadius{0.0f};
        std::int32_t m_PointCloudOutlierMinNeighbors{4};
        std::int32_t m_ProgressivePoissonDimension{3};
        std::int32_t m_ProgressivePoissonGridWidth{4};
        std::int32_t m_ProgressivePoissonMaxLevels{16};
        float m_ProgressivePoissonHashLoadFactor{0.25f};
        float m_ProgressivePoissonRadiusAlpha{-1.0f};
        bool m_ProgressivePoissonRandomizeGridOrigin{true};
        std::int32_t m_ProgressivePoissonGridOriginSeed{1337};
        bool m_ProgressivePoissonShuffleWithinLevels{true};
        std::int32_t m_ProgressivePoissonShuffleSeed{0x51ed270b};
        std::int32_t m_ProgressivePoissonPrefixCount{0};
        std::int32_t m_ProgressivePoissonChannel{0};
        std::int32_t m_ProgressivePoissonBackend{0};
        std::int32_t m_ProgressivePoissonMeshSurfaceSampleCount{4096};
        std::int32_t m_ProgressivePoissonMeshSurfaceSampleSeed{1337};
        float m_ProgressivePoissonMeshSurfaceMinTriangleArea{1.0e-14f};
        bool m_ProgressivePoissonMeshSurfaceInterpolateNormals{true};
        bool m_ProgressivePoissonAutoRunOnEdit{true};
        float m_ProgressivePoissonDebounceSeconds{0.25f};
        bool m_ProgressivePoissonAutoRunPending{false};
        double m_ProgressivePoissonLastEditTime{0.0};
        std::uint32_t m_ProgressivePoissonPendingStableEntityId{0u};
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
