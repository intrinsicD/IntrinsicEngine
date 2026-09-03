module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;

import Geometry.GaussianMixture;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Geometry::CurvatureSegmentation
{
    namespace Gmm = Geometry::GaussianMixture;

    namespace
    {
        using ProfileClock = std::chrono::steady_clock;
        using RegionPair = std::pair<std::uint32_t, std::uint32_t>;
        constexpr double kTiny = 1.0e-12;
        constexpr double kPosteriorFloor = 1.0e-12;
        constexpr double kInfinity = std::numeric_limits<double>::infinity();

        struct PatchFaceSample
        {
            FaceHandle Face{};
            glm::dvec3 Centroid{0.0};
            glm::dvec2 SignedCurvature{0.0};
            glm::dvec2 Descriptor{0.0};
            glm::dvec2 NormalizedCurvature{0.0};
            double Area{0.0};
            double NormalizedArea{0.0};
        };

        struct PatchDualTransition
        {
            std::uint32_t FaceA{0u};
            std::uint32_t FaceB{0u};
            EdgeHandle Edge{};
            VertexHandle VertexA{};
            VertexHandle VertexB{};
            double NormalizedLength{0.0};
            double GrowthCost{0.0};
            double SoftConfidence{0.0};
            bool Hard{false};
        };

        struct FittedPatchCandidate
        {
            ModelCandidateDiagnostics Diagnostics{};
            Gmm::FitResult Fit{};
        };

        struct PatchRegion
        {
            bool Active{false};
            std::set<std::uint32_t> Faces{};
            std::vector<double> WeightedUnary{};
            double Area{0.0};
            glm::dvec2 DescriptorSum{0.0};
            glm::dmat2 DescriptorSecondMoment{0.0};
            glm::dvec2 NormalizedDescriptorSum{0.0};
            glm::dmat2 NormalizedDescriptorSecondMoment{0.0};
        };

        struct PatchPartition
        {
            std::vector<std::uint32_t> RegionByFace{};
            std::vector<PatchRegion> Regions{};
            std::map<RegionPair, std::vector<std::uint32_t>> Rag{};
        };

        struct MergeEvaluation
        {
            RegionPair Pair{};
            bool HardBlocked{false};
            double RegionalCostIncrease{0.0};
            double BoundaryCredit{0.0};
            double Delta{kInfinity};
        };

        [[nodiscard]] double
        ElapsedMilliseconds(const ProfileClock::time_point start) noexcept
        {
            return std::chrono::duration<double, std::milli>(
                       ProfileClock::now() - start)
                .count();
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool
        IsValidMixtureParams(const CurvatureSegmentationParams& params) noexcept
        {
            const bool validMode =
                params.SelectionMode == ComponentSelectionMode::FixedCount ||
                params.SelectionMode == ComponentSelectionMode::Automatic;
            return validMode && params.FixedComponentCount > 0u &&
                   params.AutomaticMinComponents > 0u &&
                   params.AutomaticMaxComponents >=
                       params.AutomaticMinComponents &&
                   std::isfinite(params.AutomaticFitTolerance) &&
                   params.AutomaticFitTolerance > 0.0 &&
                   std::isfinite(params.AutomaticComplexityWeight) &&
                   params.AutomaticComplexityWeight >= 0.0 &&
                   params.MaxEmIterations > 0u &&
                   std::isfinite(params.EmRelativeTolerance) &&
                   params.EmRelativeTolerance >= 0.0 &&
                   std::isfinite(params.CovarianceFloor) &&
                   params.CovarianceFloor > 0.0;
        }

        [[nodiscard]] bool
        IsValidParams(const CurvaturePatchParams& params) noexcept
        {
            return IsValidMixtureParams(params.Mixture) &&
                   std::isfinite(params.BaseRadiusRatio) &&
                   params.BaseRadiusRatio > 0.0 &&
                   params.BaseRadiusRatio <= 0.5 &&
                   std::isfinite(params.SeedSpacingMultiplier) &&
                   params.SeedSpacingMultiplier > 0.0 &&
                   std::isfinite(params.CurvatureTransitionWeight) &&
                   params.CurvatureTransitionWeight >= 0.0 &&
                   std::isfinite(params.SoftFeatureTransitionWeight) &&
                   params.SoftFeatureTransitionWeight >= 0.0 &&
                   std::isfinite(params.PatchComplexityCost) &&
                   params.PatchComplexityCost > 0.0 &&
                   std::isfinite(params.BoundaryLengthWeight) &&
                   params.BoundaryLengthWeight >= 0.0 &&
                   std::isfinite(params.BoundaryTurnWeight) &&
                   params.BoundaryTurnWeight >= 0.0 &&
                   std::isfinite(params.BoundaryFeatureWeight) &&
                   params.BoundaryFeatureWeight >= 0.0 &&
                   params.MaximumRefinementSweeps <= 8u;
        }

        [[nodiscard]] bool IsLiveFace(const HalfedgeMesh::Mesh& mesh,
                                      const FaceHandle face) noexcept
        {
            return face.IsValid() && mesh.IsValid(face) &&
                   !mesh.IsDeleted(face);
        }

        [[nodiscard]] double Median(std::vector<double> values)
        {
            if (values.empty())
                return 0.0;
            const std::size_t middle = values.size() / 2u;
            std::nth_element(values.begin(), values.begin() + middle,
                             values.end());
            const double upper = values[middle];
            if ((values.size() & 1u) != 0u)
                return upper;
            const double lower =
                *std::max_element(values.begin(), values.begin() + middle);
            return 0.5 * (lower + upper);
        }

        [[nodiscard]] double RobustScale(const std::vector<double>& values,
                                         const double center)
        {
            std::vector<double> deviations;
            deviations.reserve(values.size());
            for (const double value : values)
                deviations.push_back(std::abs(value - center));
            const double madScale = 1.4826 * Median(std::move(deviations));
            if (std::isfinite(madScale) && madScale > kTiny)
                return madScale;

            double squared = 0.0;
            for (const double value : values)
            {
                const double delta = value - center;
                squared += delta * delta;
            }
            const double rms =
                std::sqrt(squared / static_cast<double>(values.size()));
            return std::isfinite(rms) && rms > kTiny ? rms : 1.0;
        }

        [[nodiscard]] std::uint8_t
        Flag(const PatchGrowthTransitionFlag flag) noexcept
        {
            return static_cast<std::uint8_t>(flag);
        }

        void AddFlag(std::uint8_t& flags,
                     const PatchGrowthTransitionFlag flag) noexcept
        {
            flags = static_cast<std::uint8_t>(flags | Flag(flag));
        }

        [[nodiscard]] CurvaturePatchStatus BuildSamplesAndTransitions(
            const HalfedgeMesh::Mesh& mesh,
            const std::span<const double> maxPrincipal,
            const std::span<const double> minPrincipal,
            const FeatureEvidenceView evidence,
            const CurvaturePatchParams& params,
            std::vector<PatchFaceSample>& samples,
            std::vector<std::uint32_t>& faceSlotToSample,
            std::vector<PatchDualTransition>& transitions,
            std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            CurvaturePatchResult& result)
        {
            if (maxPrincipal.size() != mesh.VerticesSize() ||
                minPrincipal.size() != mesh.VerticesSize())
            {
                return CurvaturePatchStatus::CurvatureCountMismatch;
            }
            if (evidence.HardEdgeMask.size() != mesh.EdgesSize())
                return CurvaturePatchStatus::HardEvidenceCountMismatch;
            if (evidence.SoftEdgeConfidence.size() != mesh.EdgesSize())
                return CurvaturePatchStatus::SoftEvidenceCountMismatch;

            for (const std::uint8_t hard : evidence.HardEdgeMask)
            {
                if (hard > 1u)
                    return CurvaturePatchStatus::InvalidHardEvidence;
            }
            for (const double confidence : evidence.SoftEdgeConfidence)
            {
                if (!std::isfinite(confidence))
                    return CurvaturePatchStatus::NonFiniteSoftEvidence;
                if (confidence < 0.0 || confidence > 1.0)
                    return CurvaturePatchStatus::SoftEvidenceOutOfRange;
            }

            const double infinity = std::numeric_limits<double>::infinity();
            glm::dvec3 lower{infinity};
            glm::dvec3 upper{-infinity};
            for (const VertexHandle vertex : mesh.LiveVertices())
            {
                const glm::vec3 storedPosition = mesh.Position(vertex);
                if (!IsFinite(storedPosition))
                    return CurvaturePatchStatus::NonFinitePosition;
                const double k1 = maxPrincipal[vertex.Index];
                const double k2 = minPrincipal[vertex.Index];
                if (!std::isfinite(k1) || !std::isfinite(k2))
                    return CurvaturePatchStatus::NonFiniteCurvature;
                if (k1 < k2)
                    return CurvaturePatchStatus::InvalidCurvatureOrder;
                const glm::dvec3 position{storedPosition};
                lower = glm::min(lower, position);
                upper = glm::max(upper, position);
            }

            const double diagonal = glm::length(upper - lower);
            if (!std::isfinite(diagonal) || !(diagonal > 0.0))
                return CurvaturePatchStatus::DegenerateFace;
            const double diagonalSquared = diagonal * diagonal;
            const double baseRadius = params.BaseRadiusRatio * diagonal;
            result.Diagnostics.BoundingBoxDiagonal = diagonal;
            result.Diagnostics.BaseRadiusWorld = baseRadius;
            result.Diagnostics.SeedSpacingCost =
                params.SeedSpacingMultiplier * params.BaseRadiusRatio;

            faceSlotToSample.assign(mesh.FacesSize(), kInvalidPatchIndex);
            samples.clear();
            samples.reserve(mesh.FaceCount());
            std::vector<double> k1Values;
            std::vector<double> k2Values;
            k1Values.reserve(mesh.FaceCount());
            k2Values.reserve(mesh.FaceCount());
            for (const FaceHandle face : mesh.LiveFaces())
            {
                if (mesh.Valence(face) != 3u)
                    return CurvaturePatchStatus::NonTriangleFace;

                std::array<glm::dvec3, 3u> positions{};
                glm::dvec2 curvatureSum{0.0};
                std::size_t vertexCount = 0u;
                for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
                {
                    if (vertexCount >= positions.size() || !vertex.IsValid() ||
                        !mesh.IsValid(vertex) || mesh.IsDeleted(vertex))
                    {
                        return CurvaturePatchStatus::InvalidTopology;
                    }
                    positions[vertexCount++] =
                        glm::dvec3(mesh.Position(vertex));
                    curvatureSum += glm::dvec2{maxPrincipal[vertex.Index],
                                               minPrincipal[vertex.Index]};
                }
                if (vertexCount != positions.size())
                    return CurvaturePatchStatus::NonTriangleFace;

                const glm::dvec3 areaNormal =
                    glm::cross(positions[1u] - positions[0u],
                               positions[2u] - positions[0u]);
                const double doubleArea = glm::length(areaNormal);
                if (!std::isfinite(doubleArea) || !(doubleArea > 0.0))
                    return CurvaturePatchStatus::DegenerateFace;
                const glm::dvec2 curvature = curvatureSum / 3.0;
                if (!IsFinite(curvature))
                    return CurvaturePatchStatus::NonFiniteCurvature;

                const std::uint32_t sample =
                    static_cast<std::uint32_t>(samples.size());
                faceSlotToSample[face.Index] = sample;
                samples.push_back(PatchFaceSample{
                    .Face = face,
                    .Centroid =
                        (positions[0u] + positions[1u] + positions[2u]) / 3.0,
                    .SignedCurvature = curvature,
                    .Descriptor = baseRadius * curvature,
                    .Area = 0.5 * doubleArea,
                    .NormalizedArea = 0.5 * doubleArea / diagonalSquared,
                });
                k1Values.push_back(curvature.x);
                k2Values.push_back(curvature.y);
            }

            result.Diagnostics.SignedK1Center = Median(k1Values);
            result.Diagnostics.SignedK2Center = Median(k2Values);
            result.Diagnostics.SignedK1Scale =
                RobustScale(k1Values, result.Diagnostics.SignedK1Center);
            result.Diagnostics.SignedK2Scale =
                RobustScale(k2Values, result.Diagnostics.SignedK2Center);
            for (PatchFaceSample& sample : samples)
            {
                sample.NormalizedCurvature = glm::dvec2{
                    (sample.SignedCurvature.x -
                     result.Diagnostics.SignedK1Center) /
                        result.Diagnostics.SignedK1Scale,
                    (sample.SignedCurvature.y -
                     result.Diagnostics.SignedK2Center) /
                        result.Diagnostics.SignedK2Scale,
                };
                if (!IsFinite(sample.Descriptor) ||
                    !IsFinite(sample.NormalizedCurvature))
                {
                    return CurvaturePatchStatus::NonFiniteCurvature;
                }
            }

            transitions.clear();
            transitions.reserve(mesh.EdgeCount());
            incidentTransitions.assign(samples.size(), {});
            for (const EdgeHandle edge : mesh.LiveEdges())
            {
                const HalfedgeHandle h0 = mesh.Halfedge(edge, 0u);
                const HalfedgeHandle h1 = mesh.Halfedge(edge, 1u);
                if (!h0.IsValid() || !h1.IsValid() || !mesh.IsValid(h0) ||
                    !mesh.IsValid(h1) || mesh.Edge(h0) != edge ||
                    mesh.Edge(h1) != edge)
                {
                    return CurvaturePatchStatus::InvalidTopology;
                }
                const VertexHandle from = mesh.FromVertex(h0);
                const VertexHandle to = mesh.ToVertex(h0);
                if (!from.IsValid() || !to.IsValid() || !mesh.IsValid(from) ||
                    !mesh.IsValid(to) || mesh.IsDeleted(from) ||
                    mesh.IsDeleted(to) || from == to)
                {
                    return CurvaturePatchStatus::InvalidTopology;
                }
                const FaceHandle face0 = mesh.Face(h0);
                const FaceHandle face1 = mesh.Face(h1);
                const bool live0 = IsLiveFace(mesh, face0);
                const bool live1 = IsLiveFace(mesh, face1);
                const bool cleanBoundary =
                    (live0 && !face1.IsValid()) || (live1 && !face0.IsValid());
                if (!(live0 && live1) && !cleanBoundary)
                    return CurvaturePatchStatus::InvalidTopology;
                if (live0 &&
                    faceSlotToSample[face0.Index] == kInvalidPatchIndex)
                {
                    return CurvaturePatchStatus::InvalidTopology;
                }
                if (live1 &&
                    faceSlotToSample[face1.Index] == kInvalidPatchIndex)
                {
                    return CurvaturePatchStatus::InvalidTopology;
                }
                if (!live0 || !live1)
                    continue;

                const std::uint32_t sample0 = faceSlotToSample[face0.Index];
                const std::uint32_t sample1 = faceSlotToSample[face1.Index];
                if (sample0 == sample1)
                    return CurvaturePatchStatus::InvalidTopology;
                const std::uint32_t a = std::min(sample0, sample1);
                const std::uint32_t b = std::max(sample0, sample1);
                const glm::dvec3 fromPosition{mesh.Position(from)};
                const glm::dvec3 toPosition{mesh.Position(to)};
                const double edgeLength =
                    glm::length(toPosition - fromPosition);
                if (!std::isfinite(edgeLength) || !(edgeLength > 0.0))
                    return CurvaturePatchStatus::DegenerateFace;
                const glm::dvec3 midpoint = 0.5 * (fromPosition + toPosition);
                const double dualLength =
                    glm::length(samples[a].Centroid - midpoint) +
                    glm::length(samples[b].Centroid - midpoint);
                const double descriptorJump =
                    glm::length(samples[a].Descriptor - samples[b].Descriptor);
                const double confidence =
                    evidence.SoftEdgeConfidence[edge.Index];
                const bool hard = evidence.HardEdgeMask[edge.Index] != 0u;
                const double growthCost =
                    hard
                        ? kInfinity
                        : (dualLength / diagonal) *
                              (1.0 +
                               params.CurvatureTransitionWeight *
                                   descriptorJump +
                               params.SoftFeatureTransitionWeight * confidence);
                if ((!hard &&
                     (!std::isfinite(growthCost) || growthCost < 0.0)) ||
                    !std::isfinite(edgeLength / diagonal))
                {
                    return CurvaturePatchStatus::NonFiniteEnergy;
                }

                const std::uint32_t transition =
                    static_cast<std::uint32_t>(transitions.size());
                transitions.push_back(PatchDualTransition{
                    .FaceA = a,
                    .FaceB = b,
                    .Edge = edge,
                    .VertexA = from,
                    .VertexB = to,
                    .NormalizedLength = edgeLength / diagonal,
                    .GrowthCost = growthCost,
                    .SoftConfidence = confidence,
                    .Hard = hard,
                });
                incidentTransitions[a].push_back(transition);
                incidentTransitions[b].push_back(transition);

                std::uint8_t& flags = result.EdgeGrowthFlags[edge.Index];
                AddFlag(flags, PatchGrowthTransitionFlag::Interior);
                if (hard)
                {
                    AddFlag(flags, PatchGrowthTransitionFlag::HardBlocked);
                    ++result.Diagnostics.HardBlockedTransitionCount;
                }
                if (descriptorJump > 0.0)
                {
                    AddFlag(flags,
                            PatchGrowthTransitionFlag::DescriptorPenalty);
                    ++result.Diagnostics.DescriptorPenalizedTransitionCount;
                }
                if (confidence > 0.0)
                {
                    AddFlag(flags,
                            PatchGrowthTransitionFlag::SoftFeaturePenalty);
                    ++result.Diagnostics.SoftPenalizedTransitionCount;
                }
                result.EdgeGrowthTransitionCosts[edge.Index] = growthCost;
            }

            for (const VertexHandle vertex : mesh.LiveVertices())
            {
                if (mesh.IsIsolated(vertex) || !mesh.IsManifold(vertex))
                    return CurvaturePatchStatus::InvalidTopology;
            }
            result.Diagnostics.InteriorTransitionCount = transitions.size();
            return CurvaturePatchStatus::Success;
        }

        [[nodiscard]] FittedPatchCandidate
        FitCandidate(const std::span<const glm::vec3> points,
                     const std::uint32_t componentCount,
                     const CurvatureSegmentationParams& params)
        {
            FittedPatchCandidate candidate{};
            candidate.Diagnostics.ComponentCount = componentCount;
            Gmm::FitParams fitParams{};
            fitParams.MaxIterations = params.MaxEmIterations;
            fitParams.RelativeTolerance = params.EmRelativeTolerance;
            fitParams.CovarianceFloor = params.CovarianceFloor;
            fitParams.Seed = params.Seed;
            fitParams.Acceleration = Gmm::AccelerationPolicy::None;
            const ProfileClock::time_point start = ProfileClock::now();
            candidate.Fit = Gmm::FitEM(points, componentCount, fitParams);
            candidate.Diagnostics.FitMilliseconds = ElapsedMilliseconds(start);
            const Gmm::FitDiagnostics& fit = candidate.Fit.Diagnostics;
            candidate.Diagnostics.FitSucceeded = fit.Succeeded();
            candidate.Diagnostics.Converged = fit.Converged;
            candidate.Diagnostics.Iterations = fit.Iterations;
            candidate.Diagnostics.RegularizedCovariances =
                fit.RegularizedCovariances;
            candidate.Diagnostics.FinalLogLikelihood = fit.FinalLogLikelihood;
            if (!fit.Succeeded())
                return candidate;

            double squaredResidual = 0.0;
            for (const glm::vec3 point : points)
            {
                const auto responsibilities = Gmm::Responsibilities(
                    candidate.Fit.Mixture, glm::dvec3(point));
                if (!responsibilities || responsibilities->empty())
                {
                    candidate.Diagnostics.FitSucceeded = false;
                    return candidate;
                }
                const auto best = std::max_element(responsibilities->begin(),
                                                   responsibilities->end());
                const std::size_t component = static_cast<std::size_t>(
                    std::distance(responsibilities->begin(), best));
                const glm::dvec3 delta =
                    glm::dvec3(point) -
                    candidate.Fit.Mixture.Components[component].Mean;
                squaredResidual += delta.x * delta.x + delta.y * delta.y;
            }
            candidate.Diagnostics.NormalizedRmsFit =
                std::sqrt(squaredResidual / static_cast<double>(points.size()));
            candidate.Diagnostics.FitToleranceSatisfied =
                candidate.Diagnostics.NormalizedRmsFit <=
                params.AutomaticFitTolerance;
            const double parameterCount =
                static_cast<double>(6u * componentCount - 1u);
            candidate.Diagnostics.BayesianInformationCriterion =
                -2.0 * fit.FinalLogLikelihood +
                params.AutomaticComplexityWeight * parameterCount *
                    std::log(static_cast<double>(points.size()));
            return candidate;
        }

        [[nodiscard]] std::optional<std::size_t>
        SelectCandidate(const std::vector<FittedPatchCandidate>& candidates,
                        const ComponentSelectionMode mode)
        {
            bool anyWithinTolerance = false;
            if (mode == ComponentSelectionMode::Automatic)
            {
                for (const FittedPatchCandidate& candidate : candidates)
                {
                    anyWithinTolerance |=
                        candidate.Diagnostics.FitSucceeded &&
                        candidate.Diagnostics.FitToleranceSatisfied;
                }
            }

            std::optional<std::size_t> selected{};
            for (std::size_t i = 0u; i < candidates.size(); ++i)
            {
                const ModelCandidateDiagnostics& candidate =
                    candidates[i].Diagnostics;
                if (!candidate.FitSucceeded)
                    continue;
                if (mode == ComponentSelectionMode::Automatic &&
                    anyWithinTolerance && !candidate.FitToleranceSatisfied)
                {
                    continue;
                }
                if (!selected)
                {
                    selected = i;
                    continue;
                }
                const ModelCandidateDiagnostics& current =
                    candidates[*selected].Diagnostics;
                if (candidate.BayesianInformationCriterion <
                        current.BayesianInformationCriterion - kTiny ||
                    (std::abs(candidate.BayesianInformationCriterion -
                              current.BayesianInformationCriterion) <= kTiny &&
                     candidate.ComponentCount < current.ComponentCount))
                {
                    selected = i;
                }
            }
            return selected;
        }

        [[nodiscard]] CurvaturePatchStatus
        FitMixtureAndPosteriors(const std::vector<PatchFaceSample>& samples,
                                const CurvaturePatchParams& params,
                                std::vector<double>& unary,
                                std::vector<std::uint32_t>& faceComponents,
                                Gmm::Model& selectedMixture,
                                CurvaturePatchResult& result,
                                double& fittingMilliseconds,
                                double& posteriorMilliseconds)
        {
            std::vector<glm::vec3> points;
            points.reserve(samples.size());
            for (const PatchFaceSample& sample : samples)
            {
                const glm::vec3 point{
                    static_cast<float>(sample.NormalizedCurvature.x),
                    static_cast<float>(sample.NormalizedCurvature.y), 0.0f};
                if (!IsFinite(point))
                    return CurvaturePatchStatus::NonFiniteCurvature;
                points.push_back(point);
            }

            const std::uint32_t faceCount =
                static_cast<std::uint32_t>(samples.size());
            std::uint32_t first = params.Mixture.FixedComponentCount;
            std::uint32_t last = params.Mixture.FixedComponentCount;
            if (params.Mixture.SelectionMode ==
                ComponentSelectionMode::Automatic)
            {
                first = params.Mixture.AutomaticMinComponents;
                last =
                    std::min(params.Mixture.AutomaticMaxComponents, faceCount);
            }
            if (first == 0u || first > last || first > faceCount ||
                last > faceCount)
            {
                return CurvaturePatchStatus::InvalidParameters;
            }

            std::vector<FittedPatchCandidate> candidates;
            candidates.reserve(last - first + 1u);
            const ProfileClock::time_point fittingStart = ProfileClock::now();
            for (std::uint32_t count = first; count <= last; ++count)
            {
                candidates.push_back(FitCandidate(
                    std::span<const glm::vec3>{points}, count, params.Mixture));
            }
            fittingMilliseconds = ElapsedMilliseconds(fittingStart);
            const std::optional<std::size_t> selected =
                SelectCandidate(candidates, params.Mixture.SelectionMode);
            if (!selected)
            {
                for (const FittedPatchCandidate& candidate : candidates)
                    result.Diagnostics.Candidates.push_back(
                        candidate.Diagnostics);
                return CurvaturePatchStatus::GaussianMixtureFitFailed;
            }
            candidates[*selected].Diagnostics.Selected = true;
            for (const FittedPatchCandidate& candidate : candidates)
                result.Diagnostics.Candidates.push_back(candidate.Diagnostics);
            const FittedPatchCandidate& chosen = candidates[*selected];
            selectedMixture = chosen.Fit.Mixture;
            result.Diagnostics.SelectedComponentCount =
                chosen.Diagnostics.ComponentCount;

            for (std::uint32_t component = 0u;
                 component < selectedMixture.Components.size(); ++component)
            {
                const Gmm::MultivariateGaussian& gaussian =
                    selectedMixture.Components[component];
                result.Diagnostics.Components.push_back(
                    CurvatureComponentSummary{
                        .Component = component,
                        .Weight = selectedMixture.Weights[component],
                        .NormalizedK1Mean = gaussian.Mean.x,
                        .NormalizedK2Mean = gaussian.Mean.y,
                        .SignedK1Mean =
                            result.Diagnostics.SignedK1Center +
                            result.Diagnostics.SignedK1Scale * gaussian.Mean.x,
                        .SignedK2Mean =
                            result.Diagnostics.SignedK2Center +
                            result.Diagnostics.SignedK2Scale * gaussian.Mean.y,
                    });
            }

            const std::size_t componentCount =
                selectedMixture.Components.size();
            unary.assign(samples.size() * componentCount, 0.0);
            faceComponents.assign(samples.size(), 0u);
            const ProfileClock::time_point posteriorStart = ProfileClock::now();
            for (std::size_t face = 0u; face < samples.size(); ++face)
            {
                const auto responsibilities = Gmm::Responsibilities(
                    selectedMixture, glm::dvec3(points[face]));
                if (!responsibilities ||
                    responsibilities->size() != componentCount)
                {
                    return CurvaturePatchStatus::PosteriorEvaluationFailed;
                }
                double minimum = kInfinity;
                for (std::size_t component = 0u; component < componentCount;
                     ++component)
                {
                    const double posterior = (*responsibilities)[component];
                    if (!std::isfinite(posterior) || posterior < 0.0)
                        return CurvaturePatchStatus::PosteriorEvaluationFailed;
                    const double cost =
                        -std::log(std::max(posterior, kPosteriorFloor));
                    if (!std::isfinite(cost))
                        return CurvaturePatchStatus::PosteriorEvaluationFailed;
                    unary[face * componentCount + component] = cost;
                    if (cost < minimum)
                    {
                        minimum = cost;
                        faceComponents[face] =
                            static_cast<std::uint32_t>(component);
                    }
                }
            }
            posteriorMilliseconds = ElapsedMilliseconds(posteriorStart);
            return CurvaturePatchStatus::Success;
        }

        [[nodiscard]] std::uint32_t
        OtherFace(const PatchDualTransition& transition,
                  const std::uint32_t face) noexcept
        {
            return transition.FaceA == face ? transition.FaceB
                                            : transition.FaceA;
        }

        [[nodiscard]] std::vector<std::vector<std::uint32_t>>
        HardBarrierComponents(
            const std::vector<PatchFaceSample>& samples,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incident)
        {
            std::vector<std::vector<std::uint32_t>> components;
            std::vector<std::uint8_t> visited(samples.size(), 0u);
            for (std::uint32_t start = 0u; start < samples.size(); ++start)
            {
                if (visited[start] != 0u)
                    continue;
                std::vector<std::uint32_t> component;
                std::queue<std::uint32_t> queue;
                queue.push(start);
                visited[start] = 1u;
                while (!queue.empty())
                {
                    const std::uint32_t face = queue.front();
                    queue.pop();
                    component.push_back(face);
                    for (const std::uint32_t transitionIndex : incident[face])
                    {
                        const PatchDualTransition& transition =
                            transitions[transitionIndex];
                        if (transition.Hard)
                            continue;
                        const std::uint32_t neighbor =
                            OtherFace(transition, face);
                        if (visited[neighbor] == 0u)
                        {
                            visited[neighbor] = 1u;
                            queue.push(neighbor);
                        }
                    }
                }
                std::sort(
                    component.begin(), component.end(),
                    [&](const std::uint32_t a, const std::uint32_t b)
                    { return samples[a].Face.Index < samples[b].Face.Index; });
                components.push_back(std::move(component));
            }
            std::sort(components.begin(), components.end(),
                      [&](const auto& a, const auto& b)
                      {
                          return samples[a.front()].Face.Index <
                                 samples[b.front()].Face.Index;
                      });
            return components;
        }

        void UpdateDistanceToSeeds(
            const std::vector<PatchFaceSample>& samples,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incident,
            const std::span<const std::uint32_t> newSeeds,
            std::vector<double>& distance)
        {
            struct Entry
            {
                double Distance{0.0};
                std::uint32_t FaceSlot{0u};
                std::uint32_t Face{0u};
            };
            const auto later = [](const Entry& a, const Entry& b)
            {
                return std::tie(a.Distance, a.FaceSlot, a.Face) >
                       std::tie(b.Distance, b.FaceSlot, b.Face);
            };
            std::priority_queue<Entry, std::vector<Entry>, decltype(later)>
                queue(later);
            for (const std::uint32_t face : newSeeds)
            {
                distance[face] = 0.0;
                queue.push(Entry{
                    .Distance = 0.0,
                    .FaceSlot = samples[face].Face.Index,
                    .Face = face,
                });
            }
            while (!queue.empty())
            {
                const Entry current = queue.top();
                queue.pop();
                if (current.Distance != distance[current.Face])
                    continue;
                for (const std::uint32_t transitionIndex :
                     incident[current.Face])
                {
                    const PatchDualTransition& transition =
                        transitions[transitionIndex];
                    if (transition.Hard)
                        continue;
                    const std::uint32_t neighbor =
                        OtherFace(transition, current.Face);
                    const double candidate =
                        current.Distance + transition.GrowthCost;
                    if (!(candidate < distance[neighbor]))
                        continue;
                    distance[neighbor] = candidate;
                    queue.push(Entry{
                        .Distance = candidate,
                        .FaceSlot = samples[neighbor].Face.Index,
                        .Face = neighbor,
                    });
                }
            }
        }

        [[nodiscard]] CurvaturePatchStatus
        SelectSeeds(const std::vector<PatchFaceSample>& samples,
                    const std::vector<std::uint32_t>& faceSlotToSample,
                    const std::vector<PatchDualTransition>& transitions,
                    const std::vector<std::vector<std::uint32_t>>& incident,
                    const PatchSeedOverrides overrides,
                    const double spacing,
                    std::vector<std::uint32_t>& seeds)
        {
            std::vector<std::uint8_t> isSeed(samples.size(), 0u);
            for (const PatchDualTransition& transition : transitions)
            {
                if (!transition.Hard)
                    continue;
                isSeed[transition.FaceA] = 1u;
                isSeed[transition.FaceB] = 1u;
            }

            for (const std::uint32_t faceSlot : overrides.FaceSlots)
            {
                if (faceSlot >= faceSlotToSample.size() ||
                    faceSlotToSample[faceSlot] == kInvalidPatchIndex)
                {
                    return CurvaturePatchStatus::InvalidSeedOverride;
                }
                isSeed[faceSlotToSample[faceSlot]] = 1u;
            }

            const auto components =
                HardBarrierComponents(samples, transitions, incident);
            for (const std::vector<std::uint32_t>& component : components)
            {
                const bool hasSeed =
                    std::ranges::any_of(component, [&](const std::uint32_t face)
                                        { return isSeed[face] != 0u; });
                if (!hasSeed)
                    isSeed[component.front()] = 1u;
            }

            if (!overrides.ReplaceAutomaticSeeds)
            {
                std::vector<std::uint32_t> initialSeeds;
                initialSeeds.reserve(samples.size());
                for (std::uint32_t face = 0u; face < samples.size(); ++face)
                {
                    if (isSeed[face] != 0u)
                        initialSeeds.push_back(face);
                }
                std::vector<double> distance(samples.size(), kInfinity);
                UpdateDistanceToSeeds(samples, transitions, incident,
                                      initialSeeds, distance);
                for (;;)
                {
                    std::optional<std::uint32_t> farthest{};
                    double farthestDistance = -1.0;
                    for (std::uint32_t face = 0u; face < samples.size(); ++face)
                    {
                        if (isSeed[face] != 0u)
                            continue;
                        const double candidate = distance[face];
                        if (!std::isfinite(candidate))
                            return CurvaturePatchStatus::GrowthFailed;
                        if (candidate > farthestDistance ||
                            (candidate == farthestDistance && farthest &&
                             samples[face].Face.Index <
                                 samples[*farthest].Face.Index))
                        {
                            farthest = face;
                            farthestDistance = candidate;
                        }
                    }
                    if (!farthest || !(farthestDistance > spacing))
                        break;
                    isSeed[*farthest] = 1u;
                    const std::array<std::uint32_t, 1u> newSeed{*farthest};
                    UpdateDistanceToSeeds(samples, transitions, incident,
                                          newSeed, distance);
                }
            }

            seeds.clear();
            for (std::uint32_t face = 0u; face < samples.size(); ++face)
            {
                if (isSeed[face] != 0u)
                    seeds.push_back(face);
            }
            std::sort(
                seeds.begin(), seeds.end(),
                [&](const std::uint32_t a, const std::uint32_t b)
                { return samples[a].Face.Index < samples[b].Face.Index; });
            return seeds.empty() ? CurvaturePatchStatus::GrowthFailed
                                 : CurvaturePatchStatus::Success;
        }

        [[nodiscard]] CurvaturePatchStatus
        GrowSeedRegions(const std::vector<PatchFaceSample>& samples,
                        const std::vector<PatchDualTransition>& transitions,
                        const std::vector<std::vector<std::uint32_t>>& incident,
                        const std::vector<std::uint32_t>& seeds,
                        std::vector<std::uint32_t>& regionByFace,
                        std::vector<double>& growthCost)
        {
            struct Entry
            {
                double Cost{0.0};
                std::uint32_t SeedFaceSlot{0u};
                std::uint32_t FaceSlot{0u};
                std::uint32_t Seed{0u};
                std::uint32_t Face{0u};
            };
            const auto later = [](const Entry& a, const Entry& b)
            {
                return std::tie(a.Cost, a.SeedFaceSlot, a.FaceSlot, a.Seed,
                                a.Face) > std::tie(b.Cost, b.SeedFaceSlot,
                                                   b.FaceSlot, b.Seed, b.Face);
            };
            std::priority_queue<Entry, std::vector<Entry>, decltype(later)>
                queue(later);
            regionByFace.assign(samples.size(), kInvalidPatchIndex);
            growthCost.assign(samples.size(), kInfinity);
            for (std::uint32_t seed = 0u; seed < seeds.size(); ++seed)
            {
                const std::uint32_t face = seeds[seed];
                regionByFace[face] = seed;
                growthCost[face] = 0.0;
                queue.push(Entry{
                    .Cost = 0.0,
                    .SeedFaceSlot = samples[face].Face.Index,
                    .FaceSlot = samples[face].Face.Index,
                    .Seed = seed,
                    .Face = face,
                });
            }

            while (!queue.empty())
            {
                const Entry current = queue.top();
                queue.pop();
                if (current.Cost != growthCost[current.Face] ||
                    current.Seed != regionByFace[current.Face])
                {
                    continue;
                }
                for (const std::uint32_t transitionIndex :
                     incident[current.Face])
                {
                    const PatchDualTransition& transition =
                        transitions[transitionIndex];
                    if (transition.Hard)
                        continue;
                    const std::uint32_t neighbor =
                        OtherFace(transition, current.Face);
                    const double candidate =
                        current.Cost + transition.GrowthCost;
                    const std::uint32_t oldSeed = regionByFace[neighbor];
                    const bool better =
                        candidate < growthCost[neighbor] ||
                        (candidate == growthCost[neighbor] &&
                         (oldSeed == kInvalidPatchIndex ||
                          samples[seeds[current.Seed]].Face.Index <
                              samples[seeds[oldSeed]].Face.Index));
                    if (!better)
                        continue;
                    growthCost[neighbor] = candidate;
                    regionByFace[neighbor] = current.Seed;
                    queue.push(Entry{
                        .Cost = candidate,
                        .SeedFaceSlot = samples[seeds[current.Seed]].Face.Index,
                        .FaceSlot = samples[neighbor].Face.Index,
                        .Seed = current.Seed,
                        .Face = neighbor,
                    });
                }
            }

            for (std::uint32_t face = 0u; face < samples.size(); ++face)
            {
                if (regionByFace[face] == kInvalidPatchIndex ||
                    !std::isfinite(growthCost[face]))
                {
                    return CurvaturePatchStatus::GrowthFailed;
                }
            }
            for (const PatchDualTransition& transition : transitions)
            {
                if (transition.Hard && regionByFace[transition.FaceA] ==
                                           regionByFace[transition.FaceB])
                {
                    return CurvaturePatchStatus::HardConstraintViolated;
                }
            }
            return CurvaturePatchStatus::Success;
        }

        [[nodiscard]] RegionPair OrderedPair(const std::uint32_t a,
                                             const std::uint32_t b) noexcept
        {
            return {std::min(a, b), std::max(a, b)};
        }

        void AccumulateFace(PatchRegion& region,
                            const PatchFaceSample& sample,
                            const std::span<const double> faceUnary,
                            const double sign)
        {
            region.Area += sign * sample.NormalizedArea;
            region.DescriptorSum +=
                sign * sample.NormalizedArea * sample.Descriptor;
            region.DescriptorSecondMoment +=
                sign * sample.NormalizedArea *
                glm::outerProduct(sample.Descriptor, sample.Descriptor);
            region.NormalizedDescriptorSum +=
                sign * sample.NormalizedArea * sample.NormalizedCurvature;
            region.NormalizedDescriptorSecondMoment +=
                sign * sample.NormalizedArea *
                glm::outerProduct(sample.NormalizedCurvature,
                                  sample.NormalizedCurvature);
            for (std::size_t component = 0u;
                 component < region.WeightedUnary.size(); ++component)
            {
                region.WeightedUnary[component] +=
                    sign * sample.NormalizedArea * faceUnary[component];
            }
        }

        void RebuildRag(PatchPartition& partition,
                        const std::vector<PatchDualTransition>& transitions)
        {
            partition.Rag.clear();
            for (std::uint32_t transitionIndex = 0u;
                 transitionIndex < transitions.size(); ++transitionIndex)
            {
                const PatchDualTransition& transition =
                    transitions[transitionIndex];
                const std::uint32_t a =
                    partition.RegionByFace[transition.FaceA];
                const std::uint32_t b =
                    partition.RegionByFace[transition.FaceB];
                if (a != b)
                    partition.Rag[OrderedPair(a, b)].push_back(transitionIndex);
            }
        }

        [[nodiscard]] PatchPartition
        BuildPartition(const std::vector<PatchFaceSample>& samples,
                       const std::vector<PatchDualTransition>& transitions,
                       const std::vector<std::uint32_t>& regionByFace,
                       const std::vector<double>& unary,
                       const std::size_t componentCount,
                       const std::size_t regionCount)
        {
            PatchPartition partition{};
            partition.RegionByFace = regionByFace;
            partition.Regions.resize(regionCount);
            for (PatchRegion& region : partition.Regions)
                region.WeightedUnary.assign(componentCount, 0.0);
            for (std::uint32_t face = 0u; face < samples.size(); ++face)
            {
                const std::uint32_t regionId = regionByFace[face];
                PatchRegion& region = partition.Regions[regionId];
                region.Active = true;
                region.Faces.insert(face);
                AccumulateFace(
                    region, samples[face],
                    std::span<const double>{
                        unary.data() + face * componentCount, componentCount},
                    1.0);
            }
            RebuildRag(partition, transitions);
            return partition;
        }

        [[nodiscard]] double
        RegionCost(const PatchRegion& region,
                   const CurvaturePatchParams& params) noexcept
        {
            if (!region.Active || region.Faces.empty() ||
                region.WeightedUnary.empty())
            {
                return kInfinity;
            }
            const double minimum = *std::min_element(
                region.WeightedUnary.begin(), region.WeightedUnary.end());
            return minimum + params.PatchComplexityCost;
        }

        [[nodiscard]] double
        UnionRegionCost(const PatchRegion& a,
                        const PatchRegion& b,
                        const CurvaturePatchParams& params) noexcept
        {
            if (!a.Active || !b.Active ||
                a.WeightedUnary.size() != b.WeightedUnary.size() ||
                a.WeightedUnary.empty())
            {
                return kInfinity;
            }
            double minimum = kInfinity;
            for (std::size_t component = 0u; component < a.WeightedUnary.size();
                 ++component)
            {
                minimum = std::min(minimum, a.WeightedUnary[component] +
                                                b.WeightedUnary[component]);
            }
            return minimum + params.PatchComplexityCost;
        }

        [[nodiscard]] double
        PartitionRegionalCost(const PatchPartition& partition,
                              const CurvaturePatchParams& params) noexcept
        {
            double cost = 0.0;
            for (const PatchRegion& region : partition.Regions)
            {
                if (!region.Active)
                    continue;
                cost += RegionCost(region, params);
            }
            return cost;
        }

        [[nodiscard]] double CornerAngle(const HalfedgeMesh::Mesh& mesh,
                                         const FaceHandle face,
                                         const VertexHandle vertex)
        {
            std::array<VertexHandle, 2u> others{};
            std::size_t count = 0u;
            for (const VertexHandle candidate : mesh.VerticesAroundFace(face))
            {
                if (candidate == vertex)
                    continue;
                if (count >= others.size())
                    return kInfinity;
                others[count++] = candidate;
            }
            if (count != others.size())
                return kInfinity;
            const glm::dvec3 origin{mesh.Position(vertex)};
            const glm::dvec3 a = glm::dvec3(mesh.Position(others[0u])) - origin;
            const glm::dvec3 b = glm::dvec3(mesh.Position(others[1u])) - origin;
            const double crossLength = glm::length(glm::cross(a, b));
            const double dot = glm::dot(a, b);
            const double angle = std::atan2(crossLength, dot);
            return std::isfinite(angle) ? angle : kInfinity;
        }

        template <typename RegionForFace>
        [[nodiscard]] double VertexTurnContribution(
            const HalfedgeMesh::Mesh& mesh,
            const VertexHandle vertex,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const RegionForFace& regionForFace)
        {
            std::array<double, 2u> boundaryLengths{};
            std::size_t boundaryCount = 0u;
            std::array<std::uint32_t, 2u> incidentRegions{};
            std::size_t incidentRegionCount = 0u;
            const auto addIncidentRegion = [&](const std::uint32_t region)
            {
                const std::size_t storedRegionCount = std::min(
                    incidentRegionCount, incidentRegions.size());
                for (std::size_t i = 0u; i < storedRegionCount; ++i)
                {
                    if (incidentRegions[i] == region)
                        return;
                }
                if (incidentRegionCount < incidentRegions.size())
                    incidentRegions[incidentRegionCount] = region;
                ++incidentRegionCount;
            };
            for (const HalfedgeHandle halfedge :
                 mesh.HalfedgesAroundVertex(vertex))
            {
                const EdgeHandle edge = mesh.Edge(halfedge);
                const FaceHandle face0 = mesh.Face(mesh.Halfedge(edge, 0u));
                const FaceHandle face1 = mesh.Face(mesh.Halfedge(edge, 1u));
                if (!IsLiveFace(mesh, face0) || !IsLiveFace(mesh, face1))
                    continue;
                const std::uint32_t sample0 = faceSlotToSample[face0.Index];
                const std::uint32_t sample1 = faceSlotToSample[face1.Index];
                const std::uint32_t region0 = regionForFace(sample0);
                const std::uint32_t region1 = regionForFace(sample1);
                if (region0 == region1)
                    continue;
                addIncidentRegion(region0);
                addIncidentRegion(region1);
                double length = 0.0;
                bool found = false;
                for (const std::uint32_t transitionIndex :
                     incidentTransitions[sample0])
                {
                    const PatchDualTransition& transition =
                        transitions[transitionIndex];
                    if (transition.Edge == edge)
                    {
                        length = transition.NormalizedLength;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return kInfinity;
                if (boundaryCount < boundaryLengths.size())
                    boundaryLengths[boundaryCount] = length;
                ++boundaryCount;
            }
            if (boundaryCount != 2u || incidentRegionCount != 2u)
                return 0.0;

            const std::uint32_t firstRegion = incidentRegions[0u];
            double firstSector = 0.0;
            double secondSector = 0.0;
            for (const FaceHandle face : mesh.FacesAroundVertex(vertex))
            {
                if (!IsLiveFace(mesh, face))
                    continue;
                const std::uint32_t sample = faceSlotToSample[face.Index];
                const double angle = CornerAngle(mesh, face, vertex);
                if (!std::isfinite(angle))
                    return kInfinity;
                if (regionForFace(sample) == firstRegion)
                    firstSector += angle;
                else
                    secondSector += angle;
            }
            const double phi =
                std::min(std::abs(std::numbers::pi_v<double> - firstSector),
                         std::abs(std::numbers::pi_v<double> - secondSector));
            const double averageLength = std::max(
                0.5 * (boundaryLengths[0u] + boundaryLengths[1u]), kTiny);
            return phi * phi / averageLength;
        }

        [[nodiscard]] double BoundaryEnergy(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const std::vector<std::uint32_t>& regionByFace,
            const CurvaturePatchParams& params)
        {
            double length = 0.0;
            double support = 0.0;
            for (const PatchDualTransition& transition : transitions)
            {
                if (regionByFace[transition.FaceA] ==
                    regionByFace[transition.FaceB])
                {
                    continue;
                }
                length += transition.NormalizedLength;
                support +=
                    transition.NormalizedLength * transition.SoftConfidence;
            }
            double turning = 0.0;
            const auto regionForFace = [&](const std::uint32_t face)
            { return regionByFace[face]; };
            for (const VertexHandle vertex : mesh.LiveVertices())
            {
                const double contribution = VertexTurnContribution(
                    mesh, vertex, faceSlotToSample, transitions,
                    incidentTransitions, regionForFace);
                if (!std::isfinite(contribution))
                    return kInfinity;
                turning += contribution;
            }
            return params.BoundaryLengthWeight * length +
                   params.BoundaryTurnWeight * turning -
                   params.BoundaryFeatureWeight * support;
        }

        [[nodiscard]] double MergeBoundaryCredit(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const std::vector<std::uint32_t>& regionByFace,
            const RegionPair pair,
            const std::vector<std::uint32_t>& sharedTransitions,
            const CurvaturePatchParams& params)
        {
            double removedLength = 0.0;
            double removedSupport = 0.0;
            std::vector<std::uint32_t> affectedVertices;
            affectedVertices.reserve(2u * sharedTransitions.size());
            const auto addAffectedVertex = [&](const std::uint32_t vertex)
            {
                if (std::ranges::find(affectedVertices, vertex) ==
                    affectedVertices.end())
                {
                    affectedVertices.push_back(vertex);
                }
            };
            for (const std::uint32_t transitionIndex : sharedTransitions)
            {
                const PatchDualTransition& transition =
                    transitions[transitionIndex];
                removedLength += transition.NormalizedLength;
                removedSupport +=
                    transition.NormalizedLength * transition.SoftConfidence;
                addAffectedVertex(transition.VertexA.Index);
                addAffectedVertex(transition.VertexB.Index);
            }
            const auto before = [&](const std::uint32_t face)
            { return regionByFace[face]; };
            const auto after = [&](const std::uint32_t face)
            {
                const std::uint32_t region = regionByFace[face];
                return region == pair.second ? pair.first : region;
            };
            double turningCredit = 0.0;
            for (const std::uint32_t vertexSlot : affectedVertices)
            {
                const VertexHandle vertex{vertexSlot};
                const double oldTurn = VertexTurnContribution(
                    mesh, vertex, faceSlotToSample, transitions,
                    incidentTransitions, before);
                const double newTurn = VertexTurnContribution(
                    mesh, vertex, faceSlotToSample, transitions,
                    incidentTransitions, after);
                if (!std::isfinite(oldTurn) || !std::isfinite(newTurn))
                    return kInfinity;
                turningCredit += oldTurn - newTurn;
            }
            return params.BoundaryLengthWeight * removedLength +
                   params.BoundaryTurnWeight * turningCredit -
                   params.BoundaryFeatureWeight * removedSupport;
        }

        [[nodiscard]] double PartitionEnergy(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const PatchPartition& partition,
            const CurvaturePatchParams& params)
        {
            const double regional = PartitionRegionalCost(partition, params);
            const double boundary = BoundaryEnergy(
                mesh, faceSlotToSample, transitions, incidentTransitions,
                partition.RegionByFace, params);
            return regional + boundary;
        }

        [[nodiscard]] MergeEvaluation EvaluateMerge(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const PatchPartition& partition,
            const RegionPair pair,
            const std::vector<std::uint32_t>& sharedTransitions,
            const CurvaturePatchParams& params)
        {
            MergeEvaluation evaluation{};
            evaluation.Pair = pair;
            if (pair.first >= partition.Regions.size() ||
                pair.second >= partition.Regions.size() ||
                !partition.Regions[pair.first].Active ||
                !partition.Regions[pair.second].Active)
            {
                return evaluation;
            }
            for (const std::uint32_t transitionIndex : sharedTransitions)
            {
                if (transitions[transitionIndex].Hard)
                {
                    evaluation.HardBlocked = true;
                    return evaluation;
                }
            }
            const PatchRegion& a = partition.Regions[pair.first];
            const PatchRegion& b = partition.Regions[pair.second];
            evaluation.RegionalCostIncrease = UnionRegionCost(a, b, params) -
                                              RegionCost(a, params) -
                                              RegionCost(b, params);
            evaluation.BoundaryCredit = MergeBoundaryCredit(
                mesh, faceSlotToSample, transitions, incidentTransitions,
                partition.RegionByFace, pair, sharedTransitions, params);
            evaluation.Delta =
                evaluation.RegionalCostIncrease - evaluation.BoundaryCredit;
            return evaluation;
        }

        void ApplyMerge(PatchPartition& partition, const RegionPair pair)
        {
            PatchRegion& winner = partition.Regions[pair.first];
            PatchRegion& loser = partition.Regions[pair.second];
            for (const std::uint32_t face : loser.Faces)
                partition.RegionByFace[face] = pair.first;
            winner.Faces.insert(loser.Faces.begin(), loser.Faces.end());
            winner.Area += loser.Area;
            winner.DescriptorSum += loser.DescriptorSum;
            winner.DescriptorSecondMoment += loser.DescriptorSecondMoment;
            winner.NormalizedDescriptorSum += loser.NormalizedDescriptorSum;
            winner.NormalizedDescriptorSecondMoment +=
                loser.NormalizedDescriptorSecondMoment;
            for (std::size_t component = 0u;
                 component < winner.WeightedUnary.size(); ++component)
            {
                winner.WeightedUnary[component] +=
                    loser.WeightedUnary[component];
            }
            loser.Active = false;
            loser.Faces.clear();
            loser.Area = 0.0;
            std::fill(loser.WeightedUnary.begin(), loser.WeightedUnary.end(),
                      0.0);

            std::vector<std::pair<RegionPair, std::vector<std::uint32_t>>>
                redirected;
            for (auto it = partition.Rag.begin(); it != partition.Rag.end();)
            {
                if (it->first.first != pair.second &&
                    it->first.second != pair.second)
                {
                    ++it;
                    continue;
                }
                const std::uint32_t neighbor =
                    it->first.first == pair.second ? it->first.second
                                                  : it->first.first;
                if (neighbor != pair.first)
                {
                    redirected.emplace_back(
                        OrderedPair(pair.first, neighbor),
                        std::move(it->second));
                }
                it = partition.Rag.erase(it);
            }
            for (auto& [newPair, sharedTransitions] : redirected)
            {
                auto [target, inserted] = partition.Rag.try_emplace(newPair);
                if (inserted)
                {
                    target->second = std::move(sharedTransitions);
                    continue;
                }
                std::vector<std::uint32_t> merged;
                merged.reserve(
                    target->second.size() + sharedTransitions.size());
                std::ranges::merge(
                    target->second, sharedTransitions,
                    std::back_inserter(merged));
                target->second = std::move(merged);
            }
        }

        [[nodiscard]] CurvaturePatchStatus MergeToLocalOptimum(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<PatchFaceSample>& samples,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const CurvaturePatchParams& params,
            PatchPartition& partition,
            double& currentEnergy,
            CurvaturePatchResult& result)
        {
            std::map<RegionPair, MergeEvaluation> evaluations;
            const auto refreshEvaluations =
                [&](const std::vector<std::uint8_t>* affectedVertices,
                    const std::optional<std::uint32_t> changedRegion)
                -> CurvaturePatchStatus
            {
                for (auto it = evaluations.begin(); it != evaluations.end();)
                {
                    if (!partition.Rag.contains(it->first))
                        it = evaluations.erase(it);
                    else
                        ++it;
                }
                for (const auto& [pair, shared] : partition.Rag)
                {
                    bool needsRefresh = !evaluations.contains(pair) ||
                        (changedRegion &&
                         (pair.first == *changedRegion ||
                          pair.second == *changedRegion));
                    if (!needsRefresh && affectedVertices != nullptr)
                    {
                        needsRefresh = std::ranges::any_of(
                            shared,
                            [&](const std::uint32_t transitionIndex)
                            {
                                const PatchDualTransition& transition =
                                    transitions[transitionIndex];
                                return (*affectedVertices)
                                           [transition.VertexA.Index] != 0u ||
                                       (*affectedVertices)
                                           [transition.VertexB.Index] != 0u;
                            });
                    }
                    if (!needsRefresh)
                        continue;
                    MergeEvaluation candidate = EvaluateMerge(
                        mesh, faceSlotToSample, transitions,
                        incidentTransitions, partition, pair, shared, params);
                    if (!candidate.HardBlocked &&
                        !std::isfinite(candidate.Delta))
                    {
                        return CurvaturePatchStatus::NonFiniteEnergy;
                    }
                    evaluations[pair] = std::move(candidate);
                }
                return CurvaturePatchStatus::Success;
            };

            CurvaturePatchStatus refreshStatus =
                refreshEvaluations(nullptr, std::nullopt);
            if (refreshStatus != CurvaturePatchStatus::Success)
                return refreshStatus;
            for (;;)
            {
                std::optional<MergeEvaluation> best{};
                for (const auto& evaluation : evaluations)
                {
                    const MergeEvaluation& candidate = evaluation.second;
                    if (candidate.HardBlocked)
                        continue;
                    if (!(candidate.Delta < 0.0))
                        continue;
                    if (!best || candidate.Delta < best->Delta ||
                        (candidate.Delta == best->Delta &&
                         candidate.Pair < best->Pair))
                    {
                        best = candidate;
                    }
                }
                if (!best)
                    break;

                const RegionPair pair = best->Pair;
                std::vector<std::uint8_t> affectedVertices(
                    mesh.VerticesSize(), 0u);
                for (const std::uint32_t face :
                     partition.Regions[pair.second].Faces)
                {
                    for (const VertexHandle vertex :
                         mesh.VerticesAroundFace(samples[face].Face))
                    {
                        affectedVertices[vertex.Index] = 1u;
                    }
                }
                const double energyBefore = currentEnergy;
                ApplyMerge(partition, pair);
                const double predictedEnergy = energyBefore + best->Delta;
                if (!std::isfinite(predictedEnergy)
                    || !(predictedEnergy < energyBefore))
                {
                    return CurvaturePatchStatus::EnergyInvariantFailed;
                }
                currentEnergy = predictedEnergy;
                result.AcceptedMerges.push_back(CurvaturePatchMergeDiagnostics{
                    .RegionA = pair.first,
                    .RegionB = pair.second,
                    .ResultRegion = pair.first,
                    .RegionalCostIncrease = best->RegionalCostIncrease,
                    .BoundaryCredit = best->BoundaryCredit,
                    .DeltaMerge = best->Delta,
                    .EnergyAfter = currentEnergy,
                });
                result.AcceptedEnergyHistory.push_back(currentEnergy);
                refreshStatus = refreshEvaluations(
                    &affectedVertices, pair.first);
                if (refreshStatus != CurvaturePatchStatus::Success)
                    return refreshStatus;
            }
            const double exactEnergy = PartitionEnergy(
                mesh, faceSlotToSample, transitions, incidentTransitions,
                partition, params);
            if (!std::isfinite(exactEnergy))
                return CurvaturePatchStatus::NonFiniteEnergy;
            const double tolerance = 1.0e-8
                * (1.0 + std::max(
                    std::abs(currentEnergy), std::abs(exactEnergy)));
            if (std::abs(currentEnergy - exactEnergy) > tolerance)
                return CurvaturePatchStatus::EnergyInvariantFailed;
            currentEnergy = exactEnergy;
            return CurvaturePatchStatus::Success;
        }

        [[nodiscard]] bool RegionRemainsConnectedWithoutFace(
            const std::uint32_t region,
            const std::uint32_t removedFace,
            const PatchPartition& partition,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions)
        {
            const PatchRegion& source = partition.Regions[region];
            if (source.Faces.size() <= 1u)
                return false;
            const auto start = std::find_if(
                source.Faces.begin(), source.Faces.end(),
                [&](const std::uint32_t face) { return face != removedFace; });
            if (start == source.Faces.end())
                return false;

            std::set<std::uint32_t> visited;
            std::queue<std::uint32_t> queue;
            visited.insert(*start);
            queue.push(*start);
            while (!queue.empty())
            {
                const std::uint32_t face = queue.front();
                queue.pop();
                for (const std::uint32_t transitionIndex :
                     incidentTransitions[face])
                {
                    const std::uint32_t neighbor =
                        OtherFace(transitions[transitionIndex], face);
                    if (neighbor == removedFace ||
                        partition.RegionByFace[neighbor] != region)
                    {
                        continue;
                    }
                    if (visited.insert(neighbor).second)
                        queue.push(neighbor);
                }
            }
            return visited.size() + 1u == source.Faces.size();
        }

        [[nodiscard]] double
        AdjustedRegionCost(const PatchRegion& region,
                           const PatchFaceSample& sample,
                           const std::span<const double> faceUnary,
                           const double sign,
                           const CurvaturePatchParams& params)
        {
            double minimum = kInfinity;
            for (std::size_t component = 0u;
                 component < region.WeightedUnary.size(); ++component)
            {
                minimum = std::min(minimum, region.WeightedUnary[component] +
                                                sign * sample.NormalizedArea *
                                                    faceUnary[component]);
            }
            return minimum + params.PatchComplexityCost;
        }

        [[nodiscard]] double MoveBoundaryDelta(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const std::vector<std::uint32_t>& regionByFace,
            const std::uint32_t face,
            const std::uint32_t target,
            const CurvaturePatchParams& params)
        {
            double lengthDelta = 0.0;
            double supportDelta = 0.0;
            std::vector<std::uint32_t> affectedVertices;
            affectedVertices.reserve(2u * incidentTransitions[face].size());
            const auto addAffectedVertex = [&](const std::uint32_t vertex)
            {
                if (std::ranges::find(affectedVertices, vertex) ==
                    affectedVertices.end())
                {
                    affectedVertices.push_back(vertex);
                }
            };
            for (const std::uint32_t transitionIndex :
                 incidentTransitions[face])
            {
                const PatchDualTransition& transition =
                    transitions[transitionIndex];
                const std::uint32_t neighbor = OtherFace(transition, face);
                const bool before =
                    regionByFace[face] != regionByFace[neighbor];
                const bool after = target != regionByFace[neighbor];
                if (before == after)
                    continue;
                const double sign = after ? 1.0 : -1.0;
                lengthDelta += sign * transition.NormalizedLength;
                supportDelta += sign * transition.NormalizedLength *
                                transition.SoftConfidence;
                addAffectedVertex(transition.VertexA.Index);
                addAffectedVertex(transition.VertexB.Index);
            }
            const auto before = [&](const std::uint32_t sample)
            { return regionByFace[sample]; };
            const auto after = [&](const std::uint32_t sample)
            { return sample == face ? target : regionByFace[sample]; };
            double turningDelta = 0.0;
            for (const std::uint32_t vertexSlot : affectedVertices)
            {
                const VertexHandle vertex{vertexSlot};
                const double oldTurn = VertexTurnContribution(
                    mesh, vertex, faceSlotToSample, transitions,
                    incidentTransitions, before);
                const double newTurn = VertexTurnContribution(
                    mesh, vertex, faceSlotToSample, transitions,
                    incidentTransitions, after);
                if (!std::isfinite(oldTurn) || !std::isfinite(newTurn))
                    return kInfinity;
                turningDelta += newTurn - oldTurn;
            }
            return params.BoundaryLengthWeight * lengthDelta +
                   params.BoundaryTurnWeight * turningDelta -
                   params.BoundaryFeatureWeight * supportDelta;
        }

        [[nodiscard]] CurvaturePatchStatus RefineBoundaries(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<PatchFaceSample>& samples,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const std::vector<double>& unary,
            const std::size_t componentCount,
            const CurvaturePatchParams& params,
            PatchPartition& partition,
            double& currentEnergy,
            CurvaturePatchResult& result)
        {
            for (std::uint32_t sweep = 0u;
                 sweep < params.MaximumRefinementSweeps; ++sweep)
            {
                bool moved = false;
                ++result.Diagnostics.RefinementSweeps;
                for (std::uint32_t face = 0u; face < samples.size(); ++face)
                {
                    const std::uint32_t source = partition.RegionByFace[face];
                    if (!partition.Regions[source].Active ||
                        partition.Regions[source].Faces.size() <= 1u)
                    {
                        continue;
                    }
                    std::set<std::uint32_t> targets;
                    for (const std::uint32_t transitionIndex :
                         incidentTransitions[face])
                    {
                        const std::uint32_t neighbor =
                            OtherFace(transitions[transitionIndex], face);
                        const std::uint32_t target =
                            partition.RegionByFace[neighbor];
                        if (target != source)
                            targets.insert(target);
                    }
                    if (targets.empty() ||
                        !RegionRemainsConnectedWithoutFace(
                            source, face, partition, transitions,
                            incidentTransitions))
                    {
                        continue;
                    }

                    std::optional<std::uint32_t> bestTarget{};
                    double bestDelta = 0.0;
                    const std::span<const double> faceUnary{
                        unary.data() + face * componentCount, componentCount};
                    for (const std::uint32_t target : targets)
                    {
                        if (!partition.Regions[target].Active)
                            continue;
                        bool internalizesHard = false;
                        for (const std::uint32_t transitionIndex :
                             incidentTransitions[face])
                        {
                            const PatchDualTransition& transition =
                                transitions[transitionIndex];
                            if (!transition.Hard)
                                continue;
                            const std::uint32_t neighbor =
                                OtherFace(transition, face);
                            if (partition.RegionByFace[neighbor] == target)
                            {
                                internalizesHard = true;
                                break;
                            }
                        }
                        if (internalizesHard)
                            continue;

                        const double regionalDelta =
                            AdjustedRegionCost(partition.Regions[source],
                                               samples[face], faceUnary, -1.0,
                                               params) +
                            AdjustedRegionCost(partition.Regions[target],
                                               samples[face], faceUnary, 1.0,
                                               params) -
                            RegionCost(partition.Regions[source], params) -
                            RegionCost(partition.Regions[target], params);
                        const double boundaryDelta = MoveBoundaryDelta(
                            mesh, faceSlotToSample, transitions,
                            incidentTransitions, partition.RegionByFace, face,
                            target, params);
                        const double delta = regionalDelta + boundaryDelta;
                        if (!std::isfinite(delta))
                            return CurvaturePatchStatus::NonFiniteEnergy;
                        if (delta < bestDelta ||
                            (delta == bestDelta && bestTarget &&
                             target < *bestTarget))
                        {
                            bestDelta = delta;
                            bestTarget = target;
                        }
                    }
                    if (!bestTarget)
                        continue;

                    PatchRegion& sourceRegion = partition.Regions[source];
                    PatchRegion& targetRegion = partition.Regions[*bestTarget];
                    sourceRegion.Faces.erase(face);
                    targetRegion.Faces.insert(face);
                    AccumulateFace(sourceRegion, samples[face], faceUnary,
                                   -1.0);
                    AccumulateFace(targetRegion, samples[face], faceUnary, 1.0);
                    partition.RegionByFace[face] = *bestTarget;
                    const double energyBefore = currentEnergy;
                    const double predictedEnergy = energyBefore + bestDelta;
                    if (!std::isfinite(predictedEnergy)
                        || !(predictedEnergy < energyBefore))
                    {
                        return CurvaturePatchStatus::EnergyInvariantFailed;
                    }
                    currentEnergy = predictedEnergy;
                    result.RefinementMoves.push_back(
                        CurvaturePatchRefinementDiagnostics{
                            .FaceSlot = samples[face].Face.Index,
                            .RegionBefore = source,
                            .RegionAfter = *bestTarget,
                            .Sweep = sweep,
                            .DeltaEnergy = bestDelta,
                            .EnergyAfter = currentEnergy,
                        });
                    result.AcceptedEnergyHistory.push_back(currentEnergy);
                    moved = true;
                }
                if (!moved)
                    break;
                RebuildRag(partition, transitions);
                const CurvaturePatchStatus mergeStatus = MergeToLocalOptimum(
                    mesh, samples, faceSlotToSample, transitions,
                    incidentTransitions, params, partition, currentEnergy,
                    result);
                if (mergeStatus != CurvaturePatchStatus::Success)
                    return mergeStatus;
            }
            RebuildRag(partition, transitions);
            const double exactEnergy = PartitionEnergy(
                mesh, faceSlotToSample, transitions, incidentTransitions,
                partition, params);
            if (!std::isfinite(exactEnergy))
                return CurvaturePatchStatus::NonFiniteEnergy;
            const double tolerance = 1.0e-8
                * (1.0 + std::max(
                    std::abs(currentEnergy), std::abs(exactEnergy)));
            if (std::abs(currentEnergy - exactEnergy) > tolerance)
                return CurvaturePatchStatus::EnergyInvariantFailed;
            currentEnergy = exactEnergy;
            return CurvaturePatchStatus::Success;
        }

        [[nodiscard]] bool RegionIsConnected(
            const std::uint32_t region,
            const PatchPartition& partition,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions)
        {
            const PatchRegion& record = partition.Regions[region];
            if (!record.Active || record.Faces.empty())
                return false;
            std::set<std::uint32_t> visited;
            std::queue<std::uint32_t> queue;
            const std::uint32_t start = *record.Faces.begin();
            visited.insert(start);
            queue.push(start);
            while (!queue.empty())
            {
                const std::uint32_t face = queue.front();
                queue.pop();
                for (const std::uint32_t transitionIndex :
                     incidentTransitions[face])
                {
                    const std::uint32_t neighbor =
                        OtherFace(transitions[transitionIndex], face);
                    if (partition.RegionByFace[neighbor] != region)
                        continue;
                    if (visited.insert(neighbor).second)
                        queue.push(neighbor);
                }
            }
            return visited.size() == record.Faces.size();
        }

        [[nodiscard]] CurvaturePatchStatus ValidateFinalPartition(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const PatchPartition& partition,
            CurvaturePatchResult& result)
        {
            std::size_t assignedFaces = 0u;
            for (std::uint32_t region = 0u; region < partition.Regions.size();
                 ++region)
            {
                if (!partition.Regions[region].Active)
                    continue;
                assignedFaces += partition.Regions[region].Faces.size();
                if (!RegionIsConnected(region, partition, transitions,
                                       incidentTransitions))
                {
                    return CurvaturePatchStatus::ConnectivityInvariantFailed;
                }
            }
            if (assignedFaces != partition.RegionByFace.size())
                return CurvaturePatchStatus::ConnectivityInvariantFailed;
            for (const PatchDualTransition& transition : transitions)
            {
                if (transition.Hard &&
                    partition.RegionByFace[transition.FaceA] ==
                        partition.RegionByFace[transition.FaceB])
                {
                    return CurvaturePatchStatus::HardConstraintViolated;
                }
            }

            std::vector<std::size_t> boundaryDegree(mesh.VerticesSize(), 0u);
            for (const PatchDualTransition& transition : transitions)
            {
                if (partition.RegionByFace[transition.FaceA] ==
                    partition.RegionByFace[transition.FaceB])
                {
                    continue;
                }
                ++boundaryDegree[transition.VertexA.Index];
                ++boundaryDegree[transition.VertexB.Index];
            }
            for (const VertexHandle vertex : mesh.LiveVertices())
            {
                const std::size_t degree = boundaryDegree[vertex.Index];
                if (degree == 1u)
                {
                    if (!mesh.IsBoundary(vertex))
                    {
                        return CurvaturePatchStatus::
                            ConnectivityInvariantFailed;
                    }
                    ++result.Diagnostics.BoundaryEndpointCount;
                }
                else if (degree >= 3u)
                {
                    ++result.Diagnostics.BoundaryJunctionCount;
                }
            }
            return CurvaturePatchStatus::Success;
        }

        [[nodiscard]] glm::vec4 RegionColor(const std::uint32_t region) noexcept
        {
            const float hue = std::fmod(
                0.61803398875f * static_cast<float>(region) + 0.08f, 1.0f);
            constexpr float saturation = 0.68f;
            constexpr float value = 0.92f;
            const float scaled = hue * 6.0f;
            const int sector = static_cast<int>(std::floor(scaled));
            const float fraction = scaled - static_cast<float>(sector);
            const float p = value * (1.0f - saturation);
            const float q = value * (1.0f - saturation * fraction);
            const float t = value * (1.0f - saturation * (1.0f - fraction));
            switch (sector % 6)
            {
            case 0:
                return {value, t, p, 1.0f};
            case 1:
                return {q, value, p, 1.0f};
            case 2:
                return {p, value, t, 1.0f};
            case 3:
                return {p, q, value, 1.0f};
            case 4:
                return {t, p, value, 1.0f};
            default:
                return {value, p, q, 1.0f};
            }
        }

        [[nodiscard]] CurvaturePatchRegionDiagnostics
        RegionDiagnostics(const std::uint32_t publishedRegion,
                          const PatchRegion& region,
                          const Gmm::Model& mixture,
                          const CurvaturePatchParams& params)
        {
            CurvaturePatchRegionDiagnostics diagnostics{};
            diagnostics.Region = publishedRegion;
            diagnostics.FaceCount = region.Faces.size();
            diagnostics.NormalizedArea = region.Area;
            const auto best = std::min_element(region.WeightedUnary.begin(),
                                               region.WeightedUnary.end());
            const std::size_t winning = static_cast<std::size_t>(
                std::distance(region.WeightedUnary.begin(), best));
            diagnostics.WinningComponent = static_cast<std::uint32_t>(winning);
            diagnostics.ModelCost = *best + params.PatchComplexityCost;
            if (region.WeightedUnary.size() > 1u)
            {
                double runner = kInfinity;
                for (std::size_t component = 0u;
                     component < region.WeightedUnary.size(); ++component)
                {
                    if (component != winning)
                    {
                        runner =
                            std::min(runner, region.WeightedUnary[component]);
                    }
                }
                diagnostics.RunnerUpMargin = runner - *best;
            }
            if (region.Area > 0.0)
            {
                const glm::dvec2 mean = region.DescriptorSum / region.Area;
                const glm::dmat2 covariance =
                    region.DescriptorSecondMoment / region.Area -
                    glm::outerProduct(mean, mean);
                diagnostics.DescriptorMeanK1 = mean.x;
                diagnostics.DescriptorMeanK2 = mean.y;
                diagnostics.DescriptorCovarianceK1K1 =
                    std::max(covariance[0][0], 0.0);
                diagnostics.DescriptorCovarianceK1K2 =
                    0.5 * (covariance[0][1] + covariance[1][0]);
                diagnostics.DescriptorCovarianceK2K2 =
                    std::max(covariance[1][1], 0.0);

                const glm::dvec2 normalizedMean =
                    region.NormalizedDescriptorSum / region.Area;
                const glm::dmat2 normalizedMoment =
                    region.NormalizedDescriptorSecondMoment / region.Area;
                const glm::dvec2 componentMean{
                    mixture.Components[winning].Mean.x,
                    mixture.Components[winning].Mean.y};
                const double meanSquare =
                    normalizedMoment[0][0] + normalizedMoment[1][1];
                const double residualSquared = std::max(
                    meanSquare - 2.0 * glm::dot(normalizedMean, componentMean) +
                        glm::dot(componentMean, componentMean),
                    0.0);
                diagnostics.NormalizedResidual = std::sqrt(residualSquared);
            }
            return diagnostics;
        }

        [[nodiscard]] CurvaturePatchStatus PublishFinalResult(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<PatchFaceSample>& samples,
            const std::vector<PatchDualTransition>& transitions,
            const std::vector<std::vector<std::uint32_t>>& incidentTransitions,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::vector<std::uint32_t>& faceComponents,
            const Gmm::Model& mixture,
            const CurvaturePatchParams& params,
            const PatchPartition& partition,
            CurvaturePatchResult& result)
        {
            const CurvaturePatchStatus validation = ValidateFinalPartition(
                mesh, transitions, incidentTransitions, partition, result);
            if (validation != CurvaturePatchStatus::Success)
                return validation;

            std::vector<std::uint32_t> compact(partition.Regions.size(),
                                               kInvalidPatchIndex);
            std::uint32_t nextRegion = 0u;
            for (std::uint32_t region = 0u; region < partition.Regions.size();
                 ++region)
            {
                if (!partition.Regions[region].Active)
                    continue;
                compact[region] = nextRegion++;
                result.Regions.push_back(RegionDiagnostics(
                    compact[region], partition.Regions[region], mixture,
                    params));
            }
            result.Diagnostics.FinalRegionCount = nextRegion;

            for (std::uint32_t face = 0u; face < samples.size(); ++face)
            {
                const std::uint32_t published =
                    compact[partition.RegionByFace[face]];
                const std::uint32_t faceSlot = samples[face].Face.Index;
                result.FaceComponents[faceSlot] = faceComponents[face];
                result.FaceRegions[faceSlot] = published;
                result.FaceRegionColors[faceSlot] = RegionColor(published);
            }

            std::map<RegionPair, MergeEvaluation> finalEvaluations;
            bool hasAdmissible = false;
            double minimumAdmissible = kInfinity;
            for (const auto& [pair, shared] : partition.Rag)
            {
                const MergeEvaluation evaluation = EvaluateMerge(
                    mesh, faceSlotToSample, transitions, incidentTransitions,
                    partition, pair, shared, params);
                finalEvaluations.emplace(pair, evaluation);
                const bool hard = evaluation.HardBlocked;
                if (!hard)
                {
                    if (!std::isfinite(evaluation.Delta))
                        return CurvaturePatchStatus::NonFiniteEnergy;
                    hasAdmissible = true;
                    minimumAdmissible =
                        std::min(minimumAdmissible, evaluation.Delta);
                    if (evaluation.Delta < 0.0)
                        ++result.Diagnostics.FinalNegativeMergeCount;
                }
                result.FinalAdjacencies.push_back(
                    CurvaturePatchAdjacencyDiagnostics{
                        .RegionA = compact[pair.first],
                        .RegionB = compact[pair.second],
                        .HardBlocked = hard,
                        .RegionalCostIncrease = evaluation.RegionalCostIncrease,
                        .BoundaryCredit = evaluation.BoundaryCredit,
                        .DeltaMerge = evaluation.Delta,
                    });
            }
            result.Diagnostics.MinimumFinalAdmissibleDelta =
                hasAdmissible ? minimumAdmissible : 0.0;
            if (result.Diagnostics.FinalNegativeMergeCount != 0u)
                return CurvaturePatchStatus::EnergyInvariantFailed;

            constexpr glm::vec4 hardColor{1.0f, 0.08f, 0.02f, 1.0f};
            constexpr glm::vec4 softColor{1.0f, 0.72f, 0.05f, 1.0f};
            constexpr glm::vec4 closureColor{0.15f, 0.65f, 1.0f, 1.0f};
            for (const PatchDualTransition& transition : transitions)
            {
                const std::uint32_t regionA =
                    partition.RegionByFace[transition.FaceA];
                const std::uint32_t regionB =
                    partition.RegionByFace[transition.FaceB];
                if (regionA == regionB)
                    continue;
                const RegionPair pair = OrderedPair(regionA, regionB);
                const MergeEvaluation& evaluation = finalEvaluations.at(pair);
                const std::size_t edge = transition.Edge.Index;
                result.EdgeBoundaries[edge] = 1u;
                ++result.Diagnostics.FinalBoundaryEdgeCount;
                if (evaluation.HardBlocked)
                {
                    result.EdgeBoundaryRoles[edge] =
                        PatchBoundaryRole::HardFeature;
                    result.EdgeBoundaryMergeDelta[edge] = kInfinity;
                    result.EdgeBoundaryColors[edge] = hardColor;
                    ++result.Diagnostics.HardBoundaryEdgeCount;
                }
                else if (transition.SoftConfidence > 0.0)
                {
                    result.EdgeBoundaryRoles[edge] =
                        PatchBoundaryRole::SoftFeatureSupported;
                    result.EdgeBoundaryMergeDelta[edge] = evaluation.Delta;
                    result.EdgeBoundaryColors[edge] = softColor;
                    ++result.Diagnostics.SoftBoundaryEdgeCount;
                }
                else
                {
                    result.EdgeBoundaryRoles[edge] =
                        PatchBoundaryRole::CurvatureClosure;
                    result.EdgeBoundaryMergeDelta[edge] = evaluation.Delta;
                    result.EdgeBoundaryColors[edge] = closureColor;
                    ++result.Diagnostics.ClosureBoundaryEdgeCount;
                }
            }
            return CurvaturePatchStatus::Success;
        }
    } // namespace

    const char* ToString(const CurvaturePatchStatus status) noexcept
    {
        switch (status)
        {
        case CurvaturePatchStatus::Success:
            return "success";
        case CurvaturePatchStatus::EmptyMesh:
            return "empty_mesh";
        case CurvaturePatchStatus::UnsupportedSubmeshView:
            return "unsupported_submesh_view";
        case CurvaturePatchStatus::InvalidParameters:
            return "invalid_parameters";
        case CurvaturePatchStatus::CurvatureCountMismatch:
            return "curvature_count_mismatch";
        case CurvaturePatchStatus::HardEvidenceCountMismatch:
            return "hard_evidence_count_mismatch";
        case CurvaturePatchStatus::SoftEvidenceCountMismatch:
            return "soft_evidence_count_mismatch";
        case CurvaturePatchStatus::InvalidHardEvidence:
            return "invalid_hard_evidence";
        case CurvaturePatchStatus::NonFiniteSoftEvidence:
            return "non_finite_soft_evidence";
        case CurvaturePatchStatus::SoftEvidenceOutOfRange:
            return "soft_evidence_out_of_range";
        case CurvaturePatchStatus::InvalidSeedOverride:
            return "invalid_seed_override";
        case CurvaturePatchStatus::NonTriangleFace:
            return "non_triangle_face";
        case CurvaturePatchStatus::NonFinitePosition:
            return "non_finite_position";
        case CurvaturePatchStatus::DegenerateFace:
            return "degenerate_face";
        case CurvaturePatchStatus::NonFiniteCurvature:
            return "non_finite_curvature";
        case CurvaturePatchStatus::InvalidCurvatureOrder:
            return "invalid_curvature_order";
        case CurvaturePatchStatus::InvalidTopology:
            return "invalid_topology";
        case CurvaturePatchStatus::GaussianMixtureFitFailed:
            return "gaussian_mixture_fit_failed";
        case CurvaturePatchStatus::PosteriorEvaluationFailed:
            return "posterior_evaluation_failed";
        case CurvaturePatchStatus::GrowthFailed:
            return "growth_failed";
        case CurvaturePatchStatus::NonFiniteEnergy:
            return "non_finite_energy";
        case CurvaturePatchStatus::EnergyInvariantFailed:
            return "energy_invariant_failed";
        case CurvaturePatchStatus::ConnectivityInvariantFailed:
            return "connectivity_invariant_failed";
        case CurvaturePatchStatus::HardConstraintViolated:
            return "hard_constraint_violated";
        }
        return "unknown";
    }

    const char* ToString(const PatchBoundaryRole role) noexcept
    {
        switch (role)
        {
        case PatchBoundaryRole::None:
            return "none";
        case PatchBoundaryRole::HardFeature:
            return "hard_feature";
        case PatchBoundaryRole::SoftFeatureSupported:
            return "soft_feature_supported";
        case PatchBoundaryRole::CurvatureClosure:
            return "curvature_closure";
        }
        return "unknown";
    }

    CurvaturePatchResult
    SegmentFeatureAlignedPatches(const HalfedgeMesh::Mesh& mesh,
                                 const std::span<const double> maxPrincipal,
                                 const std::span<const double> minPrincipal,
                                 const FeatureEvidenceView evidence,
                                 const CurvaturePatchParams& params,
                                 const PatchSeedOverrides seedOverrides)
    {
        const ProfileClock::time_point totalStart = ProfileClock::now();
        CurvaturePatchResult result{};
        CurvaturePatchDiagnostics& diagnostics = result.Diagnostics;
        diagnostics.VertexSlotCount = mesh.VerticesSize();
        diagnostics.LiveVertexCount = mesh.VertexCount();
        diagnostics.FaceSlotCount = mesh.FacesSize();
        diagnostics.LiveFaceCount = mesh.FaceCount();
        diagnostics.EdgeSlotCount = mesh.EdgesSize();
        diagnostics.LiveEdgeCount = mesh.EdgeCount();

        result.FaceComponents.assign(mesh.FacesSize(), kInvalidPatchIndex);
        result.ProvisionalFaceRegions.assign(mesh.FacesSize(),
                                             kInvalidPatchIndex);
        result.FaceRegions.assign(mesh.FacesSize(), kInvalidPatchIndex);
        result.FaceGrowthCosts.assign(mesh.FacesSize(), kInfinity);
        result.EdgeGrowthFlags.assign(mesh.EdgesSize(), 0u);
        result.EdgeGrowthTransitionCosts.assign(mesh.EdgesSize(), 0.0);
        result.EdgeProvisionalBoundaries.assign(mesh.EdgesSize(), 0u);
        result.EdgeBoundaries.assign(mesh.EdgesSize(), 0u);
        result.EdgeBoundaryRoles.assign(mesh.EdgesSize(),
                                        PatchBoundaryRole::None);
        result.EdgeBoundaryMergeDelta.assign(mesh.EdgesSize(), 0.0);
        result.FaceRegionColors.assign(mesh.FacesSize(), glm::vec4{0.0f});
        result.EdgeBoundaryColors.assign(mesh.EdgesSize(), glm::vec4{0.0f});

        const auto finish =
            [&](const CurvaturePatchStatus status) -> CurvaturePatchResult
        {
            diagnostics.Status = status;
            diagnostics.AcceptedMergeCount = result.AcceptedMerges.size();
            diagnostics.AcceptedRefinementMoveCount =
                result.RefinementMoves.size();
            diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return std::move(result);
        };

        if (mesh.IsEmpty() || mesh.FaceCount() == 0u)
            return finish(CurvaturePatchStatus::EmptyMesh);
        if (mesh.IsSubmeshView())
            return finish(CurvaturePatchStatus::UnsupportedSubmeshView);
        if (!IsValidParams(params))
            return finish(CurvaturePatchStatus::InvalidParameters);

        const ProfileClock::time_point validationStart = ProfileClock::now();
        std::vector<PatchFaceSample> samples;
        std::vector<std::uint32_t> faceSlotToSample;
        std::vector<PatchDualTransition> transitions;
        std::vector<std::vector<std::uint32_t>> incidentTransitions;
        const CurvaturePatchStatus sampleStatus = BuildSamplesAndTransitions(
            mesh, maxPrincipal, minPrincipal, evidence, params, samples,
            faceSlotToSample, transitions, incidentTransitions, result);
        diagnostics.Timings.ValidationAndSamplingMilliseconds =
            ElapsedMilliseconds(validationStart);
        if (sampleStatus != CurvaturePatchStatus::Success)
            return finish(sampleStatus);

        Gmm::Model mixture;
        std::vector<double> unary;
        std::vector<std::uint32_t> faceComponents;
        const CurvaturePatchStatus mixtureStatus = FitMixtureAndPosteriors(
            samples, params, unary, faceComponents, mixture, result,
            diagnostics.Timings.MixtureFittingMilliseconds,
            diagnostics.Timings.PosteriorConstructionMilliseconds);
        if (mixtureStatus != CurvaturePatchStatus::Success)
            return finish(mixtureStatus);

        const ProfileClock::time_point seedStart = ProfileClock::now();
        std::vector<std::uint32_t> seeds;
        const CurvaturePatchStatus seedStatus = SelectSeeds(
            samples, faceSlotToSample, transitions, incidentTransitions,
            seedOverrides, diagnostics.SeedSpacingCost, seeds);
        diagnostics.Timings.SeedSelectionMilliseconds =
            ElapsedMilliseconds(seedStart);
        if (seedStatus != CurvaturePatchStatus::Success)
            return finish(seedStatus);
        diagnostics.SeedCount = seeds.size();
        result.SeedFaceSlots.reserve(seeds.size());
        for (const std::uint32_t seed : seeds)
            result.SeedFaceSlots.push_back(samples[seed].Face.Index);

        const ProfileClock::time_point growthStart = ProfileClock::now();
        std::vector<std::uint32_t> provisionalRegions;
        std::vector<double> growthCosts;
        const CurvaturePatchStatus growthStatus =
            GrowSeedRegions(samples, transitions, incidentTransitions, seeds,
                            provisionalRegions, growthCosts);
        diagnostics.Timings.SimultaneousGrowthMilliseconds =
            ElapsedMilliseconds(growthStart);
        if (growthStatus != CurvaturePatchStatus::Success)
            return finish(growthStatus);
        diagnostics.ProvisionalRegionCount = seeds.size();
        for (std::uint32_t face = 0u; face < samples.size(); ++face)
        {
            const std::uint32_t slot = samples[face].Face.Index;
            result.ProvisionalFaceRegions[slot] = provisionalRegions[face];
            result.FaceGrowthCosts[slot] = growthCosts[face];
        }
        for (const PatchDualTransition& transition : transitions)
        {
            if (provisionalRegions[transition.FaceA] ==
                provisionalRegions[transition.FaceB])
            {
                continue;
            }
            const std::size_t edge = transition.Edge.Index;
            result.EdgeProvisionalBoundaries[edge] = 1u;
            AddFlag(result.EdgeGrowthFlags[edge],
                    PatchGrowthTransitionFlag::ProvisionalFront);
            ++diagnostics.ProvisionalBoundaryEdgeCount;
            diagnostics.ProvisionalFrontLength += transition.NormalizedLength;
        }

        PatchPartition partition =
            BuildPartition(samples, transitions, provisionalRegions, unary,
                           mixture.Components.size(), seeds.size());
        double currentEnergy =
            PartitionEnergy(mesh, faceSlotToSample, transitions,
                            incidentTransitions, partition, params);
        if (!std::isfinite(currentEnergy))
            return finish(CurvaturePatchStatus::NonFiniteEnergy);
        diagnostics.InitialEnergy = currentEnergy;
        result.AcceptedEnergyHistory.push_back(currentEnergy);

        const ProfileClock::time_point mergeStart = ProfileClock::now();
        const CurvaturePatchStatus mergeStatus = MergeToLocalOptimum(
            mesh, samples, faceSlotToSample, transitions, incidentTransitions,
            params, partition, currentEnergy, result);
        diagnostics.Timings.RegionMergingMilliseconds =
            ElapsedMilliseconds(mergeStart);
        if (mergeStatus != CurvaturePatchStatus::Success)
            return finish(mergeStatus);

        const ProfileClock::time_point refinementStart = ProfileClock::now();
        const CurvaturePatchStatus refinementStatus = RefineBoundaries(
            mesh, samples, faceSlotToSample, transitions, incidentTransitions,
            unary, mixture.Components.size(), params, partition, currentEnergy,
            result);
        diagnostics.Timings.BoundaryRefinementMilliseconds =
            ElapsedMilliseconds(refinementStart);
        if (refinementStatus != CurvaturePatchStatus::Success)
            return finish(refinementStatus);

        diagnostics.FinalEnergy = currentEnergy;
        const ProfileClock::time_point publicationStart = ProfileClock::now();
        const CurvaturePatchStatus publicationStatus = PublishFinalResult(
            mesh, samples, transitions, incidentTransitions, faceSlotToSample,
            faceComponents, mixture, params, partition, result);
        diagnostics.Timings.PublicationAndValidationMilliseconds =
            ElapsedMilliseconds(publicationStart);
        return finish(publicationStatus);
    }
} // namespace Geometry::CurvatureSegmentation
