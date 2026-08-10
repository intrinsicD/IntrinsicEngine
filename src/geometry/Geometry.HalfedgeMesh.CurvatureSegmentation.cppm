module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh.CurvatureSegmentation;

import Geometry.HalfedgeMesh;

export namespace Geometry::CurvatureSegmentation
{
    inline constexpr std::uint32_t kInvalidLabel =
        std::numeric_limits<std::uint32_t>::max();

    enum class ComponentSelectionMode : std::uint8_t
    {
        FixedCount = 0,
        Automatic,
    };

    enum class SegmentationStatus : std::uint8_t
    {
        Success = 0,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        CurvatureCountMismatch,
        NonTriangleFace,
        NonFinitePosition,
        DegenerateFace,
        NonFiniteCurvature,
        GaussianMixtureFitFailed,
        PosteriorEvaluationFailed,
    };

    struct CurvatureSegmentationParams
    {
        ComponentSelectionMode SelectionMode{
            ComponentSelectionMode::Automatic};

        // FixedCount fits exactly this many Gaussian components. Spatial
        // regularization may leave a statistically redundant component with
        // no winning faces; diagnostics distinguish fitted and active counts.
        std::uint32_t FixedComponentCount{6u};

        // Automatic mode evaluates every count in this inclusive range. A
        // candidate satisfying AutomaticFitTolerance is preferred; weighted
        // BIC resolves model complexity within the qualifying set (or across
        // all candidates when no count reaches the requested fit).
        std::uint32_t AutomaticMinComponents{1u};
        std::uint32_t AutomaticMaxComponents{12u};
        double AutomaticFitTolerance{0.35};
        double AutomaticComplexityWeight{1.0};

        std::uint32_t MaxEmIterations{100u};
        double EmRelativeTolerance{1.0e-6};
        double CovarianceFloor{1.0e-5};
        std::uint32_t Seed{42u};

        // Potts regularization on the face-dual graph. SpatialWeight is the
        // pairwise/data balance. FeatureSensitivity makes cuts progressively
        // cheaper across signed-curvature jumps and face-normal bends.
        double SpatialWeight{0.75};
        double FeatureSensitivity{4.0};
        std::uint32_t MaxSpatialIterations{24u};

        // Connected regions smaller than this face count are greedily merged
        // into the lowest-energy adjacent component. One disables cleanup.
        std::uint32_t MinimumRegionFaces{2u};
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

        double SignedK1Center{0.0};
        double SignedK2Center{0.0};
        double SignedK1Scale{1.0};
        double SignedK2Scale{1.0};

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

    struct CurvatureSegmentationResult
    {
        // Slot-aligned outputs. Deleted face slots retain kInvalidLabel;
        // deleted/non-boundary edge slots retain zero/transparent values.
        std::vector<std::uint32_t> FaceComponents{};
        std::vector<std::uint32_t> FaceRegions{};
        std::vector<std::uint8_t> EdgeBoundaries{};
        std::vector<glm::vec4> FaceRegionColors{};
        std::vector<glm::vec4> EdgeBoundaryColors{};
        CurvatureSegmentationDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostics.Succeeded();
        }
    };

    [[nodiscard]] const char* ToString(
        ComponentSelectionMode mode) noexcept;
    [[nodiscard]] const char* ToString(
        SegmentationStatus status) noexcept;

    // Segment a triangle mesh from slot-aligned signed principal curvatures.
    // maxPrincipal[i] is k1 and minPrincipal[i] is k2, with k1 >= k2 under the
    // caller's orientation convention. The kernel does not mutate the mesh.
    [[nodiscard]] CurvatureSegmentationResult Segment(
        const HalfedgeMesh::Mesh& mesh,
        std::span<const double> maxPrincipal,
        std::span<const double> minPrincipal,
        const CurvatureSegmentationParams& params = {});

    // Convenience path using Geometry.Curvature's signed per-vertex estimate.
    // Curvature properties are materialized only on the supplied mesh object;
    // runtime calls this on its detached snapshot and publishes selected output
    // properties explicitly after the solve.
    [[nodiscard]] CurvatureSegmentationResult ComputeAndSegment(
        HalfedgeMesh::Mesh& mesh,
        const CurvatureSegmentationParams& params = {});
}
