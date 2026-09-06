module;
#include "CurvatureBoundaryGraph.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>
#include <glm/glm.hpp>
module Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Geometry::CurvatureSegmentation
{
const char *ToString(BoundaryPartitionStatus s) noexcept
{
    switch (s)
    {
    case BoundaryPartitionStatus::Success:
        return "success";
    case BoundaryPartitionStatus::EmptyMesh:
        return "empty_mesh";
    case BoundaryPartitionStatus::UnsupportedSubmeshView:
        return "unsupported_submesh_view";
    case BoundaryPartitionStatus::InvalidParameters:
        return "invalid_parameters";
    case BoundaryPartitionStatus::InvalidEvidence:
        return "invalid_evidence";
    case BoundaryPartitionStatus::NonFinitePosition:
        return "non_finite_position";
    case BoundaryPartitionStatus::NonTriangleFace:
        return "non_triangle_face";
    case BoundaryPartitionStatus::DegenerateFace:
        return "degenerate_face";
    case BoundaryPartitionStatus::InvalidTopology:
        return "invalid_topology";
    case BoundaryPartitionStatus::InvalidCurvature:
        return "invalid_curvature";
    case BoundaryPartitionStatus::WorkLimit:
        return "work_limit";
    case BoundaryPartitionStatus::InvariantFailure:
        return "invariant_failure";
    }
    return "unknown";
}

BoundaryPartitionResult PartitionFeatureBoundaries(
    const HalfedgeMesh::Mesh &mesh, const FeatureEvidenceView evidence,
    const BoundaryPartitionParams &params, const std::span<const double> maxPrincipal,
    const std::span<const double> minPrincipal)
{
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const auto elapsed = [](auto start)
    { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
    BoundaryPartitionResult result;
    auto &d = result.Diagnostics;
    const auto finish = [&](BoundaryPartitionStatus s)
    {
        d.Status = s;
        d.TotalMilliseconds = elapsed(started);
        if (s != BoundaryPartitionStatus::Success)
        {
            result.FaceRegions.clear();
            result.EdgeBoundaries.clear();
            result.RegionAreas.clear();
        }
        return std::move(result);
    };
    constexpr auto invalid = std::numeric_limits<std::uint32_t>::max();
    if (mesh.FaceCount() == 0)
        return finish(BoundaryPartitionStatus::EmptyMesh);
    if (mesh.IsSubmeshView())
        return finish(BoundaryPartitionStatus::UnsupportedSubmeshView);
    if (!std::isfinite(params.FeatureWeight) || params.FeatureWeight < 0 ||
        !std::isfinite(params.FeatureExponent) || params.FeatureExponent < 1 ||
        params.FeatureExponent > 8 || !params.MaximumMoves || !params.MaximumSweeps ||
        !params.MaximumFlowEdgeVisits || !params.MaximumSplitTrials ||
        params.MaximumSplitTrials > 32 || !params.MaximumNonImprovingMoves ||
        !std::isfinite(params.RelativeEnergyTolerance) ||
        params.RelativeEnergyTolerance < 0 || params.RelativeEnergyTolerance > 1e-6 ||
        mesh.FaceCount() >= invalid / 2 || mesh.EdgeCount() >= invalid ||
        !std::isfinite(params.ModelWeight) || params.ModelWeight < 0 ||
        !std::isfinite(params.RegionCost) || params.RegionCost < 0 ||
        !std::isfinite(params.MinimumRegionArea) || params.MinimumRegionArea < 0 ||
        !std::isfinite(params.BoundaryScale) || params.BoundaryScale <= 0 ||
        !std::isfinite(params.CurvatureRadiusRatio) || params.CurvatureRadiusRatio <= 0 ||
        params.CurvatureRadiusRatio > 0.5 ||
        !std::isfinite(params.MinimumExclusionCurveLength) ||
        params.MinimumExclusionCurveLength < 0 ||
        !std::isfinite(params.HardFeatureExclusionRatio) ||
        params.HardFeatureExclusionRatio < 0 || params.HardFeatureExclusionRatio > 0.5)
        return finish(BoundaryPartitionStatus::InvalidParameters);
    if (params.ModelWeight > 0 && (maxPrincipal.size() != mesh.VerticesSize() ||
                                   minPrincipal.size() != mesh.VerticesSize()))
        return finish(BoundaryPartitionStatus::InvalidCurvature);
    if (evidence.HardEdgeMask.size() != mesh.EdgesSize() ||
        evidence.SoftEdgeConfidence.size() != mesh.EdgesSize())
        return finish(BoundaryPartitionStatus::InvalidEvidence);
    for (std::size_t e = 0; e < mesh.EdgesSize(); ++e)
        if (evidence.HardEdgeMask[e] > 1 ||
            !std::isfinite(evidence.SoftEdgeConfidence[e]) ||
            evidence.SoftEdgeConfidence[e] < 0 || evidence.SoftEdgeConfidence[e] > 1)
            return finish(BoundaryPartitionStatus::InvalidEvidence);
    glm::dvec3 lower{std::numeric_limits<double>::infinity()},
        upper{-std::numeric_limits<double>::infinity()};
    for (auto v : mesh.LiveVertices())
    {
        const glm::dvec3 p{mesh.Position(v)};
        if (params.ModelWeight > 0 && (!std::isfinite(maxPrincipal[v.Index]) ||
                                       !std::isfinite(minPrincipal[v.Index]) ||
                                       maxPrincipal[v.Index] < minPrincipal[v.Index]))
            return finish(BoundaryPartitionStatus::InvalidCurvature);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return finish(BoundaryPartitionStatus::NonFinitePosition);
        if (mesh.IsIsolated(v) || !mesh.IsManifold(v))
            return finish(BoundaryPartitionStatus::InvalidTopology);
        lower = glm::min(lower, p);
        upper = glm::max(upper, p);
    }
    const double diagonal = glm::length(upper - lower);
    if (!std::isfinite(diagonal) || !(diagonal > 0))
        return finish(BoundaryPartitionStatus::DegenerateFace);
    d.BoundingBoxDiagonal = diagonal;
    d.FaceCount = mesh.FaceCount();
    std::vector<std::uint32_t> faceToNode(mesh.FacesSize(), invalid), nodeToFace;
    std::vector<double> areas;
    std::vector<glm::dvec3> centers;
    std::vector<BoundaryDetail::Sample> samples;
    const double radius = params.CurvatureRadiusRatio * diagonal;
    for (auto f : mesh.LiveFaces())
    {
        if (mesh.Valence(f) != 3)
            return finish(BoundaryPartitionStatus::NonTriangleFace);
        std::array<glm::dvec3, 3> p;
        std::size_t i = 0;
        std::array<double, 2> descriptor{};
        for (auto v : mesh.VerticesAroundFace(f))
        {
            if (i == 3 || !v.IsValid() || !mesh.IsValid(v) || mesh.IsDeleted(v))
                return finish(BoundaryPartitionStatus::InvalidTopology);
            p[i++] = glm::dvec3(mesh.Position(v));
            if (params.ModelWeight > 0)
            {
                descriptor[0] += std::atan(radius * maxPrincipal[v.Index]) / 3;
                descriptor[1] += std::atan(radius * minPrincipal[v.Index]) / 3;
            }
        }
        if (i != 3)
            return finish(BoundaryPartitionStatus::NonTriangleFace);
        // Normalize before multiplying to keep area conditioning scale invariant.
        const double area = 0.5 * glm::length(glm::cross((p[1] - p[0]) / diagonal,
                                                         (p[2] - p[0]) / diagonal));
        if (!std::isfinite(area) || !(area > 0))
            return finish(BoundaryPartitionStatus::DegenerateFace);
        faceToNode[f.Index] = static_cast<std::uint32_t>(nodeToFace.size());
        nodeToFace.push_back(f.Index);
        areas.push_back(area);
        centers.push_back((p[0] + p[1] + p[2]) / 3.0);
        if (params.ModelWeight > 0)
            samples.push_back({area * params.ModelWeight, descriptor});
    }
    std::vector<BoundaryDetail::Edge> edges;
    std::vector<std::uint32_t> edgeSlots;
    std::vector<double> lengths;
    std::vector<std::pair<double, double>> halfDual;
    edges.reserve(mesh.EdgeCount());
    edgeSlots.reserve(mesh.EdgeCount());
    lengths.reserve(mesh.EdgeCount());
    for (auto e : mesh.LiveEdges())
    {
        auto h0 = mesh.Halfedge(e, 0), h1 = mesh.Halfedge(e, 1);
        if (!h0.IsValid() || !h1.IsValid() || !mesh.IsValid(h0) || !mesh.IsValid(h1) ||
            mesh.Edge(h0) != e || mesh.Edge(h1) != e)
            return finish(BoundaryPartitionStatus::InvalidTopology);
        auto from = mesh.FromVertex(h0), to = mesh.ToVertex(h0);
        if (!from.IsValid() || !to.IsValid() || !mesh.IsValid(from) ||
            !mesh.IsValid(to) || mesh.IsDeleted(from) || mesh.IsDeleted(to) || from == to)
            return finish(BoundaryPartitionStatus::InvalidTopology);
        const double length =
            glm::length(glm::dvec3(mesh.Position(from)) - glm::dvec3(mesh.Position(to))) /
            diagonal;
        if (!std::isfinite(length) || !(length > 0))
            return finish(BoundaryPartitionStatus::DegenerateFace);
        auto f0 = mesh.Face(h0), f1 = mesh.Face(h1);
        for (auto f : {f0, f1})
            if (f.IsValid() && (!mesh.IsValid(f) || mesh.IsDeleted(f)))
                return finish(BoundaryPartitionStatus::InvalidTopology);
        if (!f0.IsValid() && !f1.IsValid())
            return finish(BoundaryPartitionStatus::InvalidTopology);
        if (!f0.IsValid() || !f1.IsValid())
            continue;
        if (f0 == f1)
            return finish(BoundaryPartitionStatus::InvalidTopology);
        const double cost =
            params.BoundaryScale * length *
            (1 - params.FeatureWeight * std::pow(evidence.SoftEdgeConfidence[e.Index],
                                                 params.FeatureExponent));
        if (!std::isfinite(cost))
            return finish(BoundaryPartitionStatus::InvalidParameters);
        edges.push_back({faceToNode[f0.Index], faceToNode[f1.Index], cost,
                         evidence.HardEdgeMask[e.Index] != 0});
        edgeSlots.push_back(e.Index);
        lengths.push_back(length);
        const glm::dvec3 midpoint =
            (glm::dvec3(mesh.Position(from)) + glm::dvec3(mesh.Position(to))) / 2.0;
        halfDual.emplace_back(
            glm::length(centers[faceToNode[f0.Index]] - midpoint) / diagonal,
            glm::length(centers[faceToNode[f1.Index]] - midpoint) / diagonal);
    }
    if (params.HardFeatureExclusionRatio > 0)
    {
        using Entry = std::pair<double, std::uint32_t>;
        std::vector<std::uint32_t> curve(mesh.VerticesSize());
        std::iota(curve.begin(), curve.end(), 0);
        const auto root = [&](std::uint32_t vertex)
        {
            while (curve[vertex] != vertex)
            {
                curve[vertex] = curve[curve[vertex]];
                vertex = curve[vertex];
            }
            return vertex;
        };
        for (std::size_t i = 0; i < edges.size(); ++i)
            if (edges[i].Hard)
            {
                const auto h = mesh.Halfedge(EdgeHandle{edgeSlots[i]}, 0);
                const auto a = root(mesh.FromVertex(h).Index),
                           b = root(mesh.ToVertex(h).Index);
                curve[std::max(a, b)] = std::min(a, b);
            }
        std::vector<double> curveLength(mesh.VerticesSize());
        for (std::size_t i = 0; i < edges.size(); ++i)
            if (edges[i].Hard)
            {
                const auto h = mesh.Halfedge(EdgeHandle{edgeSlots[i]}, 0);
                curveLength[root(mesh.FromVertex(h).Index)] += lengths[i];
            }
        for (double length : curveLength)
            d.AttenuationCurveCount +=
                length > 0 && length >= params.MinimumExclusionCurveLength;
        std::vector<std::vector<Entry>> adjacent(nodeToFace.size());
        std::vector<double> distance(nodeToFace.size(),
                                     std::numeric_limits<double>::infinity());
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            const auto e = edges[i];
            const auto [a, b] = halfDual[i];
            adjacent[e.A].emplace_back(a + b, e.B);
            adjacent[e.B].emplace_back(a + b, e.A);
            const auto h = mesh.Halfedge(EdgeHandle{edgeSlots[i]}, 0);
            if (e.Hard && curveLength[root(mesh.FromVertex(h).Index)] >=
                              params.MinimumExclusionCurveLength)
            {
                ++d.AttenuationHardEdgeCount;
                for (auto [cost, v] : {Entry{a, e.A}, Entry{b, e.B}})
                    if (cost < distance[v])
                    {
                        distance[v] = cost;
                        queue.emplace(cost, v);
                    }
            }
        }
        while (!queue.empty())
        {
            auto [cost, v] = queue.top();
            queue.pop();
            if (cost != distance[v])
                continue;
            for (auto [step, to] : adjacent[v])
                if (cost + step < distance[to])
                {
                    distance[to] = cost + step;
                    queue.emplace(distance[to], to);
                }
        }
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            auto &e = edges[i];
            if (e.Hard)
                continue;
            const double dist = std::min(distance[e.A] + halfDual[i].first,
                                         distance[e.B] + halfDual[i].second);
            const double scaled = dist / params.HardFeatureExclusionRatio;
            const double confidence = evidence.SoftEdgeConfidence[edgeSlots[i]] *
                                      (-std::expm1(-scaled * scaled));
            e.Cost =
                params.BoundaryScale * lengths[i] *
                (1 - params.FeatureWeight * std::pow(confidence, params.FeatureExponent));
        }
    }
    d.TransitionCount = edges.size();
    d.AssemblyMilliseconds = elapsed(started);
    const auto solveStart = Clock::now();
    auto solved = BoundaryDetail::Solve(
        static_cast<std::uint32_t>(nodeToFace.size()), edges,
        {.MaximumSweeps = params.MaximumSweeps,
         .MaximumMoves = params.MaximumMoves,
         .RelativeTolerance = params.RelativeEnergyTolerance,
         .MaximumFlowEdgeVisits = params.MaximumFlowEdgeVisits,
         .MaximumSplitTrials = params.MaximumSplitTrials,
         .MaximumNonImprovingMoves = params.MaximumNonImprovingMoves,
         .RegionCost = params.RegionCost,
         .MinimumRegionArea = params.MinimumRegionArea},
        samples, areas);
    d.SolveMilliseconds = elapsed(solveStart);
    d.AttemptedMoves = solved.AttemptedMoves;
    d.FlowEdgeVisits = solved.FlowEdgeVisits;
    d.AcceptedMoves = solved.AcceptedMoves;
    d.Contractions = solved.Contractions;
    d.Sweeps = solved.Sweeps;
    d.Exact = solved.Exact;
    d.InitialEnergy = solved.InitialEnergy;
    d.FinalEnergy = solved.Energy;
    d.OptimizedEnergy = solved.OptimizedEnergy;
    d.AreaMerges = solved.AreaMerges;
    d.UnmergeableSmallRegions = solved.UnmergeableSmallRegions;
    d.LowerBound = solved.LowerBound;
    if (solved.State != BoundaryDetail::Status::Success)
        return finish(solved.State == BoundaryDetail::Status::WorkLimit
                          ? BoundaryPartitionStatus::WorkLimit
                          : BoundaryPartitionStatus::InvariantFailure);
    d.RegionCount = *std::max_element(solved.Labels.begin(), solved.Labels.end()) + 1;
    result.FaceRegions.assign(mesh.FacesSize(), invalid);
    result.EdgeBoundaries.assign(mesh.EdgesSize(), 0);
    result.RegionAreas.assign(d.RegionCount, 0);
    for (std::size_t v = 0; v < nodeToFace.size(); ++v)
    {
        result.FaceRegions[nodeToFace[v]] = solved.Labels[v];
        result.RegionAreas[solved.Labels[v]] += areas[v];
    }
    for (std::size_t i = 0; i < edges.size(); ++i)
    {
        const auto e = edges[i];
        if (solved.Labels[e.A] == solved.Labels[e.B])
            continue;
        result.EdgeBoundaries[edgeSlots[i]] = 1;
        ++d.BoundaryCount;
        d.BoundaryLength += lengths[i];
        d.BoundaryEnergy += e.Cost;
        if (e.Hard)
            ++d.HardBoundaryCount;
        else if (evidence.SoftEdgeConfidence[edgeSlots[i]] > 0)
            ++d.SoftBoundaryCount;
        else
            ++d.ClosureBoundaryCount;
        if (e.Hard || evidence.SoftEdgeConfidence[edgeSlots[i]] > 0)
            d.SupportedLength += lengths[i];
        else
            d.ClosureLength += lengths[i];
    }
    d.RegionCostEnergy = params.RegionCost * d.RegionCount;
    d.ModelEnergy = d.FinalEnergy - d.BoundaryEnergy - d.RegionCostEnergy;
    return finish(BoundaryPartitionStatus::Success);
}
} // namespace Geometry::CurvatureSegmentation
