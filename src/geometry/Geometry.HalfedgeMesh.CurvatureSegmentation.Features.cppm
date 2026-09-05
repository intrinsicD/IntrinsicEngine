// Exposes hard and multiscale soft feature evidence so curvature patch
// segmentation and diagnostics share one deterministic mesh-aligned contract.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module Geometry.HalfedgeMesh.CurvatureSegmentation.Features;

import Geometry.HalfedgeMesh;

export namespace Geometry::CurvatureSegmentation
{
    inline constexpr std::size_t kFeatureEvidenceScaleCount = 3u;
    inline constexpr std::uint32_t kInvalidFeatureIndex =
        std::numeric_limits<std::uint32_t>::max();

    enum class FeatureEvidenceStatus : std::uint8_t
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
        InvalidCurvatureOrder,
        InvalidTopology,
        HardFeatureClassificationFailed,
        NonFiniteResponse,
        CurvatureEstimationFailed,
    };

    enum class SoftFeatureSignal : std::uint8_t
    {
        None = 0,
        Transition,
        Ridge,
        Valley,
    };

    enum class FeatureEdgeDecision : std::uint8_t
    {
        NotCandidate = 0,
        SourceBoundary,
        HardFeature,
        BelowLowThreshold,
        NonMaximumSuppressed,
        WeakDisconnected,
        ShortFragmentRejected,
        RetainedStrong,
        RetainedWeak,
    };

    enum class FeatureVertexIncidence : std::uint8_t
    {
        None = 0,
        Endpoint,
        Crease,
        Junction,
    };

    struct FeatureEvidenceParams
    {
        // Reuses Geometry.HalfedgeMesh.Features. Boundary marks remain
        // diagnostics/endpoints; only interior hard edges block a dual step.
        bool BoundaryIsHardFeature{false};
        double HardDihedralThresholdDegrees{45.0};

        // Physical base radius r_0 / D. The detector evaluates the frozen
        // half/base/double scale triplet around this value.
        double BaseRadiusRatio{0.02};
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

    struct FeatureEvidenceView
    {
        std::span<const std::uint8_t> HardEdgeMask{};
        std::span<const double> SoftEdgeConfidence{};
    };

    struct FeatureEvidenceResult
    {
        // Edge arrays are aligned to mesh edge storage slots. Deleted slots,
        // source-boundary edges, and hard edges have zero soft confidence.
        std::vector<std::uint8_t> HardEdgeMask{};
        std::vector<double> SoftEdgeConfidence{};

        // Per-scale arrays use edge-major indexing:
        // edgeSlot * kFeatureEvidenceScaleCount + scaleIndex.
        std::vector<double> TransitionResponse{};
        std::vector<double> RidgeResponse{};
        std::vector<double> ValleyResponse{};
        std::vector<double> CombinedResponse{};
        std::vector<double> EdgeRawConfidence{};
        std::vector<std::uint8_t> EdgePersistentScaleMask{};
        std::vector<std::uint8_t> EdgeNonMaximumSurvivor{};
        std::vector<SoftFeatureSignal> EdgeDominantSignal{};
        std::vector<FeatureEdgeDecision> EdgeDecision{};
        std::vector<std::uint32_t> HysteresisPredecessor{};

        // Combined hard/retained-soft feature-network incidence, aligned to
        // vertex storage slots.
        std::vector<std::size_t> VertexFeatureDegree{};
        std::vector<FeatureVertexIncidence> VertexFeatureIncidence{};
        std::array<double, kFeatureEvidenceScaleCount> ScaleRadiiWorld{};
        FeatureEvidenceDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostics.Succeeded();
        }

        [[nodiscard]] FeatureEvidenceView View() const noexcept
        {
            return FeatureEvidenceView{
                std::span<const std::uint8_t>{HardEdgeMask},
                std::span<const double>{SoftEdgeConfidence}};
        }
    };

    [[nodiscard]] const char *ToString(FeatureEvidenceStatus status) noexcept;
    [[nodiscard]] const char *ToString(SoftFeatureSignal signal) noexcept;
    [[nodiscard]] const char *ToString(FeatureEdgeDecision decision) noexcept;

    // Compute hard and soft feature evidence from supplied, slot-aligned signed
    // principal curvatures. maxPrincipal[i] is k1 and minPrincipal[i] is k2,
    // with k1 >= k2. The mesh is borrowed and never mutated.
    [[nodiscard]] FeatureEvidenceResult DetectFeatureEvidence(
        const HalfedgeMesh::Mesh &mesh, std::span<const double> maxPrincipal,
        std::span<const double> minPrincipal,
        const FeatureEvidenceParams &params = {});

    // Convenience path using Geometry.Curvature's signed per-vertex estimate.
    // Curvature properties are materialized on the supplied mesh; feature
    // evidence itself remains an owning result and is not auto-published.
    [[nodiscard]] FeatureEvidenceResult ComputeFeatureEvidence(
        HalfedgeMesh::Mesh &mesh, const FeatureEvidenceParams &params = {});
} // namespace Geometry::CurvatureSegmentation
