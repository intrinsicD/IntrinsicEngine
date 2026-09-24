// Exposes hard and multiscale soft feature evidence so curvature patch
// segmentation and diagnostics share one deterministic mesh-aligned contract.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module Geometry.HalfedgeMesh.Segmentation.Features;

export import Geometry.Segmentation.Diagnostics;

import Geometry.HalfedgeMesh;

export namespace Geometry::Segmentation
{
    inline constexpr std::size_t kFeatureEvidenceScaleCount = 3u;
    inline constexpr std::uint32_t kInvalidFeatureIndex =
        std::numeric_limits<std::uint32_t>::max();

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
} // namespace Geometry::Segmentation
