// Private graph seam for the curvature-boundary CPU solver and its exact-oracle tests.
#pragma once
#include <cstdint>
#include <array>
#include <span>
#include <vector>
namespace Geometry::CurvatureSegmentation::BoundaryDetail
{
struct Edge
{
    std::uint32_t A{}, B{};
    double Cost{};
    bool Hard{};
};
struct Sample
{
    double Weight{};
    std::array<double, 2> Value{};
};
struct Options
{
    std::uint32_t MaximumSweeps{8};
    std::uint64_t MaximumMoves{2000000};
    double RelativeTolerance{1e-12};
    std::uint64_t MaximumFlowEdgeVisits{20000000};
    std::uint32_t MaximumSplitTrials{4};
    std::uint32_t MaximumNonImprovingMoves{64};
    double RegionCost{0.0};
    double MinimumRegionArea{0.0};
    // Zero forces the heuristic in oracle-comparison tests.
    std::uint32_t ExactNodeLimit{8};
};
enum class Status
{
    Success,
    InvalidInput,
    WorkLimit,
    InvariantFailure
};
struct Result
{
    Status State{Status::InvalidInput};
    std::vector<std::uint32_t> Labels;
    double InitialEnergy{}, OptimizedEnergy{}, Energy{}, LowerBound{};
    std::uint64_t AttemptedMoves{}, AcceptedMoves{}, Contractions{}, FlowEdgeVisits{};
    std::uint32_t Sweeps{}, AreaMerges{}, UnmergeableSmallRegions{};
    bool Exact{};
};
Result Solve(std::uint32_t nodes, std::span<const Edge> edges,
             const Options &options = {}, std::span<const Sample> samples = {},
             std::span<const double> nodeAreas = {});
} // namespace Geometry::CurvatureSegmentation::BoundaryDetail
