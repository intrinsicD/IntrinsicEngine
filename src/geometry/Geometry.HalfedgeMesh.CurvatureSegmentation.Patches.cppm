module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;

export import Geometry.HalfedgeMesh.CurvatureSegmentation;
export import Geometry.HalfedgeMesh.CurvatureSegmentation.Features;

import Geometry.HalfedgeMesh;

export namespace Geometry::CurvatureSegmentation
{
    inline constexpr std::uint32_t kInvalidPatchIndex =
        std::numeric_limits<std::uint32_t>::max();

    enum class CurvaturePatchStatus : std::uint8_t
    {
        Success = 0,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        CurvatureCountMismatch,
        HardEvidenceCountMismatch,
        SoftEvidenceCountMismatch,
        InvalidHardEvidence,
        NonFiniteSoftEvidence,
        SoftEvidenceOutOfRange,
        InvalidSeedOverride,
        NonTriangleFace,
        NonFinitePosition,
        DegenerateFace,
        NonFiniteCurvature,
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

    enum class PatchBoundaryRole : std::uint8_t
    {
        None = 0,
        HardFeature,
        SoftFeatureSupported,
        CurvatureClosure,
    };

    enum class PatchGrowthTransitionFlag : std::uint8_t
    {
        None = 0u,
        Interior = 1u << 0u,
        DescriptorPenalty = 1u << 1u,
        SoftFeaturePenalty = 1u << 2u,
        HardBlocked = 1u << 3u,
        ProvisionalFront = 1u << 4u,
    };

    struct CurvaturePatchParams
    {
        // Fixed/Automatic selection and deterministic EM controls are reused
        // from the established curvature-segmentation reference. Its Potts
        // and cleanup fields are intentionally ignored by this formulation.
        CurvatureSegmentationParams Mixture{};

        double BaseRadiusRatio{0.02};
        double SeedSpacingMultiplier{2.0};
        double CurvatureTransitionWeight{1.0};
        double SoftFeatureTransitionWeight{4.0};

        double PatchComplexityCost{0.0025};
        double BoundaryLengthWeight{0.01};
        double BoundaryTurnWeight{0.001};
        double BoundaryFeatureWeight{0.02};
        std::uint32_t MaximumRefinementSweeps{8u};
    };

    // Debug/test seam for perturbing only the oversegmentation. Mandatory
    // hard-adjacent and per-component seeds are always restored. Replacement
    // mode skips farthest-point sampling after these supplied seeds are added.
    struct PatchSeedOverrides
    {
        std::span<const std::uint32_t> FaceSlots{};
        bool ReplaceAutomaticSeeds{false};
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

    struct CurvaturePatchRegionDiagnostics
    {
        std::uint32_t Region{0u};
        std::size_t FaceCount{0u};
        double NormalizedArea{0.0};
        std::uint32_t WinningComponent{0u};
        double ModelCost{0.0};
        double RunnerUpMargin{0.0};
        double DescriptorMeanK1{0.0};
        double DescriptorMeanK2{0.0};
        double DescriptorCovarianceK1K1{0.0};
        double DescriptorCovarianceK1K2{0.0};
        double DescriptorCovarianceK2K2{0.0};
        double NormalizedResidual{0.0};
    };

    struct CurvaturePatchMergeDiagnostics
    {
        std::uint32_t RegionA{0u};
        std::uint32_t RegionB{0u};
        std::uint32_t ResultRegion{0u};
        double RegionalCostIncrease{0.0};
        double BoundaryCredit{0.0};
        double DeltaMerge{0.0};
        double EnergyAfter{0.0};
    };

    struct CurvaturePatchAdjacencyDiagnostics
    {
        std::uint32_t RegionA{0u};
        std::uint32_t RegionB{0u};
        bool HardBlocked{false};
        double RegionalCostIncrease{0.0};
        double BoundaryCredit{0.0};
        double DeltaMerge{0.0};
    };

    struct CurvaturePatchRefinementDiagnostics
    {
        std::uint32_t FaceSlot{0u};
        std::uint32_t RegionBefore{0u};
        std::uint32_t RegionAfter{0u};
        std::uint32_t Sweep{0u};
        double DeltaEnergy{0.0};
        double EnergyAfter{0.0};
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
        double SignedK1Center{0.0};
        double SignedK2Center{0.0};
        double SignedK1Scale{1.0};
        double SignedK2Scale{1.0};

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

    struct CurvaturePatchResult
    {
        // All arrays are aligned to source storage slots. Deleted face slots
        // retain kInvalidPatchIndex; non-applicable edge slots remain zero,
        // None, or +infinity only for an interior hard transition cost.
        std::vector<std::uint32_t> FaceComponents{};
        std::vector<std::uint32_t> ProvisionalFaceRegions{};
        std::vector<std::uint32_t> FaceRegions{};
        std::vector<double> FaceGrowthCosts{};
        std::vector<std::uint8_t> EdgeGrowthFlags{};
        std::vector<double> EdgeGrowthTransitionCosts{};
        std::vector<std::uint8_t> EdgeProvisionalBoundaries{};
        std::vector<std::uint8_t> EdgeBoundaries{};
        std::vector<PatchBoundaryRole> EdgeBoundaryRoles{};
        std::vector<double> EdgeBoundaryMergeDelta{};
        std::vector<glm::vec4> FaceRegionColors{};
        std::vector<glm::vec4> EdgeBoundaryColors{};

        std::vector<std::uint32_t> SeedFaceSlots{};
        std::vector<CurvaturePatchRegionDiagnostics> Regions{};
        std::vector<CurvaturePatchMergeDiagnostics> AcceptedMerges{};
        std::vector<CurvaturePatchAdjacencyDiagnostics> FinalAdjacencies{};
        std::vector<CurvaturePatchRefinementDiagnostics> RefinementMoves{};
        std::vector<double> AcceptedEnergyHistory{};
        CurvaturePatchDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostics.Succeeded();
        }
    };

    [[nodiscard]] const char* ToString(CurvaturePatchStatus status) noexcept;
    [[nodiscard]] const char* ToString(PatchBoundaryRole role) noexcept;

    // METHOD-039 diagnostic candidate: deterministic CPU work from supplied,
    // slot-aligned feature evidence. It fails the original frozen one-step
    // seed-location stability gate and is not cpu_reference_v2. Runtime may
    // expose it only by explicit diagnostic selection with requested/actual
    // reporting; METHOD-037 remains the production default. The mesh is
    // borrowed and never mutated. Feature detection can be tested independently
    // by passing FeatureEvidenceResult::View().
    [[nodiscard]] CurvaturePatchResult
    SegmentFeatureAlignedPatches(const HalfedgeMesh::Mesh& mesh,
                                 std::span<const double> maxPrincipal,
                                 std::span<const double> minPrincipal,
                                 FeatureEvidenceView evidence,
                                 const CurvaturePatchParams& params = {},
                                 PatchSeedOverrides seedOverrides = {});
} // namespace Geometry::CurvatureSegmentation
