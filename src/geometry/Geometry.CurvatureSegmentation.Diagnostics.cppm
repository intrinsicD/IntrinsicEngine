// Shared segmentation status and diagnostic records let runtime report outcomes
// without importing mesh containers or the algorithms that produce them.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

export module Geometry.CurvatureSegmentation.Diagnostics;

export namespace Geometry::CurvatureSegmentation
{
    enum class SegmentationStatus : std::uint8_t
    {
        Success = 0,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        FeatureCountMismatch,
        NonTriangleFace,
        NonFinitePosition,
        DegenerateFace,
        NonFiniteFeature,
        GaussianMixtureFitFailed,
        PosteriorEvaluationFailed,
    };

    struct ModelCandidateDiagnostics
    {
        std::uint32_t ComponentCount{0u};
        bool FitSucceeded{false};
        bool Converged{false};
        std::uint32_t Iterations{0u};
        std::uint32_t RegularizedCovariances{0u};
        double FinalLogLikelihood{0.0};
        double BayesianInformationCriterion{0.0};
        double NormalizedRmsFit{0.0};
        // Wall-clock duration of this candidate's FitEM call. Iterations
        // remains the deterministic work counter used to normalize profiles.
        double FitMilliseconds{0.0};
        bool FitToleranceSatisfied{false};
        bool Selected{false};
    };

    struct CurvatureSegmentationStageTimings
    {
        // Zero for Segment(), which consumes supplied curvatures. The
        // ComputeAndSegment() convenience path fills this field and includes
        // it in TotalMilliseconds.
        double CurvatureEstimationMilliseconds{0.0};
        double FaceAggregationAndNormalizationMilliseconds{0.0};
        double GmmFittingMilliseconds{0.0};
        double UnaryConstructionMilliseconds{0.0};
        double DualGraphConstructionMilliseconds{0.0};
        double SpatialOptimizationMilliseconds{0.0};
        double ConnectivityCleanupAndPublicationMilliseconds{0.0};
        double TotalMilliseconds{0.0};
    };

    struct CurvatureComponentSummary
    {
        std::uint32_t Component{0u};
        double Weight{0.0};
        double NormalizedK1Mean{0.0};
        double NormalizedK2Mean{0.0};
        double SignedK1Mean{0.0};
        double SignedK2Mean{0.0};
    };

    struct CurvatureSegmentationDiagnostics
    {
        SegmentationStatus Status{SegmentationStatus::EmptyMesh};
        std::size_t FaceSlotCount{0u};
        std::size_t LiveFaceCount{0u};
        std::size_t EdgeSlotCount{0u};
        std::size_t LiveEdgeCount{0u};
        std::size_t DualEdgeCount{0u};

        std::array<double, 3u> FeatureCenter{};
        std::array<double, 3u> FeatureScale{1.0, 1.0, 1.0};

        std::uint32_t RequestedComponentCount{0u};
        std::uint32_t SelectedComponentCount{0u};
        std::uint32_t ActiveComponentCount{0u};
        std::uint32_t ConnectedRegionCount{0u};
        std::size_t BoundaryEdgeCount{0u};

        bool AutomaticFitToleranceSatisfied{false};
        bool GmmConverged{false};
        std::uint32_t GmmIterations{0u};
        std::uint32_t GmmRegularizedCovariances{0u};
        double GmmFinalLogLikelihood{0.0};
        double NormalizedRmsFit{0.0};

        std::uint32_t SpatialIterations{0u};
        std::size_t SpatialLabelMoves{0u};
        std::size_t SmallRegionsMerged{0u};
        double InitialEnergy{0.0};
        double FinalEnergy{0.0};

        std::vector<ModelCandidateDiagnostics> Candidates{};
        std::vector<CurvatureComponentSummary> Components{};
        CurvatureSegmentationStageTimings Timings{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SegmentationStatus::Success;
        }
    };

    enum class FeatureEvidenceStatus : std::uint8_t
    {
        Success = 0,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        FeatureCountMismatch,
        NonTriangleFace,
        NonFinitePosition,
        DegenerateFace,
        NonFiniteFeature,
        InvalidCurvatureOrder,
        InvalidTopology,
        HardFeatureClassificationFailed,
        NonFiniteResponse,
        CurvatureEstimationFailed,
    };

    struct FeatureEvidenceStageTimings
    {
        // Zero for DetectFeatureEvidence(), which consumes supplied
        // curvatures. ComputeFeatureEvidence() fills this field.
        double CurvatureEstimationMilliseconds{0.0};
        double ValidationAndFaceSamplingMilliseconds{0.0};
        double HardFeatureClassificationMilliseconds{0.0};
        double MultiScaleResponseMilliseconds{0.0};
        double NonMaximumSuppressionMilliseconds{0.0};
        double HysteresisAndFragmentFilteringMilliseconds{0.0};
        double TotalMilliseconds{0.0};
    };

    struct FeatureEvidenceDiagnostics
    {
        FeatureEvidenceStatus Status{FeatureEvidenceStatus::EmptyMesh};
        std::size_t VertexSlotCount{0u};
        std::size_t LiveVertexCount{0u};
        std::size_t FaceSlotCount{0u};
        std::size_t LiveFaceCount{0u};
        std::size_t EdgeSlotCount{0u};
        std::size_t LiveEdgeCount{0u};
        std::size_t InteriorCandidateEdgeCount{0u};
        std::size_t HardFeatureEdgeCount{0u};
        std::size_t SourceBoundaryEdgeCount{0u};
        std::size_t NonMaximumSurvivorCount{0u};
        std::size_t StrongEdgeCount{0u};
        std::size_t RetainedWeakEdgeCount{0u};
        std::size_t WeakDisconnectedEdgeCount{0u};
        std::size_t ShortFragmentRejectedEdgeCount{0u};
        std::size_t RetainedSoftEdgeCount{0u};
        std::size_t EndpointVertexCount{0u};
        std::size_t CreaseVertexCount{0u};
        std::size_t JunctionVertexCount{0u};
        std::size_t BoundedSearchCount{0u};
        std::size_t SettledFaceVisitCount{0u};
        std::size_t MaximumNeighborhoodFaceCount{0u};
        double BoundingBoxDiagonal{0.0};
        double ResponseScale{0.10};
        double HysteresisLowThreshold{0.35};
        double HysteresisHighThreshold{0.65};
        double MaximumTurnDegrees{60.0};
        FeatureEvidenceStageTimings Timings{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == FeatureEvidenceStatus::Success;
        }
    };

    enum class CurvaturePatchStatus : std::uint8_t
    {
        Success = 0,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        FeatureCountMismatch,
        HardEvidenceCountMismatch,
        SoftEvidenceCountMismatch,
        InvalidHardEvidence,
        NonFiniteSoftEvidence,
        SoftEvidenceOutOfRange,
        InvalidSeedOverride,
        NonTriangleFace,
        NonFinitePosition,
        DegenerateFace,
        NonFiniteFeature,
        InvalidCurvatureOrder,
        InvalidTopology,
        GaussianMixtureFitFailed,
        PosteriorEvaluationFailed,
        GrowthFailed,
        NonFiniteEnergy,
        EnergyInvariantFailed,
        ConnectivityInvariantFailed,
        HardConstraintViolated,
    };

    struct CurvaturePatchStageTimings
    {
        double ValidationAndSamplingMilliseconds{0.0};
        double MixtureFittingMilliseconds{0.0};
        double PosteriorConstructionMilliseconds{0.0};
        double SeedSelectionMilliseconds{0.0};
        double SimultaneousGrowthMilliseconds{0.0};
        double RegionMergingMilliseconds{0.0};
        double BoundaryRefinementMilliseconds{0.0};
        double PublicationAndValidationMilliseconds{0.0};
        double TotalMilliseconds{0.0};
    };

    struct CurvaturePatchDiagnostics
    {
        CurvaturePatchStatus Status{CurvaturePatchStatus::EmptyMesh};
        std::size_t VertexSlotCount{0u};
        std::size_t LiveVertexCount{0u};
        std::size_t FaceSlotCount{0u};
        std::size_t LiveFaceCount{0u};
        std::size_t EdgeSlotCount{0u};
        std::size_t LiveEdgeCount{0u};
        std::size_t InteriorTransitionCount{0u};
        std::size_t HardBlockedTransitionCount{0u};
        std::size_t SoftPenalizedTransitionCount{0u};
        std::size_t DescriptorPenalizedTransitionCount{0u};
        std::size_t ProvisionalBoundaryEdgeCount{0u};
        std::size_t FinalBoundaryEdgeCount{0u};
        std::size_t HardBoundaryEdgeCount{0u};
        std::size_t SoftBoundaryEdgeCount{0u};
        std::size_t ClosureBoundaryEdgeCount{0u};
        std::size_t BoundaryEndpointCount{0u};
        std::size_t BoundaryJunctionCount{0u};

        double BoundingBoxDiagonal{0.0};
        double BaseRadiusWorld{0.0};
        double SeedSpacingCost{0.0};
        std::array<double, 3u> FeatureCenter{};
        std::array<double, 3u> FeatureScale{1.0, 1.0, 1.0};

        std::uint32_t SelectedComponentCount{0u};
        std::size_t SeedCount{0u};
        std::size_t ProvisionalRegionCount{0u};
        std::size_t AcceptedMergeCount{0u};
        std::size_t AcceptedRefinementMoveCount{0u};
        std::uint32_t RefinementSweeps{0u};
        std::size_t FinalRegionCount{0u};
        std::size_t FinalNegativeMergeCount{0u};
        double ProvisionalFrontLength{0.0};
        double InitialEnergy{0.0};
        double FinalEnergy{0.0};
        double MinimumFinalAdmissibleDelta{0.0};

        std::vector<ModelCandidateDiagnostics> Candidates{};
        std::vector<CurvatureComponentSummary> Components{};
        CurvaturePatchStageTimings Timings{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == CurvaturePatchStatus::Success;
        }
    };

enum class BoundaryPartitionStatus : std::uint8_t
{
    Success,
    EmptyMesh,
    UnsupportedSubmeshView,
    InvalidParameters,
    InvalidEvidence,
    NonFinitePosition,
    NonTriangleFace,
    DegenerateFace,
    InvalidTopology,
    InvalidCurvature,
    WorkLimit,
    InvariantFailure
};

struct BoundaryPartitionDiagnostics
{
    BoundaryPartitionStatus Status{BoundaryPartitionStatus::EmptyMesh};
    std::size_t FaceCount{}, TransitionCount{}, RegionCount{}, BoundaryCount{};
    std::size_t HardBoundaryCount{}, SoftBoundaryCount{}, ClosureBoundaryCount{};
    std::size_t AttenuationCurveCount{}, AttenuationHardEdgeCount{};
    std::uint64_t AttemptedMoves{}, AcceptedMoves{}, Contractions{}, FlowEdgeVisits{};
    std::uint32_t Sweeps{}, AreaMerges{}, UnmergeableSmallRegions{};
    bool Exact{};
    double BoundingBoxDiagonal{}, InitialEnergy{}, FinalEnergy{}, LowerBound{};
    double OptimizedEnergy{}, BoundaryEnergy{}, ModelEnergy{}, RegionCostEnergy{};
    double BoundaryLength{}, SupportedLength{}, ClosureLength{};
    double AssemblyMilliseconds{}, SolveMilliseconds{}, TotalMilliseconds{};
};
}
