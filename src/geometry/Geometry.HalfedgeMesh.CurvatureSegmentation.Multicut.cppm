// Partitions an embedded triangle surface using signed feature-boundary costs;
// exposes a CPU diagnostic candidate without changing geometry or production defaults.
module;
#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>
export module Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut;
export import Geometry.HalfedgeMesh.CurvatureSegmentation.Features;
import Geometry.HalfedgeMesh;
export namespace Geometry::CurvatureSegmentation
{
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
struct BoundaryPartitionParams
{
    double FeatureWeight{1.5};
    double FeatureExponent{1.0};
    std::uint32_t MaximumSweeps{8};
    std::uint64_t MaximumMoves{2000000};
    double RelativeEnergyTolerance{1e-12};
    std::uint64_t MaximumFlowEdgeVisits{20000000};
    std::uint32_t MaximumSplitTrials{4};
    std::uint32_t MaximumNonImprovingMoves{64};
    // Zero model/region costs retain the pure signed multicut objective.
    double ModelWeight{0.0};
    double RegionCost{0.0};
    double BoundaryScale{1.0};
    double CurvatureRadiusRatio{0.02};
    double HardFeatureExclusionRatio{0.0};
    double MinimumExclusionCurveLength{0.0};
    // Optional postprocessing in area / D^2; it may increase the cut objective.
    double MinimumRegionArea{0.0};
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
struct BoundaryPartitionResult
{
    // Successful arrays use source slots; deleted face slots are UINT32_MAX.
    // Failed results expose diagnostics only, never a partial partition.
    std::vector<std::uint32_t> FaceRegions;
    std::vector<std::uint8_t> EdgeBoundaries;
    std::vector<double> RegionAreas;
    BoundaryPartitionDiagnostics Diagnostics;
    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Diagnostics.Status == BoundaryPartitionStatus::Success;
    }
};
[[nodiscard]] const char *ToString(BoundaryPartitionStatus status) noexcept;
// H/F are borrowed, slot-aligned evidence. Hard constraints apply to interior
// edges; source boundary edges do not separate two surface faces.
[[nodiscard]] BoundaryPartitionResult
PartitionFeatureBoundaries(const HalfedgeMesh::Mesh &mesh, FeatureEvidenceView evidence,
                           const BoundaryPartitionParams &params = {},
                           std::span<const double> maxPrincipal = {},
                           std::span<const double> minPrincipal = {});
} // namespace Geometry::CurvatureSegmentation
