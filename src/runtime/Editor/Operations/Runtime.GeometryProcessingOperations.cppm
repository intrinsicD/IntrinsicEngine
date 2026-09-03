// Exposes validated runtime geometry-processing commands, queued requests, and
// publication diagnostics shared by editor, config, and programmatic callers.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.RHI.Device;
export import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.ClusteringModule;
export import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
export import Extrinsic.Runtime.ParameterizationConfig;
export import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
export import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Graph.Vertex.Normals;
export import Geometry.HalfedgeMesh.CurvatureSegmentation;
export import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;
import Geometry.HalfedgeMesh.Vertices.Normals;
export import Geometry.Parameterization;
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.Utils;
import Geometry.Properties;
import Geometry.Smoothing;
import Geometry.UvAtlas;

namespace Extrinsic::Runtime
{
    struct EditorGeometryProcessingCommandsAccess;
}

export namespace Extrinsic::Runtime
{
    enum class EditorGeometryProcessingDomain : std::uint32_t
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

    [[nodiscard]] constexpr EditorGeometryProcessingDomain
    operator|(const EditorGeometryProcessingDomain lhs,
              const EditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<EditorGeometryProcessingDomain>(static_cast<std::uint32_t>(lhs) |
                                                           static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr EditorGeometryProcessingDomain
    operator&(const EditorGeometryProcessingDomain lhs,
              const EditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<EditorGeometryProcessingDomain>(static_cast<std::uint32_t>(lhs) &
                                                           static_cast<std::uint32_t>(rhs));
    }

    constexpr EditorGeometryProcessingDomain&
    operator|=(EditorGeometryProcessingDomain& lhs,
               const EditorGeometryProcessingDomain rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    [[nodiscard]] constexpr bool
    HasAnyEditorGeometryProcessingDomain(const EditorGeometryProcessingDomain domains,
                                         const EditorGeometryProcessingDomain query) noexcept
    {
        return static_cast<std::uint32_t>(domains & query) != 0u;
    }

    enum class EditorGeometryProcessingAlgorithm : std::uint8_t
    {
        KMeans,
        MeshDenoise,
        Curvature,
        CurvatureSegmentation,
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

    struct EditorGeometryProcessingCapabilities
    {
        EditorGeometryProcessingDomain Domains{EditorGeometryProcessingDomain::None};
        bool HasEditableSurfaceMesh{false};

        [[nodiscard]] bool HasAny() const noexcept
        {
            return HasEditableSurfaceMesh || Domains != EditorGeometryProcessingDomain::None;
        }
    };

    struct EditorGeometryProcessingEntry
    {
        EditorGeometryProcessingAlgorithm Algorithm{EditorGeometryProcessingAlgorithm::KMeans};
        EditorGeometryProcessingDomain Domains{EditorGeometryProcessingDomain::None};
    };

    struct EditorGeometryProcessingMenuItem
    {
        EditorGeometryProcessingDomain Domain{EditorGeometryProcessingDomain::None};
        const char* Label{""};
        bool HasNormalsMethod{false};
        bool HasDenoiseMethod{false};
        bool HasCurvatureMethod{false};
        bool HasRemeshMethod{false};
        bool HasSubdivideMethod{false};
        bool HasSimplifyMethod{false};
    };

    [[nodiscard]] std::vector<EditorGeometryProcessingMenuItem>
    GetEditorGeometryProcessingMenuItems(EditorDomainWindowKind kind);

    [[nodiscard]] EditorGeometryProcessingDomain GetEditorSupportedGeometryProcessingDomains(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;

    [[nodiscard]] bool
    SupportsEditorGeometryProcessingDomain(EditorGeometryProcessingAlgorithm algorithm,
                                           EditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] EditorGeometryProcessingCapabilities
    GetEditorGeometryProcessingCapabilities(const ECS::Scene::Registry& registry,
                                            ECS::EntityHandle entity);

    [[nodiscard]] std::vector<EditorGeometryProcessingEntry>
    ResolveEditorGeometryProcessingEntries(EditorGeometryProcessingCapabilities capabilities);

    [[nodiscard]] std::vector<EditorGeometryProcessingEntry>
    ResolveEditorGeometryProcessingEntries(const ECS::Scene::Registry& registry,
                                           ECS::EntityHandle entity);

    [[nodiscard]] std::vector<EditorGeometryProcessingDomain>
    GetAvailableEditorKMeansDomains(const ECS::Scene::Registry& registry, ECS::EntityHandle entity);

    [[nodiscard]] const char*
    DebugNameForEditorGeometryProcessingDomain(EditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForEditorGeometryProcessingAlgorithm(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;

    enum class EditorProgressivePoissonChannel : std::uint8_t
    {
        Level,
        Rank,
        SplatRadius,
        PrefixVisible,
    };

    enum class EditorProgressivePoissonBackend : std::uint8_t
    {
        CpuReference,
        VulkanCompute,
    };

    [[nodiscard]] const char*
    DebugNameForEditorProgressivePoissonChannel(EditorProgressivePoissonChannel channel) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorProgressivePoissonBackend(EditorProgressivePoissonBackend backend) noexcept;

    [[nodiscard]] EditorProgressivePoissonChannel
    MakeEditorProgressivePoissonChannel(ProgressivePoissonPlaygroundChannel channel) noexcept;

    [[nodiscard]] EditorProgressivePoissonBackend
    MakeEditorProgressivePoissonBackend(ProgressivePoissonPlaygroundBackend backend) noexcept;

    struct EditorProgressivePoissonConfig
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
        EditorProgressivePoissonChannel Channel{EditorProgressivePoissonChannel::Level};
        EditorProgressivePoissonBackend Backend{EditorProgressivePoissonBackend::CpuReference};
        bool AutoRunOnEdit{true};
        double DebounceSeconds{0.25};
    };

    [[nodiscard]] EditorProgressivePoissonConfig
    MakeEditorProgressivePoissonConfig(const ProgressivePoissonPlaygroundConfig& config) noexcept;

    struct EditorProgressivePoissonCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorProgressivePoissonConfig Config{};
    };

    struct EditorProgressivePoissonResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorProgressivePoissonChannel Channel{EditorProgressivePoissonChannel::Level};
        std::uint32_t InputCount{0u};
        std::uint32_t AcceptedCount{0u};
        std::uint32_t PrefixCount{0u};
        std::uint32_t LevelCount{0u};
        EditorProgressivePoissonBackend RequestedBackend{
            EditorProgressivePoissonBackend::CpuReference};
        EditorProgressivePoissonBackend ActualBackend{
            EditorProgressivePoissonBackend::CpuReference};
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
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    enum class EditorProgressivePoissonConfigStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        MissingConfigControl,
        PreviewRejected,
        ApplyRejected,
    };

    struct EditorProgressivePoissonConfigCommand
    {
        EditorProgressivePoissonConfig Config{};
        std::string SourceId{"sandbox.progressive_poisson"};
    };

    struct EditorProgressivePoissonConfigResult
    {
        EditorProgressivePoissonConfigStatus Status{EditorProgressivePoissonConfigStatus::None};
        Core::Config::EngineConfigLoadResult Preview{};
        RuntimeEngineConfigApplyResult Apply{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorProgressivePoissonConfigStatus::Applied ||
                   Status == EditorProgressivePoissonConfigStatus::NoChange;
        }
    };

    enum class EditorMeshDenoiseStage : std::uint8_t
    {
        FullBilateral,
    };

    enum class EditorMeshCurvatureOutput : std::uint8_t
    {
        All,
        Mean,
        Gaussian,
        PrincipalDirections,
    };

    [[nodiscard]] const char*
    DebugNameForEditorMeshCurvatureOutput(EditorMeshCurvatureOutput output) noexcept;

    enum class EditorMeshRemeshMode : std::uint8_t
    {
        Uniform,
        Adaptive,
    };

    enum class EditorMeshRemeshSizingLaw : std::uint8_t
    {
        MeanCurvature,
        ErrorBoundedTaubin,
    };

    enum class EditorMeshSubdivideOperator : std::uint8_t
    {
        Loop,
        CatmullClark,
        Sqrt3,
    };
    enum class EditorMeshSimplifyMetric : std::uint8_t
    {
        ClassicalQEM,
        FA_QEM,
    };

    [[nodiscard]] const char* DebugNameForEditorMeshRemeshMode(EditorMeshRemeshMode mode) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorMeshRemeshSizingLaw(EditorMeshRemeshSizingLaw sizingLaw) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorMeshSubdivideOperator(EditorMeshSubdivideOperator op) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorMeshSimplifyMetric(EditorMeshSimplifyMetric metric) noexcept;

    struct EditorMeshDenoiseCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshDenoiseStage Stage{EditorMeshDenoiseStage::FullBilateral};
        std::uint32_t NormalIterations{5u};
        std::uint32_t VertexIterations{10u};
        double SigmaSpatial{0.0};
        double SigmaRange{0.0};
        bool PreserveBoundary{true};
        double DegenerateNormalLengthEpsilon{1.0e-12};
    };

    struct EditorMeshDenoiseResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::Smoothing::DenoiseStatus DenoiseStatus{
            Geometry::Smoothing::DenoiseStatus::Success};
        EditorMeshDenoiseStage Stage{EditorMeshDenoiseStage::FullBilateral};
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

        // Applied means the mesh actually changed. A run that writes every slot
        // but moves no vertex reports NoChange, because WrittenCount is
        // slot-derived and stays non-zero whenever the kernel ran at all.
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }

        // Share of live vertices held fixed as boundary, in [0, 1]. Surfaced
        // alongside the absolute count because the count alone does not say
        // whether the run had anything left to smooth. Derived, not stored.
        [[nodiscard]] double PinnedBoundaryRatio() const noexcept
        {
            const std::size_t live = VertexSlotCount - SkippedDeletedVertexCount;
            if (live == 0u)
            {
                return 0.0;
            }
            return static_cast<double>(PinnedBoundaryVertexCount) /
                   static_cast<double>(live);
        }
    };

    struct EditorMeshCurvatureCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshCurvatureOutput Output{EditorMeshCurvatureOutput::All};
        bool PublishPrincipalDirections{true};
    };

    struct EditorMeshCurvatureResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshCurvatureOutput Output{EditorMeshCurvatureOutput::All};
        bool DirectionsRequested{true};
        bool DirectionsAvailable{true};
        bool DirectionsPublished{false};
        std::size_t VertexSlotCount{0u};
        // Estimator support is distinct from finite publication count: a
        // supported flat vertex may be zero, while zero supported vertices
        // means the operation produced no informative field.
        std::size_t SupportedVertexCount{0u};
        std::size_t NonZeroPrincipalVertexCount{0u};
        double MinimumPrincipalValue{0.0};
        double MaximumPrincipalValue{0.0};
        std::size_t DegenerateFaceCount{0u};
        std::size_t IllConditionedFaceCount{0u};
        std::size_t UnsupportedFaceCount{0u};
        double MinimumTriangleQuality{0.0};
        double TriangleQualityThreshold{0.0};
        // Counts all four scalar fields written (mean, Gaussian, and minimum
        // and maximum principal curvature), regardless of value changes.
        std::size_t ScalarPropertyCount{0u};
        std::size_t ScalarWrittenCount{0u};
        // Counts published curvature values that differ from stored values,
        // counted across every property the run publishes.
        std::size_t ChangedValueCount{0u};
        std::size_t DirectionPropertyCount{0u};
        std::size_t DirectionWrittenCount{0u};
        std::size_t NonFiniteScalarCount{0u};
        std::size_t NonFiniteDirectionCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorCurvatureSegmentationCommand
    {
        std::uint32_t StableEntityId{0u};
        CurvatureSegmentationConfig Config{};
    };

    struct EditorCurvatureSegmentationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        CurvatureSegmentationConfig Config{};
        CurvatureSegmentationMethod RequestedMethod{
            CurvatureSegmentationMethod::CurvatureGmm};
        CurvatureSegmentationMethod ActualMethod{
            CurvatureSegmentationMethod::CurvatureGmm};
        Geometry::CurvatureSegmentation::CurvatureSegmentationDiagnostics
            Diagnostics{};
        std::optional<
            Geometry::CurvatureSegmentation::FeatureEvidenceDiagnostics>
            FeatureDiagnostics{};
        std::optional<
            Geometry::CurvatureSegmentation::CurvaturePatchDiagnostics>
            PatchDiagnostics{};
        std::size_t ChangedValueCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            const bool commandSucceeded =
                Status == EditorCommandStatus::Applied ||
                Status == EditorCommandStatus::NoChange;
            if (!commandSucceeded || RequestedMethod != ActualMethod)
                return false;
            if (ActualMethod ==
                CurvatureSegmentationMethod::FeatureAlignedPatches)
            {
                return FeatureDiagnostics.has_value() &&
                       FeatureDiagnostics->Succeeded() &&
                       PatchDiagnostics.has_value() &&
                       PatchDiagnostics->Succeeded();
            }
            return Diagnostics.Succeeded();
        }
    };

    struct EditorMeshRemeshCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshRemeshMode Mode{EditorMeshRemeshMode::Uniform};
        EditorMeshRemeshSizingLaw SizingLaw{EditorMeshRemeshSizingLaw::MeanCurvature};
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

    // BUG-146 — what a topology-replacing mesh operation did to the mesh's UV
    // parameterization. These operations rebuild the entity's halfedge mesh, so
    // a UV property the rebuilt mesh does not carry is removed outright rather
    // than left stale. That outcome is reported, never silent.
    enum class EditorMeshTexcoordOutcome : std::uint8_t
    {
        // The mesh carried no resolvable UVs, so there was nothing to keep.
        None = 0,
        // The output carries the mesh's UVs.
        Preserved = 1,
        // The operation could not carry the UVs onto the topology it produced,
        // so the parameterization was discarded. Re-parameterize afterwards.
        Discarded = 2,
    };

    [[nodiscard]] const char* DebugNameForEditorMeshTexcoordOutcome(
        EditorMeshTexcoordOutcome outcome) noexcept;

    struct EditorMeshRemeshResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshRemeshMode Mode{EditorMeshRemeshMode::Uniform};
        EditorMeshRemeshSizingLaw SizingLaw{EditorMeshRemeshSizingLaw::MeanCurvature};
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
        EditorMeshTexcoordOutcome TexcoordOutcome{
            EditorMeshTexcoordOutcome::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorMeshSubdivideCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshSubdivideOperator Operator{EditorMeshSubdivideOperator::Loop};
        std::uint32_t Iterations{1u};
        bool PreserveLoopFeatureEdges{false};
        std::uint32_t MaxOutputFaces{0u};
        std::string FeatureEdgePropertyName{"e:feature"};
    };

    struct EditorMeshSubdivideResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshSubdivideOperator Operator{EditorMeshSubdivideOperator::Loop};
        std::uint32_t IterationsRequested{0u};
        std::uint32_t IterationsPerformed{0u};
        bool PreserveLoopFeatureEdges{false};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        EditorMeshTexcoordOutcome TexcoordOutcome{
            EditorMeshTexcoordOutcome::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorMeshSimplifyCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshSimplifyMetric Metric{EditorMeshSimplifyMetric::FA_QEM};
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

    struct EditorMeshSimplifyResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshSimplifyMetric Metric{EditorMeshSimplifyMetric::FA_QEM};
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
        EditorMeshTexcoordOutcome TexcoordOutcome{
            EditorMeshTexcoordOutcome::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };
    enum class EditorICPVariant : std::uint8_t
    {
        PointToPoint,
        PointToPlane,
    };

    [[nodiscard]] const char* DebugNameForEditorICPVariant(EditorICPVariant variant) noexcept;

    struct EditorRegistrationCommand
    {
        std::uint32_t SourceStableEntityId{0u};
        std::uint32_t TargetStableEntityId{0u};
        EditorICPVariant Variant{EditorICPVariant::PointToPoint};
        std::uint32_t MaxIterations{50u};
        // World-space correspondence cutoff; <= 0 disables the cutoff.
        double MaxCorrespondenceDistance{0.0};
        double InlierRatio{0.9};
        // Trajectory step whose cumulative source->target pose is written to the
        // source entity Transform. 0 = identity (un-registered start); values at
        // or beyond the completed iteration count clamp to the converged pose.
        std::size_t TrajectoryStep{0u};
    };

    struct EditorRegistrationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        bool HasResult{false};
        // The variant the command asked for.
        EditorICPVariant Variant{EditorICPVariant::PointToPoint};
        // BUG-096: the variant the solver actually ran. `Geometry.Registration`
        // silently degrades `PointToPlane` to `PointToPoint` when target
        // normals are absent or count-mismatched, and the runtime used to
        // report the requested variant regardless. Runtime now fails a
        // point-to-plane request closed rather than degrading it, so on a
        // successful run these agree; the field exists so a disagreement is
        // visible rather than invisible.
        EditorICPVariant EffectiveVariant{EditorICPVariant::PointToPoint};
        std::size_t SourcePointCount{0u};
        std::size_t TargetPointCount{0u};
        // Target normals resolved, transformed to world space, and handed to
        // the solver. Zero for a point-to-point run.
        std::size_t TargetNormalCount{0u};
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
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorMeshVertexNormalsCommand
    {
        std::uint32_t StableEntityId{0u};
        Geometry::HalfedgeMesh::VertexNormals::AveragingMode Weighting{
            Geometry::HalfedgeMesh::VertexNormals::AveragingMode::AreaWeighted};
        glm::vec3 FallbackNormal{0.0f, 1.0f, 0.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
    };

    struct EditorMeshVertexNormalsResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::HalfedgeMesh::VertexNormals::RecomputeStatus NormalStatus{
            Geometry::HalfedgeMesh::VertexNormals::RecomputeStatus::Success};
        Geometry::HalfedgeMesh::VertexNormals::AveragingMode Weighting{
            Geometry::HalfedgeMesh::VertexNormals::AveragingMode::AreaWeighted};
        std::size_t VertexSlotCount{0};
        std::size_t WrittenCount{0};
        // BUG-145: how many published normals actually differ from the ones
        // already stored. `WrittenCount` is non-zero whenever the kernel ran at
        // all, so it cannot decide `Applied` versus `NoChange`.
        std::size_t ChangedNormalCount{0};
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
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorGraphVertexNormalsCommand
    {
        std::uint32_t StableEntityId{0u};
        glm::vec3 FallbackNormal{0.0f, 0.0f, 1.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
        double CollinearEigenvalueRatioEpsilon{1.0e-5};
        bool OrientTowardFallback{true};
    };

    struct EditorGraphVertexNormalsResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::Graph::VertexNormals::RecomputeStatus NormalStatus{
            Geometry::Graph::VertexNormals::RecomputeStatus::Success};
        bool OrientTowardFallback{true};
        std::size_t VertexSlotCount{0};
        std::size_t EdgeSlotCount{0};
        std::size_t WrittenCount{0};
        // BUG-145: see EditorMeshVertexNormalsResult::ChangedNormalCount.
        std::size_t ChangedNormalCount{0};
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
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorPointCloudVertexNormalsCommand
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

    struct EditorPointCloudVertexNormalsResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
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
        // BUG-145: see EditorMeshVertexNormalsResult::ChangedNormalCount.
        std::size_t ChangedNormalCount{0};
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
            return Status == EditorCommandStatus::Applied;
        }
    };
    enum class EditorPointCloudOutlierMethod : std::uint8_t
    {
        Statistical, // GEOM-016 RemoveStatisticalOutliers (mean-kNN distance).
        Radius,      // GEOM-016 RemoveRadiusOutliers (neighbor count in radius).
    };

    struct EditorPointCloudOutlierRemovalCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorPointCloudOutlierMethod Method{EditorPointCloudOutlierMethod::Statistical};
        std::uint32_t KNeighbors{16u};
        float StdDevMultiplier{1.0f};
        float SearchRadius{0.0f};
        std::uint32_t MinNeighbors{4u};
    };

    struct EditorPointCloudOutlierRemovalResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorPointCloudOutlierMethod Method{EditorPointCloudOutlierMethod::Statistical};
        Geometry::PointCloud::OutlierRemovalStatus GeometryStatus{
            Geometry::PointCloud::OutlierRemovalStatus::Success};
        std::size_t OriginalCount{0};
        std::size_t KeptCount{0};
        std::size_t RejectedCount{0};
        std::size_t NonFiniteCount{0};
        float MeanDistance{0.0f};
        float StdDevDistance{0.0f};
        float DistanceThreshold{0.0f};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorUvDiagnosticsModel
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
        std::optional<EditorJobModel> UvRegenerationJob{};
    };

    struct EditorUvRegenerationCommandResult;
    struct EditorParameterizationResult;

    // BUG-141: names one stored `Last<Operation>Result` slot so a panel can
    // dismiss the outcome it is showing. Every result is already superseded by
    // the next run of its own operation; this is the explicit clear, and it is
    // one enum rather than one sink per operation because the session's only
    // reaction is to reset the matching optional.
    enum class EditorGeometryProcessingResultSlot : std::uint8_t
    {
        KMeans,
        PointCloudConsolidation,
        ProgressivePoisson,
        UvRegeneration,
        Parameterization,
        MeshCurvature,
        MeshDenoise,
        MeshRemesh,
        MeshSubdivide,
        MeshSimplify,
        MeshVertexNormals,
        GraphVertexNormals,
        PointCloudVertexNormals,
        PointCloudOutlierRemoval,
        Registration,
    };

    struct EditorMethodResultSinks
    {
        std::function<void(EditorGeometryProcessingResultSlot)> DismissResult{};
        std::function<void(EditorProgressivePoissonResult)> ProgressivePoisson{};
        std::function<void(EditorUvRegenerationCommandResult)> UvRegeneration{};
        std::function<void(EditorParameterizationResult)> Parameterization{};
        std::function<void(EditorMeshCurvatureResult)> MeshCurvature{};
        std::function<void(EditorMeshDenoiseResult)> MeshDenoise{};
        std::function<void(EditorMeshRemeshResult)> MeshRemesh{};
        std::function<void(EditorMeshSubdivideResult)> MeshSubdivide{};
        std::function<void(EditorMeshSimplifyResult)> MeshSimplify{};
        std::function<void(EditorMeshVertexNormalsResult)> MeshVertexNormals{};
        std::function<void(EditorGraphVertexNormalsResult)> GraphVertexNormals{};
        std::function<void(EditorPointCloudVertexNormalsResult)> PointCloudVertexNormals{};
        std::function<void(EditorPointCloudOutlierRemovalResult)> PointCloudOutlierRemoval{};
        std::function<void(EditorRegistrationResult)> Registration{};
    };

    struct EditorGeometryProcessingModel
    {
        bool HasSelectedEntity{false};
        bool DirectMeshEnrichmentPending{false};
        JobState DirectMeshEnrichmentStatus{JobState::Invalid};
        std::string DirectMeshEnrichmentDiagnostic{};
        EditorGeometryProcessingCapabilities Capabilities{};
        std::vector<EditorGeometryProcessingEntry> Entries{};
        std::vector<EditorGeometryProcessingDomain> KMeansDomains{};
        bool MeshDenoiseAvailable{false};
        bool MeshCurvatureAvailable{false};
        bool MeshCurvatureDirectionsAvailable{false};
        bool CurvatureSegmentationAvailable{false};
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
        bool ProgressivePoissonAvailable{false};
        std::string ProgressivePoissonDisabledReason{};
        std::optional<KMeansRunCompleted> LastKMeansResult{};
        std::optional<EditorMeshDenoiseResult> LastMeshDenoiseResult{};
        std::optional<EditorMeshCurvatureResult> LastMeshCurvatureResult{};
        std::optional<EditorMeshRemeshResult> LastMeshRemeshResult{};
        std::optional<EditorMeshSubdivideResult> LastMeshSubdivideResult{};
        std::optional<EditorMeshSimplifyResult> LastMeshSimplifyResult{};
        std::optional<EditorMeshVertexNormalsResult> LastMeshVertexNormalsResult{};
        std::optional<EditorGraphVertexNormalsResult> LastGraphVertexNormalsResult{};
        std::optional<EditorPointCloudVertexNormalsResult> LastPointCloudVertexNormalsResult{};
        std::optional<EditorPointCloudOutlierRemovalResult> LastPointCloudOutlierRemovalResult{};
        std::optional<EditorProgressivePoissonResult> LastProgressivePoissonResult{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    enum class EditorParameterizationUvViewStatus : std::uint8_t
    {
        Disabled,
        CpuLayout,
        CpuFallbackNonOperational,
        WaitingForGeometry,
        WaitingForGpuFrame,
        InvalidRequest,
        ResourceCreationFailed,
        Ready,
    };

    struct EditorParameterizationUvViewRequest
    {
        bool Enabled{false};
        std::uint64_t RequestToken{0u};
        std::uint32_t StableEntityId{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        glm::vec2 UvBoundsMin{0.0f};
        glm::vec2 UvBoundsMax{1.0f};
        ParameterizationViewConfig View{};
        std::vector<std::uint32_t> LineIndices{};
        std::vector<float> TriangleConformalDistortion{};
    };
    struct EditorParameterizationUvViewState
    {
        EditorParameterizationUvViewStatus Status{EditorParameterizationUvViewStatus::Disabled};
        ParameterizationUvRenderMode RequestedMode{ParameterizationUvRenderMode::CpuLayout};
        ParameterizationUvRenderMode ActiveMode{ParameterizationUvRenderMode::CpuLayout};
        ParameterizationUvBackgroundMode RequestedBackground{
            ParameterizationUvBackgroundMode::Grid};
        ParameterizationUvBackgroundMode ActiveBackground{ParameterizationUvBackgroundMode::Grid};
        bool HeatmapActive{false};
        bool GpuReady{false};
        std::uint64_t RequestToken{0u};
        std::uint32_t BindlessIndex{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint64_t TargetGeneration{0u};
        std::uint64_t RecordedPassCount{0u};
        std::string Message{};
    };

    struct EditorParameterizationUvViewCommandSurface
    {
        std::function<EditorParameterizationUvViewState(EditorParameterizationUvViewRequest)>
            Submit{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Submit);
        }
    };

    struct EditorGeometryProcessingContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{DefaultWorldHandle};
        SelectionController* Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        RHI::IDevice* Device{nullptr};
        ClusteringService* Clustering{nullptr};
        PointCloudConsolidationService* PointCloudConsolidation{nullptr};
        EditorParameterizationUvViewCommandSurface ParameterizationUvViewCommands{};
        EditorJobCommandSurface JobCommands{};
        EditorMethodResultSinks MethodResultSinks{};
        const KMeansRunCompleted* LastKMeansResult{nullptr};
        const PointCloudConsolidationResult* LastPointCloudConsolidationResult{nullptr};
        const EditorMeshDenoiseResult* LastMeshDenoiseResult{nullptr};
        const EditorMeshCurvatureResult* LastMeshCurvatureResult{nullptr};
        const EditorMeshRemeshResult* LastMeshRemeshResult{nullptr};
        const EditorMeshSubdivideResult* LastMeshSubdivideResult{nullptr};
        const EditorMeshSimplifyResult* LastMeshSimplifyResult{nullptr};
        const EditorMeshVertexNormalsResult* LastMeshVertexNormalsResult{nullptr};
        const EditorGraphVertexNormalsResult* LastGraphVertexNormalsResult{nullptr};
        const EditorPointCloudVertexNormalsResult* LastPointCloudVertexNormalsResult{nullptr};
        const EditorPointCloudOutlierRemovalResult* LastPointCloudOutlierRemovalResult{nullptr};
        const EditorUvRegenerationCommandResult* LastUvRegenerationResult{nullptr};
        const EditorParameterizationResult* LastParameterizationResult{nullptr};
        const EditorProgressivePoissonResult* LastProgressivePoissonResult{nullptr};
        const EditorRegistrationResult* LastRegistrationResult{nullptr};
        const RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<RuntimeEngineConfigApplyResult(const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        std::function<bool()> AttachmentActive{};
        std::function<void()> InvalidateWorkspaceSnapshotCache{};
        bool EngineConfigCommandsAvailable{false};
        bool MeshDenoiseKernelAvailable{true};
        bool MeshCurvatureKernelAvailable{true};
        bool MeshCurvatureDirectionsAvailable{true};
        bool CurvatureSegmentationKernelAvailable{true};
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

    class EditorGeometryProcessingCommands final
    {
    public:
        EditorGeometryProcessingCommands() = default;

        [[nodiscard]] bool IsBound() const noexcept;

    private:
        struct State;
        std::shared_ptr<const State> m_State{};

        explicit EditorGeometryProcessingCommands(std::shared_ptr<const State> state);
        friend struct EditorGeometryProcessingCommandsAccess;
        friend EditorGeometryProcessingCommands
        BindEditorGeometryProcessingCommands(EditorGeometryProcessingContext context);
    };

    [[nodiscard]] EditorGeometryProcessingCommands
    BindEditorGeometryProcessingCommands(EditorGeometryProcessingContext context);

    [[nodiscard]] bool AreEditorGeometryConfigCommandsAvailable(
        const EditorGeometryProcessingContext& context) noexcept;
    [[nodiscard]] bool
    IsEditorClusteringAvailable(const EditorGeometryProcessingContext& context) noexcept;
    [[nodiscard]] bool IsEditorPointCloudConsolidationAvailable(
        const EditorGeometryProcessingContext& context) noexcept;
    [[nodiscard]] KMeansRunCompleted
    SubmitKMeansRun(const EditorGeometryProcessingContext& context,
                    const RunKMeans& command);
    [[nodiscard]] PointCloudConsolidationResult
    SubmitEditorPointCloudConsolidation(
        const EditorGeometryProcessingContext& context,
        PointCloudConsolidationRequest request);
    [[nodiscard]] PointCloudConsolidationAvailability
    ResolveEditorPointCloudConsolidationAvailability(
        const EditorGeometryProcessingContext& context,
        const PointCloudConsolidationRequest& request);

    [[nodiscard]] Geometry::ConstPropertySet
    ResolveEditorSelectedMeshVertexProperties(const EditorGeometryProcessingContext& context);

    struct EditorUvRegenerationCommand
    {
        std::uint32_t StableEntityId{0u};
        bool PreserveValidAuthoredUvs{false};
        bool ForceRegenerate{true};
        std::uint32_t Resolution{1024u};
        std::uint32_t Padding{2u};
        float TexelsPerUnit{0.0f};
        std::string BackendName{"xatlas"};
    };

    struct EditorUvRegenerationCommandResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::UvAtlas::UvAtlasStatus UvStatus{Geometry::UvAtlas::UvAtlasStatus::Success};
        Geometry::UvAtlas::UvAtlasProvenance Provenance{Geometry::UvAtlas::UvAtlasProvenance::None};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint32_t ChartCount{0u};
        // BUG-147 — how many extra vertices an indexed GPU vertex buffer needs
        // to carry this atlas's seams, i.e. the duplication that happens once
        // at upload. It is not, and must never again become, a count of
        // vertices added to the mesh: the mesh keeps its own topology and the
        // seam is carried on the corner domain.
        std::size_t SeamSplitVertexCount{0u};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    using EditorParameterizationStrategy = ParameterizationStrategyKind;

    [[nodiscard]] std::string_view
    StableTokenForEditorParameterizationStrategy(EditorParameterizationStrategy strategy) noexcept;

    struct EditorParameterizationCommand
    {
        std::uint32_t StableEntityId{0u};
        ParameterizationConfig Config{};
    };

    struct EditorConfiguredParameterizationCommand
    {
        std::uint32_t StableEntityId{0u};
    };

    struct EditorParameterizationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::uint32_t StableEntityId{0u};
        EditorParameterizationStrategy Strategy{EditorParameterizationStrategy::Lscm};
        std::string StrategyToken{"lscm"};
        Geometry::Parameterization::ParameterizationStatus ParameterizationStatus{
            Geometry::Parameterization::ParameterizationStatus::InvalidInput};
        Geometry::Parameterization::ParameterizationDiagnostics Diagnostics{};
        // BUG-141: the structured cause behind a solver rejection. Populated
        // only when the solver ran and refused the mesh; a rejection raised
        // before the solver (stale entity, bad config, unusable source) leaves
        // it unevaluated because no mesh reached the solver.
        Geometry::Parameterization::ParameterizationRejection Rejection{};
        std::size_t VertexCount{0u};
        // Identifies the exact canonical triangle topology and UV payload that
        // produced Diagnostics. A missing value means the diagnostics must not
        // be projected onto the current UV view.
        // Exact provenance for face diagnostics: rendered topology-to-face
        // mapping, vertex positions, and UV coordinates.
        std::optional<std::uint64_t> DiagnosticInputFingerprint{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied &&
                   ParameterizationStatus ==
                       Geometry::Parameterization::ParameterizationStatus::Success;
        }
    };

    enum class EditorParameterizationConfigStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        MissingConfigControl,
        PreviewRejected,
        ApplyRejected,
    };

    struct EditorParameterizationConfigCommand
    {
        ParameterizationConfig Config{};
        std::string SourceId{"sandbox.parameterization"};
    };

    struct EditorParameterizationConfigResult
    {
        EditorParameterizationConfigStatus Status{EditorParameterizationConfigStatus::None};
        Core::Config::EngineConfigLoadResult Preview{};
        RuntimeEngineConfigApplyResult Apply{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorParameterizationConfigStatus::Applied ||
                   Status == EditorParameterizationConfigStatus::NoChange;
        }
    };

    struct EditorParameterizationViewModel
    {
        bool HasSelectedEntity{false};
        bool SelectedEntityIsMesh{false};
        bool HasUvCoordinates{false};
        bool HasFiniteUvBounds{false};
        bool HasLastResult{false};
        std::uint32_t SelectedStableEntityId{0u};
        // Matches LastParameterizationResult only while every input consumed
        // by the position- and UV-dependent face diagnostics is unchanged.
        std::optional<std::uint64_t> DiagnosticInputFingerprint{};
        EditorParameterizationStrategy Strategy{EditorParameterizationStrategy::Lscm};
        ParameterizationViewConfig View{};
        std::vector<glm::vec2> UVs{};
        std::vector<std::array<std::uint32_t, 3u>> Triangles{};
        std::vector<std::uint32_t> LineIndices{};
        std::vector<float> TriangleConformalDistortion{};
        glm::vec2 UvBoundsMin{0.0f};
        glm::vec2 UvBoundsMax{0.0f};
        std::optional<Geometry::Parameterization::ParameterizationStatus> LastStatus{};
        std::optional<Geometry::Parameterization::ParameterizationDiagnostics> LastDiagnostics{};
        std::string Message{};
    };

    [[nodiscard]] const char*
    DebugNameForEditorUvAtlasStatus(Geometry::UvAtlas::UvAtlasStatus status) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorUvAtlasProvenance(Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept;
    EditorUvRegenerationCommandResult
    ApplyEditorUvRegenerationCommand(const EditorGeometryProcessingContext& context,
                                     const EditorUvRegenerationCommand& command);

    EditorParameterizationResult
    ApplyEditorParameterizationCommand(const EditorGeometryProcessingContext& context,
                                       const EditorParameterizationCommand& command);

    EditorParameterizationResult ApplyEditorConfiguredParameterizationCommand(
        const EditorGeometryProcessingContext& context,
        const EditorConfiguredParameterizationCommand& command);

    [[nodiscard]] EditorParameterizationConfigResult
    ApplyEditorParameterizationConfigCommand(const EditorGeometryProcessingContext& context,
                                             const EditorParameterizationConfigCommand& command);

    [[nodiscard]] std::optional<ParameterizationConfig>
    GetEditorParameterizationConfig(const EditorGeometryProcessingContext& context) noexcept;

    [[nodiscard]] EditorParameterizationViewModel
    BuildEditorParameterizationViewModel(const EditorGeometryProcessingContext& context);

    [[nodiscard]] EditorParameterizationUvViewState
    SubmitEditorParameterizationUvView(const EditorGeometryProcessingContext& context,
                                       const EditorParameterizationViewModel& model,
                                       std::uint32_t width, std::uint32_t height);

    void DisableEditorParameterizationUvView(const EditorGeometryProcessingContext& context);

    [[nodiscard]] const char* DebugNameForEditorParameterizationUvViewStatus(
        EditorParameterizationUvViewStatus status) noexcept;

    EditorMeshDenoiseResult
    ApplyEditorMeshDenoiseCommand(const EditorGeometryProcessingContext& context,
                                  const EditorMeshDenoiseCommand& command);

    EditorMeshCurvatureResult
    ApplyEditorMeshCurvatureCommand(const EditorGeometryProcessingContext& context,
                                    const EditorMeshCurvatureCommand& command);

    EditorCurvatureSegmentationResult
    ApplyEditorCurvatureSegmentationCommand(
        const EditorGeometryProcessingContext& context,
        const EditorCurvatureSegmentationCommand& command);

    EditorCurvatureSegmentationResult
    ApplyEditorConfiguredCurvatureSegmentationCommand(
        const EditorGeometryProcessingContext& context,
        std::uint32_t stableEntityId);

    EditorMeshRemeshResult
    ApplyEditorMeshRemeshCommand(const EditorGeometryProcessingContext& context,
                                 const EditorMeshRemeshCommand& command);

    EditorMeshSubdivideResult
    ApplyEditorMeshSubdivideCommand(const EditorGeometryProcessingContext& context,
                                    const EditorMeshSubdivideCommand& command);

    EditorMeshSimplifyResult
    ApplyEditorMeshSimplifyCommand(const EditorGeometryProcessingContext& context,
                                   const EditorMeshSimplifyCommand& command);

    EditorMeshVertexNormalsResult
    ApplyEditorMeshVertexNormalsCommand(const EditorGeometryProcessingContext& context,
                                        const EditorMeshVertexNormalsCommand& command);

    EditorGraphVertexNormalsResult
    ApplyEditorGraphVertexNormalsCommand(const EditorGeometryProcessingContext& context,
                                         const EditorGraphVertexNormalsCommand& command);

    EditorPointCloudVertexNormalsResult
    ApplyEditorPointCloudVertexNormalsCommand(const EditorGeometryProcessingContext& context,
                                              const EditorPointCloudVertexNormalsCommand& command);

    EditorPointCloudOutlierRemovalResult ApplyEditorPointCloudOutlierRemovalCommand(
        const EditorGeometryProcessingContext& context,
        const EditorPointCloudOutlierRemovalCommand& command);

    EditorRegistrationResult
    ApplyEditorRegistrationCommand(const EditorGeometryProcessingContext& context,
                                   const EditorRegistrationCommand& command);

    EditorProgressivePoissonResult
    ApplyEditorProgressivePoissonCommand(const EditorGeometryProcessingContext& context,
                                         const EditorProgressivePoissonCommand& command);

    [[nodiscard]] EditorProgressivePoissonConfigResult ApplyEditorProgressivePoissonConfigCommand(
        const EditorGeometryProcessingContext& context,
        const EditorProgressivePoissonConfigCommand& command);

    [[nodiscard]] RuntimeEngineConfigApplyResult
    ApplyEditorClusteringConfig(const EditorGeometryProcessingContext& context,
                                const ClusteringConfig& config,
                                std::string sourceId = "sandbox.clustering");

    [[nodiscard]] std::optional<ClusteringConfig>
    GetEditorClusteringConfig(const EditorGeometryProcessingContext& context) noexcept;

    [[nodiscard]] RuntimeEngineConfigApplyResult
    ApplyEditorCurvatureSegmentationConfig(
        const EditorGeometryProcessingContext& context,
        const CurvatureSegmentationConfig& config,
        std::string sourceId = "sandbox.curvature_segmentation");

    [[nodiscard]] std::optional<CurvatureSegmentationConfig>
    GetEditorCurvatureSegmentationConfig(
        const EditorGeometryProcessingContext& context) noexcept;

    [[nodiscard]] RuntimeEngineConfigApplyResult
    ApplyEditorPointCloudConsolidationConfig(
        const EditorGeometryProcessingContext& context,
        const PointCloudConsolidationConfig& config,
        std::string sourceId = "sandbox.point_cloud_consolidation");

    [[nodiscard]] bool IsValidEditorPointCloudConsolidationConfig(
        const PointCloudConsolidationConfig& config);

    [[nodiscard]] std::optional<PointCloudConsolidationConfig>
    GetEditorPointCloudConsolidationConfig(
        const EditorGeometryProcessingContext& context) noexcept;

    [[nodiscard]] std::optional<EditorProgressivePoissonConfig>
    GetEditorProgressivePoissonConfig(const EditorGeometryProcessingContext& context) noexcept;

    [[nodiscard]] KMeansRunCompleted
    SubmitKMeansRun(const EditorGeometryProcessingCommands& commands,
                    const RunKMeans& command);
    [[nodiscard]] PointCloudConsolidationResult
    SubmitEditorPointCloudConsolidation(
        const EditorGeometryProcessingCommands& commands,
        PointCloudConsolidationRequest request);
    [[nodiscard]] PointCloudConsolidationAvailability
    ResolveEditorPointCloudConsolidationAvailability(
        const EditorGeometryProcessingCommands& commands,
        const PointCloudConsolidationRequest& request);
    [[nodiscard]] Geometry::ConstPropertySet
    ResolveEditorSelectedMeshVertexProperties(
        const EditorGeometryProcessingCommands& commands);
    EditorUvRegenerationCommandResult ApplyEditorUvRegenerationCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorUvRegenerationCommand& command);
    EditorParameterizationResult ApplyEditorParameterizationCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorParameterizationCommand& command);
    EditorParameterizationResult ApplyEditorConfiguredParameterizationCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorConfiguredParameterizationCommand& command);
    [[nodiscard]] EditorParameterizationConfigResult
    ApplyEditorParameterizationConfigCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorParameterizationConfigCommand& command);
    [[nodiscard]] std::optional<ParameterizationConfig>
    GetEditorParameterizationConfig(
        const EditorGeometryProcessingCommands& commands) noexcept;
    [[nodiscard]] EditorParameterizationViewModel
    BuildEditorParameterizationViewModel(
        const EditorGeometryProcessingCommands& commands);
    [[nodiscard]] EditorParameterizationUvViewState
    SubmitEditorParameterizationUvView(
        const EditorGeometryProcessingCommands& commands,
        const EditorParameterizationViewModel& model,
        std::uint32_t width,
        std::uint32_t height);
    void DisableEditorParameterizationUvView(
        const EditorGeometryProcessingCommands& commands);
    EditorMeshDenoiseResult ApplyEditorMeshDenoiseCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorMeshDenoiseCommand& command);
    EditorMeshCurvatureResult ApplyEditorMeshCurvatureCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorMeshCurvatureCommand& command);
    EditorCurvatureSegmentationResult
    ApplyEditorCurvatureSegmentationCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorCurvatureSegmentationCommand& command);
    EditorCurvatureSegmentationResult
    ApplyEditorConfiguredCurvatureSegmentationCommand(
        const EditorGeometryProcessingCommands& commands,
        std::uint32_t stableEntityId);
    EditorMeshRemeshResult ApplyEditorMeshRemeshCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorMeshRemeshCommand& command);
    EditorMeshSubdivideResult ApplyEditorMeshSubdivideCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorMeshSubdivideCommand& command);
    EditorMeshSimplifyResult ApplyEditorMeshSimplifyCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorMeshSimplifyCommand& command);
    EditorMeshVertexNormalsResult ApplyEditorMeshVertexNormalsCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorMeshVertexNormalsCommand& command);
    EditorGraphVertexNormalsResult ApplyEditorGraphVertexNormalsCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorGraphVertexNormalsCommand& command);
    EditorPointCloudVertexNormalsResult ApplyEditorPointCloudVertexNormalsCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorPointCloudVertexNormalsCommand& command);
    EditorPointCloudOutlierRemovalResult ApplyEditorPointCloudOutlierRemovalCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorPointCloudOutlierRemovalCommand& command);
    EditorRegistrationResult ApplyEditorRegistrationCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorRegistrationCommand& command);
    EditorProgressivePoissonResult ApplyEditorProgressivePoissonCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorProgressivePoissonCommand& command);
    [[nodiscard]] EditorProgressivePoissonConfigResult
    ApplyEditorProgressivePoissonConfigCommand(
        const EditorGeometryProcessingCommands& commands,
        const EditorProgressivePoissonConfigCommand& command);
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorClusteringConfig(
        const EditorGeometryProcessingCommands& commands,
        const ClusteringConfig& config,
        std::string sourceId = "sandbox.clustering");
    [[nodiscard]] std::optional<ClusteringConfig>
    GetEditorClusteringConfig(
        const EditorGeometryProcessingCommands& commands) noexcept;
    [[nodiscard]] RuntimeEngineConfigApplyResult
    ApplyEditorCurvatureSegmentationConfig(
        const EditorGeometryProcessingCommands& commands,
        const CurvatureSegmentationConfig& config,
        std::string sourceId = "sandbox.curvature_segmentation");
    [[nodiscard]] std::optional<CurvatureSegmentationConfig>
    GetEditorCurvatureSegmentationConfig(
        const EditorGeometryProcessingCommands& commands) noexcept;
    [[nodiscard]] RuntimeEngineConfigApplyResult
    ApplyEditorPointCloudConsolidationConfig(
        const EditorGeometryProcessingCommands& commands,
        const PointCloudConsolidationConfig& config,
        std::string sourceId = "sandbox.point_cloud_consolidation");
    [[nodiscard]] std::optional<PointCloudConsolidationConfig>
    GetEditorPointCloudConsolidationConfig(
        const EditorGeometryProcessingCommands& commands) noexcept;
    [[nodiscard]] std::optional<EditorProgressivePoissonConfig>
    GetEditorProgressivePoissonConfig(
        const EditorGeometryProcessingCommands& commands) noexcept;

    struct EditorGeometryProcessingResultsSnapshot
    {
        std::optional<KMeansRunCompleted> LastKMeansResult{};
        std::optional<PointCloudConsolidationResult> LastPointCloudConsolidationResult{};
        std::optional<EditorMeshDenoiseResult> LastMeshDenoiseResult{};
        std::optional<EditorMeshCurvatureResult> LastMeshCurvatureResult{};
        std::optional<EditorMeshRemeshResult> LastMeshRemeshResult{};
        std::optional<EditorMeshSubdivideResult> LastMeshSubdivideResult{};
        std::optional<EditorMeshSimplifyResult> LastMeshSimplifyResult{};
        std::optional<EditorMeshVertexNormalsResult> LastMeshVertexNormalsResult{};
        std::optional<EditorGraphVertexNormalsResult> LastGraphVertexNormalsResult{};
        std::optional<EditorPointCloudVertexNormalsResult> LastPointCloudVertexNormalsResult{};
        std::optional<EditorPointCloudOutlierRemovalResult> LastPointCloudOutlierRemovalResult{};
        std::optional<EditorUvRegenerationCommandResult> LastUvRegenerationResult{};
        std::optional<EditorParameterizationResult> LastParameterizationResult{};
        std::optional<EditorProgressivePoissonResult> LastProgressivePoissonResult{};
        std::optional<EditorRegistrationResult> LastRegistrationResult{};
    };

    struct EditorGeometryProcessingPreparedFrame
    {
        EditorGeometryProcessingCommands Commands{};
        EditorMethodResultSinks ResultSinks{};
        EditorGeometryProcessingResultsSnapshot Results{};
        bool ConfigCommandsAvailable{false};
        bool ClusteringAvailable{false};
        bool PointCloudConsolidationAvailable{false};
    };

    [[nodiscard]] EditorGeometryProcessingPreparedFrame
    PrepareEditorGeometryProcessingFrame(
        const EditorWorkspaceAttachment& attachment);
} // namespace Extrinsic::Runtime

namespace Extrinsic::Runtime
{
    struct EditorGeometryProcessingCommandsAccess final
    {
        [[nodiscard]] static const EditorGeometryProcessingContext*
        Resolve(const EditorGeometryProcessingCommands& commands) noexcept;
    };
}
