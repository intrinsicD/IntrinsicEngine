// Opt-in curvature-patch correctness and bounded-health benchmark producer.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "CurvatureProfileSupport.hpp"

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;
import Geometry.Properties;

namespace
{
    using namespace Intrinsic::Bench::CurvatureProfile;
    namespace Segment = Geometry::CurvatureSegmentation;

    struct FeaturePatchFeatureVariantProfile
    {
        std::string_view Name{};
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t ExpectedSoftEdgeCount{0u};
        std::size_t RetainedSoftEdgeCount{0u};
        std::size_t HardFeatureEdgeCount{0u};
        std::size_t EndpointVertexCount{0u};
        std::size_t JunctionVertexCount{0u};
        std::size_t BoundedSearchCount{0u};
        std::size_t SettledFaceVisitCount{0u};
        double FeatureMaskErrorFraction{1.0};
        BoundaryProfile Boundary{};
        Segment::FeatureEvidenceStageTimings Timings{};
        bool Succeeded{false};
    };

    struct FeaturePatchFeatureProfile
    {
        std::array<FeaturePatchFeatureVariantProfile, 2u> Variants{};
        double RuntimeMilliseconds{0.0};
        double MaxFeatureMaskErrorFraction{1.0};
        double MaxBoundaryErrorNormalized{1.0};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    struct FeaturePatchQualityVariantProfile
    {
        std::string_view Name{};
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t SeedCount{0u};
        std::size_t FinalRegionCount{0u};
        std::size_t SoftBoundaryEdgeCount{0u};
        std::size_t ClosureBoundaryEdgeCount{0u};
        double VariationOfInformation{1.0};
        BoundaryProfile Boundary{};
        Segment::CurvaturePatchStageTimings Timings{};
        bool Succeeded{false};
    };

    struct FeaturePatchQualityProfile
    {
        std::array<FeaturePatchQualityVariantProfile, 2u> Variants{};
        double RuntimeMilliseconds{0.0};
        double MaxVariationOfInformation{1.0};
        double MaxBoundaryErrorNormalized{1.0};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    struct FeaturePatchRefutationProfile
    {
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t BaselineSeedCount{0u};
        std::size_t PerturbedSeedCount{0u};
        std::size_t BaselineRegionCount{0u};
        std::size_t PerturbedRegionCount{0u};
        std::size_t PerturbedClosureBoundaryEdgeCount{0u};
        double BaselineVariationOfInformation{1.0};
        double PerturbedVariationOfInformation{0.0};
        BoundaryProfile PerturbedBoundary{};
        Segment::CurvaturePatchStageTimings BaselineTimings{};
        Segment::CurvaturePatchStageTimings PerturbedTimings{};
        double RuntimeMilliseconds{0.0};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    struct FeaturePatchHealthProfile
    {
        std::size_t VertexCount{0u};
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t SeedCount{0u};
        std::size_t ProvisionalRegionCount{0u};
        std::size_t AcceptedMergeCount{0u};
        std::size_t FinalRegionCount{0u};
        std::size_t FinalBoundaryEdgeCount{0u};
        std::size_t ResultStorageEntryCount{0u};
        std::size_t SparseStorageBoundEntries{0u};
        double UnassignedFaceFraction{1.0};
        double DeterministicPayloadMismatch{1.0};
        double RuntimeMilliseconds{0.0};
        Segment::CurvaturePatchStageTimings Timings{};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool SparseStorageBoundSatisfied{false};
        bool Succeeded{false};
    };
    [[nodiscard]] bool FiniteNonnegative(const double value) noexcept
    {
        return std::isfinite(value) && value >= 0.0;
    }

    [[nodiscard]] bool FeatureTimingsAreValid(
        const Segment::FeatureEvidenceStageTimings& timings) noexcept
    {
        return FiniteNonnegative(timings.CurvatureEstimationMilliseconds) &&
               FiniteNonnegative(timings.ValidationAndFaceSamplingMilliseconds) &&
               FiniteNonnegative(timings.HardFeatureClassificationMilliseconds) &&
               FiniteNonnegative(timings.MultiScaleResponseMilliseconds) &&
               FiniteNonnegative(timings.NonMaximumSuppressionMilliseconds) &&
               FiniteNonnegative(
                   timings.HysteresisAndFragmentFilteringMilliseconds) &&
               FiniteNonnegative(timings.TotalMilliseconds);
    }

    [[nodiscard]] bool PatchTimingsAreValid(
        const Segment::CurvaturePatchStageTimings& timings) noexcept
    {
        return FiniteNonnegative(timings.ValidationAndSamplingMilliseconds) &&
               FiniteNonnegative(timings.MixtureFittingMilliseconds) &&
               FiniteNonnegative(timings.PosteriorConstructionMilliseconds) &&
               FiniteNonnegative(timings.SeedSelectionMilliseconds) &&
               FiniteNonnegative(timings.SimultaneousGrowthMilliseconds) &&
               FiniteNonnegative(timings.RegionMergingMilliseconds) &&
               FiniteNonnegative(timings.BoundaryRefinementMilliseconds) &&
               FiniteNonnegative(timings.PublicationAndValidationMilliseconds) &&
               FiniteNonnegative(timings.TotalMilliseconds);
    }

    [[nodiscard]] Segment::CurvaturePatchParams
    MakePatchParams(const std::uint32_t componentCount)
    {
        Segment::CurvaturePatchParams params{};
        params.Mixture.SelectionMode = Segment::ComponentSelectionMode::FixedCount;
        params.Mixture.FixedComponentCount = componentCount;
        return params;
    }

    [[nodiscard]] std::vector<std::uint8_t>
    SoftEvidenceMask(const Segment::FeatureEvidenceResult& result)
    {
        std::vector<std::uint8_t> mask(result.SoftEdgeConfidence.size(), 0u);
        for (std::size_t edge = 0u; edge < result.SoftEdgeConfidence.size(); ++edge)
        {
            mask[edge] = result.SoftEdgeConfidence[edge] > 0.0 ? 1u : 0u;
        }
        return mask;
    }

    [[nodiscard]] std::vector<std::uint32_t>
    FacesAdjacentToBoundary(const SurfaceControlFixture& fixture)
    {
        std::set<std::uint32_t> faces;
        for (const Geometry::EdgeHandle edge : fixture.Mesh.LiveEdges())
        {
            if (fixture.ExpectedBoundaryMask[edge.Index] == 0u)
                continue;
            const Geometry::FaceHandle a =
                fixture.Mesh.Face(fixture.Mesh.Halfedge(edge, 0u));
            const Geometry::FaceHandle b =
                fixture.Mesh.Face(fixture.Mesh.Halfedge(edge, 1u));
            if (a.IsValid())
                faces.insert(a.Index);
            if (b.IsValid())
                faces.insert(b.Index);
        }
        return {faces.begin(), faces.end()};
    }

    [[nodiscard]] double TriangleArea(const Geometry::HalfedgeMesh::Mesh& mesh,
                                      const Geometry::FaceHandle face)
    {
        std::array<glm::dvec3, 3u> points{};
        std::size_t count = 0u;
        for (const Geometry::VertexHandle vertex : mesh.VerticesAroundFace(face))
        {
            if (count < points.size())
                points[count++] = ToDouble(mesh.Position(vertex));
        }
        if (count != points.size())
            return 0.0;
        return 0.5 * glm::length(glm::cross(points[1u] - points[0u],
                                            points[2u] - points[0u]));
    }

    [[nodiscard]] double
    AreaWeightedVariationOfInformation(const Geometry::HalfedgeMesh::Mesh& mesh,
                                       const std::vector<std::uint32_t>& expected,
                                       const std::vector<std::uint32_t>& actual)
    {
        if (expected.size() != mesh.FacesSize() ||
            actual.size() != mesh.FacesSize())
        {
            return 1.0;
        }

        std::map<std::uint32_t, double> expectedMass;
        std::map<std::uint32_t, double> actualMass;
        std::map<std::pair<std::uint32_t, std::uint32_t>, double> jointMass;
        double total = 0.0;
        for (const Geometry::FaceHandle face : mesh.LiveFaces())
        {
            const double area = TriangleArea(mesh, face);
            if (!FiniteNonnegative(area) || !(area > 0.0))
                return 1.0;
            total += area;
            expectedMass[expected[face.Index]] += area;
            actualMass[actual[face.Index]] += area;
            jointMass[{expected[face.Index], actual[face.Index]}] += area;
        }
        if (!(total > 0.0) || !std::isfinite(total))
            return 1.0;

        const auto entropy = [total](const auto& masses)
        {
            double value = 0.0;
            for (const auto& [label, mass] : masses)
            {
                static_cast<void>(label);
                const double probability = mass / total;
                value -= probability * std::log(probability);
            }
            return value;
        };

        double mutualInformation = 0.0;
        for (const auto& [labels, mass] : jointMass)
        {
            const double probability = mass / total;
            mutualInformation +=
                probability *
                std::log(probability / ((expectedMass[labels.first] / total) *
                                        (actualMass[labels.second] / total)));
        }
        return std::max(entropy(expectedMass) + entropy(actualMass) -
                            2.0 * mutualInformation,
                        0.0);
    }

    [[nodiscard]] Fixture MakeLocalPatchRefutationFixture()
    {
        constexpr CohortSpec spec{
            .Token = "feature_patch_seed_refutation",
            .Rows = 8u,
            .Columns = 16u,
            .WarmupIterations = 0u,
            .MeasuredIterations = 1u,
        };
        Fixture fixture = MakeGridFixture(spec, false);
        for (const Geometry::VertexHandle vertex : fixture.Mesh.LiveVertices())
        {
            glm::vec3& position = fixture.Mesh.Position(vertex);
            position.x = 2.0f * position.x - 1.0f;
            position.y = 2.0f * position.y - 1.0f;
            const double value = std::tanh(static_cast<double>(position.x) / 0.05);
            fixture.K1[vertex.Index] = std::max(value, 0.0);
            fixture.K2[vertex.Index] = std::min(value, 0.0);
        }
        return fixture;
    }

    [[nodiscard]] std::vector<std::uint32_t>
    PerturbSeedsOneDualStep(const Geometry::HalfedgeMesh::Mesh& mesh,
                            const std::vector<std::uint32_t>& seeds)
    {
        std::set<std::uint32_t> perturbed;
        for (const std::uint32_t seedSlot : seeds)
        {
            const Geometry::FaceHandle seed{seedSlot};
            std::optional<std::uint32_t> neighborSlot;
            for (const Geometry::EdgeHandle edge : mesh.LiveEdges())
            {
                const Geometry::FaceHandle a = mesh.Face(mesh.Halfedge(edge, 0u));
                const Geometry::FaceHandle b = mesh.Face(mesh.Halfedge(edge, 1u));
                if (!a.IsValid() || !b.IsValid())
                    continue;
                if (a == seed)
                    neighborSlot = b.Index;
                else if (b == seed)
                    neighborSlot = a.Index;
                else
                    continue;
                break;
            }
            perturbed.insert(neighborSlot.value_or(seedSlot));
        }
        return {perturbed.begin(), perturbed.end()};
    }

    [[nodiscard]] bool
    SamePatchPayload(const Segment::CurvaturePatchResult& a,
                     const Segment::CurvaturePatchResult& b) noexcept
    {
        const auto sameColors = [](const std::vector<glm::vec4>& left,
                                   const std::vector<glm::vec4>& right)
        {
            return left.size() == right.size() &&
                   std::equal(left.begin(), left.end(), right.begin(),
                              [](const glm::vec4& x, const glm::vec4& y)
                              { return glm::all(glm::equal(x, y)); });
        };
        return a.Succeeded() && b.Succeeded() &&
               a.FaceComponents == b.FaceComponents &&
               a.ProvisionalFaceRegions == b.ProvisionalFaceRegions &&
               a.FaceRegions == b.FaceRegions &&
               a.FaceGrowthCosts == b.FaceGrowthCosts &&
               a.EdgeGrowthFlags == b.EdgeGrowthFlags &&
               a.EdgeGrowthTransitionCosts == b.EdgeGrowthTransitionCosts &&
               a.EdgeProvisionalBoundaries == b.EdgeProvisionalBoundaries &&
               a.EdgeBoundaries == b.EdgeBoundaries &&
               a.EdgeBoundaryRoles == b.EdgeBoundaryRoles &&
               a.EdgeBoundaryMergeDelta == b.EdgeBoundaryMergeDelta &&
               sameColors(a.FaceRegionColors, b.FaceRegionColors) &&
               sameColors(a.EdgeBoundaryColors, b.EdgeBoundaryColors) &&
               a.SeedFaceSlots == b.SeedFaceSlots &&
               a.AcceptedEnergyHistory == b.AcceptedEnergyHistory &&
               a.Diagnostics.ProvisionalRegionCount ==
                   b.Diagnostics.ProvisionalRegionCount &&
               a.Diagnostics.AcceptedMergeCount ==
                   b.Diagnostics.AcceptedMergeCount &&
               a.Diagnostics.AcceptedRefinementMoveCount ==
                   b.Diagnostics.AcceptedRefinementMoveCount &&
               a.Diagnostics.FinalRegionCount == b.Diagnostics.FinalRegionCount &&
               a.Diagnostics.FinalBoundaryEdgeCount ==
                   b.Diagnostics.FinalBoundaryEdgeCount &&
               a.Diagnostics.FinalEnergy == b.Diagnostics.FinalEnergy;
    }

    [[nodiscard]] std::size_t
    ResultStorageEntryCount(const Segment::CurvaturePatchResult& result) noexcept
    {
        return result.FaceComponents.size() + result.ProvisionalFaceRegions.size() +
               result.FaceRegions.size() + result.FaceGrowthCosts.size() +
               result.EdgeGrowthFlags.size() +
               result.EdgeGrowthTransitionCosts.size() +
               result.EdgeProvisionalBoundaries.size() +
               result.EdgeBoundaries.size() + result.EdgeBoundaryRoles.size() +
               result.EdgeBoundaryMergeDelta.size() +
               result.FaceRegionColors.size() + result.EdgeBoundaryColors.size() +
               result.SeedFaceSlots.size() + result.Regions.size() +
               result.AcceptedMerges.size() + result.FinalAdjacencies.size() +
               result.RefinementMoves.size() + result.AcceptedEnergyHistory.size() +
               result.Diagnostics.Candidates.size() +
               result.Diagnostics.Components.size();
    }

    [[nodiscard]] double
    UnassignedFaceFraction(const Geometry::HalfedgeMesh::Mesh& mesh,
                           const Segment::CurvaturePatchResult& result) noexcept
    {
        if (result.FaceRegions.size() != mesh.FacesSize() || mesh.FaceCount() == 0u)
        {
            return 1.0;
        }
        std::size_t unassigned = 0u;
        for (const Geometry::FaceHandle face : mesh.LiveFaces())
        {
            unassigned +=
                result.FaceRegions[face.Index] == Segment::kInvalidPatchIndex ? 1u
                                                                              : 0u;
        }
        return static_cast<double>(unassigned) /
               static_cast<double>(mesh.FaceCount());
    }

    [[nodiscard]] FeaturePatchFeatureVariantProfile
    RunFeaturePatchFeatureVariant(const bool flippedDiagonals)
    {
        SurfaceControlFixture fixture =
            MakeSmoothTransitionFixture(flippedDiagonals);
        FeaturePatchFeatureVariantProfile profile{};
        profile.Name = flippedDiagonals ? "diagonal_b" : "diagonal_a";
        profile.FaceCount = fixture.Mesh.FaceCount();
        profile.EdgeCount = fixture.Mesh.EdgeCount();
        profile.ExpectedSoftEdgeCount = static_cast<std::size_t>(
            std::count(fixture.ExpectedBoundaryMask.begin(),
                       fixture.ExpectedBoundaryMask.end(), std::uint8_t{1u}));
        if (!fixture.Valid)
            return profile;

        const Segment::FeatureEvidenceResult result =
            Segment::DetectFeatureEvidence(fixture.Mesh, fixture.K1, fixture.K2);
        if (!result.Succeeded())
            return profile;

        const std::vector<std::uint8_t> retainedMask = SoftEvidenceMask(result);
        profile.RetainedSoftEdgeCount = result.Diagnostics.RetainedSoftEdgeCount;
        profile.HardFeatureEdgeCount = result.Diagnostics.HardFeatureEdgeCount;
        profile.EndpointVertexCount = result.Diagnostics.EndpointVertexCount;
        profile.JunctionVertexCount = result.Diagnostics.JunctionVertexCount;
        profile.BoundedSearchCount = result.Diagnostics.BoundedSearchCount;
        profile.SettledFaceVisitCount = result.Diagnostics.SettledFaceVisitCount;
        profile.FeatureMaskErrorFraction = LiveEdgeMaskErrorFraction(
            fixture.Mesh, retainedMask, fixture.ExpectedBoundaryMask);
        const double heightRange =
            SmoothTransitionHeight(1.0) - SmoothTransitionHeight(-1.0);
        const double referenceDiagonal = std::sqrt(5.0 + heightRange * heightRange);
        profile.Boundary = MeasureVerticalTransitionBoundary(
            fixture.Mesh, retainedMask, 0.0, 0.5, 0.0, 1.0, referenceDiagonal);
        profile.Timings = result.Diagnostics.Timings;
        profile.Succeeded =
            profile.HardFeatureEdgeCount == 0u &&
            profile.RetainedSoftEdgeCount == profile.ExpectedSoftEdgeCount &&
            profile.FeatureMaskErrorFraction == 0.0 &&
            profile.EndpointVertexCount == 2u &&
            profile.JunctionVertexCount == 0u &&
            profile.BoundedSearchCount ==
                2u * result.Diagnostics.InteriorCandidateEdgeCount &&
            profile.SettledFaceVisitCount > 0u && profile.Boundary.Valid &&
            profile.Boundary.SymmetricHausdorffUpperBoundNormalized <= 0.02 &&
            FeatureTimingsAreValid(profile.Timings);
        return profile;
    }

    [[nodiscard]] FeaturePatchFeatureProfile RunFeaturePatchFeatureSmoke()
    {
        FeaturePatchFeatureProfile profile{};
        profile.Variants[0u] = RunFeaturePatchFeatureVariant(false);
        profile.Variants[1u] = RunFeaturePatchFeatureVariant(true);
        profile.RuntimeMilliseconds =
            std::max(profile.Variants[0u].Timings.TotalMilliseconds,
                     profile.Variants[1u].Timings.TotalMilliseconds);
        profile.MaxFeatureMaskErrorFraction =
            std::max(profile.Variants[0u].FeatureMaskErrorFraction,
                     profile.Variants[1u].FeatureMaskErrorFraction);
        profile.MaxBoundaryErrorNormalized = std::max(
            profile.Variants[0u].Boundary.SymmetricHausdorffUpperBoundNormalized,
            profile.Variants[1u].Boundary.SymmetricHausdorffUpperBoundNormalized);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        profile.Succeeded =
            profile.Variants[0u].Succeeded && profile.Variants[1u].Succeeded;
        return profile;
    }

    [[nodiscard]] FeaturePatchQualityVariantProfile
    RunFeaturePatchQualityVariant(const bool flippedDiagonals)
    {
        SurfaceControlFixture fixture =
            MakeSmoothTransitionFixture(flippedDiagonals);
        FeaturePatchQualityVariantProfile profile{};
        profile.Name = flippedDiagonals ? "diagonal_b" : "diagonal_a";
        profile.FaceCount = fixture.Mesh.FaceCount();
        profile.EdgeCount = fixture.Mesh.EdgeCount();
        if (!fixture.Valid)
            return profile;

        const Segment::FeatureEvidenceResult evidence =
            Segment::DetectFeatureEvidence(fixture.Mesh, fixture.K1, fixture.K2);
        if (!evidence.Succeeded())
            return profile;
        const std::vector<std::uint32_t> seeds = FacesAdjacentToBoundary(fixture);
        const Segment::CurvaturePatchResult result =
            Segment::SegmentFeatureAlignedPatches(
                fixture.Mesh, fixture.K1, fixture.K2, evidence.View(),
                MakePatchParams(1u), {seeds, true});
        if (!result.Succeeded())
            return profile;

        profile.SeedCount = result.Diagnostics.SeedCount;
        profile.FinalRegionCount = result.Diagnostics.FinalRegionCount;
        profile.SoftBoundaryEdgeCount = result.Diagnostics.SoftBoundaryEdgeCount;
        profile.ClosureBoundaryEdgeCount =
            result.Diagnostics.ClosureBoundaryEdgeCount;
        profile.VariationOfInformation = AreaWeightedVariationOfInformation(
            fixture.Mesh, fixture.ExpectedFaceRegime, result.FaceRegions);
        const double heightRange =
            SmoothTransitionHeight(1.0) - SmoothTransitionHeight(-1.0);
        const double referenceDiagonal = std::sqrt(5.0 + heightRange * heightRange);
        profile.Boundary = MeasureVerticalTransitionBoundary(
            fixture.Mesh, result.EdgeBoundaries, 0.0, 0.5, 0.0, 1.0,
            referenceDiagonal);
        profile.Timings = result.Diagnostics.Timings;
        profile.Succeeded =
            profile.SeedCount == seeds.size() && profile.FinalRegionCount == 2u &&
            result.Diagnostics.HardBoundaryEdgeCount == 0u &&
            profile.SoftBoundaryEdgeCount > 0u &&
            profile.ClosureBoundaryEdgeCount == 0u &&
            profile.VariationOfInformation <= 0.01 && profile.Boundary.Valid &&
            profile.Boundary.SymmetricHausdorffUpperBoundNormalized <= 0.02 &&
            profile.Boundary.EndpointCount == 2u &&
            profile.Boundary.JunctionCount == 0u &&
            PatchTimingsAreValid(profile.Timings);
        return profile;
    }

    [[nodiscard]] FeaturePatchQualityProfile RunFeaturePatchQualitySmoke()
    {
        FeaturePatchQualityProfile profile{};
        profile.Variants[0u] = RunFeaturePatchQualityVariant(false);
        profile.Variants[1u] = RunFeaturePatchQualityVariant(true);
        profile.RuntimeMilliseconds =
            std::max(profile.Variants[0u].Timings.TotalMilliseconds,
                     profile.Variants[1u].Timings.TotalMilliseconds);
        profile.MaxVariationOfInformation =
            std::max(profile.Variants[0u].VariationOfInformation,
                     profile.Variants[1u].VariationOfInformation);
        profile.MaxBoundaryErrorNormalized = std::max(
            profile.Variants[0u].Boundary.SymmetricHausdorffUpperBoundNormalized,
            profile.Variants[1u].Boundary.SymmetricHausdorffUpperBoundNormalized);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        profile.Succeeded =
            profile.Variants[0u].Succeeded && profile.Variants[1u].Succeeded;
        return profile;
    }

    [[nodiscard]] FeaturePatchRefutationProfile RunFeaturePatchSeedRefutation()
    {
        FeaturePatchRefutationProfile profile{};
        Fixture fixture = MakeLocalPatchRefutationFixture();
        profile.FaceCount = fixture.Mesh.FaceCount();
        profile.EdgeCount = fixture.Mesh.EdgeCount();
        if (!fixture.Valid)
            return profile;

        const std::vector<std::uint8_t> hard(fixture.Mesh.EdgesSize(), 0u);
        const std::vector<double> soft(fixture.Mesh.EdgesSize(), 0.0);
        const Segment::CurvaturePatchResult baseline =
            Segment::SegmentFeatureAlignedPatches(fixture.Mesh, fixture.K1,
                                                  fixture.K2, {hard, soft},
                                                  MakePatchParams(2u));
        if (!baseline.Succeeded())
            return profile;

        const std::vector<std::uint32_t> perturbedSeeds =
            PerturbSeedsOneDualStep(fixture.Mesh, baseline.SeedFaceSlots);
        const Segment::CurvaturePatchResult perturbed =
            Segment::SegmentFeatureAlignedPatches(
                fixture.Mesh, fixture.K1, fixture.K2, {hard, soft},
                MakePatchParams(2u), {perturbedSeeds, true});
        if (!perturbed.Succeeded())
            return profile;

        profile.BaselineSeedCount = baseline.Diagnostics.SeedCount;
        profile.PerturbedSeedCount = perturbed.Diagnostics.SeedCount;
        profile.BaselineRegionCount = baseline.Diagnostics.FinalRegionCount;
        profile.PerturbedRegionCount = perturbed.Diagnostics.FinalRegionCount;
        profile.PerturbedClosureBoundaryEdgeCount =
            perturbed.Diagnostics.ClosureBoundaryEdgeCount;
        profile.BaselineVariationOfInformation = AreaWeightedVariationOfInformation(
            fixture.Mesh, fixture.ExpectedFaceRegime, baseline.FaceRegions);
        profile.PerturbedVariationOfInformation =
            AreaWeightedVariationOfInformation(
                fixture.Mesh, fixture.ExpectedFaceRegime, perturbed.FaceRegions);
        profile.PerturbedBoundary = MeasureVerticalTransitionBoundary(
            fixture.Mesh, perturbed.EdgeBoundaries, 0.0, 0.0, -1.0, 1.0,
            std::sqrt(8.0));
        profile.BaselineTimings = baseline.Diagnostics.Timings;
        profile.PerturbedTimings = perturbed.Diagnostics.Timings;
        profile.RuntimeMilliseconds = profile.BaselineTimings.TotalMilliseconds +
                                      profile.PerturbedTimings.TotalMilliseconds;
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        profile.Succeeded = profile.BaselineRegionCount == 2u &&
                            profile.BaselineVariationOfInformation <= 0.01 &&
                            profile.PerturbedVariationOfInformation > 0.01 &&
                            profile.PerturbedRegionCount > 2u &&
                            profile.PerturbedClosureBoundaryEdgeCount > 0u &&
                            profile.PerturbedBoundary.Valid &&
                            PatchTimingsAreValid(profile.BaselineTimings) &&
                            PatchTimingsAreValid(profile.PerturbedTimings);
        return profile;
    }

    [[nodiscard]] FeaturePatchHealthProfile RunFeaturePatchHealth100k()
    {
        constexpr CohortSpec spec{
            .Token = "feature_patch_health_100k",
            .Rows = 200u,
            .Columns = 250u,
            .WarmupIterations = 0u,
            .MeasuredIterations = 2u,
        };
        FeaturePatchHealthProfile profile{};
        Fixture fixture = MakeGridFixture(spec, false);
        profile.VertexCount = fixture.Mesh.VertexCount();
        profile.FaceCount = fixture.Mesh.FaceCount();
        profile.EdgeCount = fixture.Mesh.EdgeCount();
        if (!fixture.Valid || profile.FaceCount != 100000u)
            return profile;

        std::fill(fixture.K1.begin(), fixture.K1.end(), 0.0);
        std::fill(fixture.K2.begin(), fixture.K2.end(), 0.0);
        const std::vector<std::uint8_t> hard(fixture.Mesh.EdgesSize(), 0u);
        const std::vector<double> soft(fixture.Mesh.EdgesSize(), 0.0);
        const Segment::CurvaturePatchParams params = MakePatchParams(1u);
        const Segment::CurvaturePatchResult first =
            Segment::SegmentFeatureAlignedPatches(fixture.Mesh, fixture.K1,
                                                  fixture.K2, {hard, soft}, params);
        if (!first.Succeeded())
            return profile;
        const Segment::CurvaturePatchResult second =
            Segment::SegmentFeatureAlignedPatches(fixture.Mesh, fixture.K1,
                                                  fixture.K2, {hard, soft}, params);
        if (!second.Succeeded())
            return profile;

        profile.SeedCount = first.Diagnostics.SeedCount;
        profile.ProvisionalRegionCount = first.Diagnostics.ProvisionalRegionCount;
        profile.AcceptedMergeCount = first.Diagnostics.AcceptedMergeCount;
        profile.FinalRegionCount = first.Diagnostics.FinalRegionCount;
        profile.FinalBoundaryEdgeCount = first.Diagnostics.FinalBoundaryEdgeCount;
        profile.ResultStorageEntryCount = ResultStorageEntryCount(first);
        const std::size_t sparseBase =
            profile.VertexCount + profile.FaceCount + profile.EdgeCount + 1u;
        profile.SparseStorageBoundEntries = 64u * sparseBase;
        profile.SparseStorageBoundSatisfied =
            profile.ResultStorageEntryCount <= profile.SparseStorageBoundEntries;
        profile.UnassignedFaceFraction =
            UnassignedFaceFraction(fixture.Mesh, first);
        profile.DeterministicPayloadMismatch =
            SamePatchPayload(first, second) ? 0.0 : 1.0;
        profile.RuntimeMilliseconds =
            std::max(first.Diagnostics.Timings.TotalMilliseconds,
                     second.Diagnostics.Timings.TotalMilliseconds);
        profile.Timings = second.Diagnostics.Timings;
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();

        const bool cardinalitiesMatch =
            first.FaceComponents.size() == fixture.Mesh.FacesSize() &&
            first.ProvisionalFaceRegions.size() == fixture.Mesh.FacesSize() &&
            first.FaceRegions.size() == fixture.Mesh.FacesSize() &&
            first.FaceGrowthCosts.size() == fixture.Mesh.FacesSize() &&
            first.FaceRegionColors.size() == fixture.Mesh.FacesSize() &&
            first.EdgeGrowthFlags.size() == fixture.Mesh.EdgesSize() &&
            first.EdgeGrowthTransitionCosts.size() == fixture.Mesh.EdgesSize() &&
            first.EdgeProvisionalBoundaries.size() == fixture.Mesh.EdgesSize() &&
            first.EdgeBoundaries.size() == fixture.Mesh.EdgesSize() &&
            first.EdgeBoundaryRoles.size() == fixture.Mesh.EdgesSize() &&
            first.EdgeBoundaryMergeDelta.size() == fixture.Mesh.EdgesSize() &&
            first.EdgeBoundaryColors.size() == fixture.Mesh.EdgesSize();
        profile.Succeeded =
            cardinalitiesMatch && profile.SeedCount > 0u &&
            profile.ProvisionalRegionCount >= profile.FinalRegionCount &&
            profile.AcceptedMergeCount + profile.FinalRegionCount ==
                profile.ProvisionalRegionCount &&
            profile.FinalRegionCount == 1u &&
            profile.FinalBoundaryEdgeCount == 0u &&
            first.Diagnostics.FinalNegativeMergeCount == 0u &&
            profile.UnassignedFaceFraction == 0.0 &&
            profile.DeterministicPayloadMismatch == 0.0 &&
            profile.SparseStorageBoundSatisfied &&
            PatchTimingsAreValid(first.Diagnostics.Timings) &&
            PatchTimingsAreValid(second.Diagnostics.Timings);
        return profile;
    }
    [[nodiscard]] std::string FeaturePatchFeatureBenchmarkId()
    {
        return "geometry.curvature_segmentation.feature_patch.feature_smoke";
    }

    [[nodiscard]] std::string FeaturePatchQualityBenchmarkId()
    {
        return "geometry.curvature_segmentation.feature_patch.quality_smoke";
    }

    [[nodiscard]] std::string FeaturePatchRefutationBenchmarkId()
    {
        return "geometry.curvature_segmentation.feature_patch.seed_refutation";
    }

    [[nodiscard]] std::string FeaturePatchHealthBenchmarkId()
    {
        return "geometry.curvature_segmentation.feature_patch.health.100k";
    }
    void EmitBoundary(std::ostringstream& out, const BoundaryProfile& boundary)
    {
        out << "{\"valid\":" << (boundary.Valid ? "true" : "false")
            << ",\"edge_count\":" << boundary.EdgeCount
            << ",\"endpoint_count\":" << boundary.EndpointCount
            << ",\"junction_count\":" << boundary.JunctionCount
            << ",\"reference_sample_count\":" << boundary.ReferenceSampleCount
            << ",\"reference_sample_spacing_normalized\":"
            << boundary.ReferenceSampleSpacingNormalized
            << ",\"symmetric_hausdorff_upper_bound_normalized\":"
            << boundary.SymmetricHausdorffUpperBoundNormalized
            << ",\"tolerance_band_precision\":" << boundary.ToleranceBandPrecision
            << ",\"tolerance_band_recall\":" << boundary.ToleranceBandRecall
            << ",\"predicted_length_normalized\":"
            << boundary.PredictedLengthNormalized
            << ",\"reference_length_normalized\":"
            << boundary.ReferenceLengthNormalized << '}';
    }

    void EmitFeatureTimings(std::ostringstream& out,
                            const Segment::FeatureEvidenceStageTimings& timings)
    {
        out << "{\"curvature_estimation_ms\":"
            << timings.CurvatureEstimationMilliseconds
            << ",\"validation_and_face_sampling_ms\":"
            << timings.ValidationAndFaceSamplingMilliseconds
            << ",\"hard_feature_classification_ms\":"
            << timings.HardFeatureClassificationMilliseconds
            << ",\"multi_scale_response_ms\":"
            << timings.MultiScaleResponseMilliseconds
            << ",\"non_maximum_suppression_ms\":"
            << timings.NonMaximumSuppressionMilliseconds
            << ",\"hysteresis_and_fragment_filtering_ms\":"
            << timings.HysteresisAndFragmentFilteringMilliseconds
            << ",\"total_ms\":" << timings.TotalMilliseconds << '}';
    }

    void EmitPatchTimings(std::ostringstream& out,
                          const Segment::CurvaturePatchStageTimings& timings)
    {
        out << "{\"validation_and_sampling_ms\":"
            << timings.ValidationAndSamplingMilliseconds
            << ",\"mixture_fitting_ms\":" << timings.MixtureFittingMilliseconds
            << ",\"posterior_construction_ms\":"
            << timings.PosteriorConstructionMilliseconds
            << ",\"seed_selection_ms\":" << timings.SeedSelectionMilliseconds
            << ",\"simultaneous_growth_ms\":"
            << timings.SimultaneousGrowthMilliseconds
            << ",\"region_merging_ms\":" << timings.RegionMergingMilliseconds
            << ",\"boundary_refinement_ms\":"
            << timings.BoundaryRefinementMilliseconds
            << ",\"publication_and_validation_ms\":"
            << timings.PublicationAndValidationMilliseconds
            << ",\"total_ms\":" << timings.TotalMilliseconds << '}';
    }

    [[nodiscard]] std::string
    EmitFeaturePatchFeatureResult(const FeaturePatchFeatureProfile& profile,
                                  const std::string& commit)
    {
        const std::string benchmarkId = FeaturePatchFeatureBenchmarkId();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9) << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": "
               "\"builtin.method_039.smooth_transition_diagonal_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << profile.RuntimeMilliseconds << ",\n"
            << "    \"memory_peak_bytes\": " << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << profile.MaxFeatureMaskErrorFraction
            << ",\n"
            << "    \"quality_error_linf\": " << profile.MaxBoundaryErrorNormalized
            << ",\n"
            << "    \"population_count\": 2\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": "
               "\"IntrinsicCurvaturePatchProfile\",\n"
            << "    \"implementation_version\": "
               "\"method_039_feature_evidence_unadopted\",\n"
            << "    \"candidate_adopted\": false,\n"
            << "    \"quality_error_l2_unit\": "
               "\"maximum_live_edge_feature_mask_mismatch_fraction\",\n"
            << "    \"quality_error_linf_unit\": "
               "\"maximum_bbox_normalized_boundary_distance_upper_bound\",\n"
            << "    \"aggregation\": \"max_of_diagonal_pair\",\n"
            << "    \"variants\": [";
        for (std::size_t index = 0u; index < profile.Variants.size(); ++index)
        {
            if (index != 0u)
                out << ',';
            const FeaturePatchFeatureVariantProfile& variant =
                profile.Variants[index];
            out << "{\"name\":\"" << variant.Name << "\""
                << ",\"face_count\":" << variant.FaceCount
                << ",\"edge_count\":" << variant.EdgeCount
                << ",\"expected_soft_edge_count\":" << variant.ExpectedSoftEdgeCount
                << ",\"retained_soft_edge_count\":" << variant.RetainedSoftEdgeCount
                << ",\"hard_feature_edge_count\":" << variant.HardFeatureEdgeCount
                << ",\"endpoint_vertex_count\":" << variant.EndpointVertexCount
                << ",\"junction_vertex_count\":" << variant.JunctionVertexCount
                << ",\"bounded_search_count\":" << variant.BoundedSearchCount
                << ",\"settled_face_visit_count\":" << variant.SettledFaceVisitCount
                << ",\"feature_mask_error_fraction\":"
                << variant.FeatureMaskErrorFraction << ",\"continuous_boundary\":";
            EmitBoundary(out, variant.Boundary);
            out << ",\"stage_timings\":";
            EmitFeatureTimings(out, variant.Timings);
            out << ",\"passed\":" << (variant.Succeeded ? "true" : "false") << '}';
        }
        out << "]\n"
            << "  },\n"
            << "  \"status\": \"" << (profile.Succeeded ? "passed" : "failed")
            << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] std::string
    EmitFeaturePatchQualityResult(const FeaturePatchQualityProfile& profile,
                                  const std::string& commit)
    {
        const std::string benchmarkId = FeaturePatchQualityBenchmarkId();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9) << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": "
               "\"builtin.method_039.smooth_transition_diagonal_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << profile.RuntimeMilliseconds << ",\n"
            << "    \"memory_peak_bytes\": " << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << profile.MaxVariationOfInformation
            << ",\n"
            << "    \"quality_error_linf\": " << profile.MaxBoundaryErrorNormalized
            << ",\n"
            << "    \"population_count\": 2\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": "
               "\"IntrinsicCurvaturePatchProfile\",\n"
            << "    \"implementation_version\": "
               "\"method_039_local_patch_unadopted\",\n"
            << "    \"candidate_adopted\": false,\n"
            << "    \"feature_evidence\": \"computed\",\n"
            << "    \"seed_mode\": "
               "\"boundary_adjacent_test_override\",\n"
            << "    \"quality_error_l2_unit\": "
               "\"area_weighted_variation_of_information\",\n"
            << "    \"quality_error_linf_unit\": "
               "\"bbox_normalized_boundary_distance_upper_bound\",\n"
            << "    \"aggregation\": \"max_of_diagonal_pair\",\n"
            << "    \"variants\": [";
        for (std::size_t index = 0u; index < profile.Variants.size(); ++index)
        {
            if (index != 0u)
                out << ',';
            const FeaturePatchQualityVariantProfile& variant =
                profile.Variants[index];
            out << "{\"name\":\"" << variant.Name << "\""
                << ",\"face_count\":" << variant.FaceCount
                << ",\"edge_count\":" << variant.EdgeCount
                << ",\"seed_count\":" << variant.SeedCount
                << ",\"final_region_count\":" << variant.FinalRegionCount
                << ",\"soft_boundary_edge_count\":" << variant.SoftBoundaryEdgeCount
                << ",\"closure_boundary_edge_count\":"
                << variant.ClosureBoundaryEdgeCount
                << ",\"variation_of_information\":"
                << variant.VariationOfInformation << ",\"continuous_boundary\":";
            EmitBoundary(out, variant.Boundary);
            out << ",\"stage_timings\":";
            EmitPatchTimings(out, variant.Timings);
            out << ",\"passed\":" << (variant.Succeeded ? "true" : "false") << '}';
        }
        out << "]\n"
            << "  },\n"
            << "  \"status\": \"" << (profile.Succeeded ? "passed" : "failed")
            << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] std::string
    EmitFeaturePatchRefutationResult(const FeaturePatchRefutationProfile& profile,
                                     const std::string& commit)
    {
        const std::string benchmarkId = FeaturePatchRefutationBenchmarkId();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9) << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": "
               "\"builtin.method_039.local_rag_seed_refutation.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << profile.RuntimeMilliseconds << ",\n"
            << "    \"memory_peak_bytes\": " << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": "
            << profile.PerturbedVariationOfInformation << ",\n"
            << "    \"quality_error_linf\": "
            << profile.PerturbedBoundary.SymmetricHausdorffUpperBoundNormalized
            << ",\n"
            << "    \"population_count\": " << profile.PerturbedRegionCount << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": "
               "\"IntrinsicCurvaturePatchProfile\",\n"
            << "    \"implementation_version\": "
               "\"method_039_local_patch_unadopted\",\n"
            << "    \"candidate_adopted\": false,\n"
            << "    \"gate_interpretation\": "
               "\"passed_means_frozen_local_adoption_refutation_reproduced\",\n"
            << "    \"quality_error_l2_unit\": "
               "\"perturbed_area_weighted_variation_of_information\",\n"
            << "    \"quality_error_linf_unit\": "
               "\"perturbed_bbox_normalized_boundary_distance_upper_bound\",\n"
            << "    \"face_count\": " << profile.FaceCount << ",\n"
            << "    \"edge_count\": " << profile.EdgeCount << ",\n"
            << "    \"baseline_seed_count\": " << profile.BaselineSeedCount << ",\n"
            << "    \"perturbed_seed_count\": " << profile.PerturbedSeedCount
            << ",\n"
            << "    \"baseline_region_count\": " << profile.BaselineRegionCount
            << ",\n"
            << "    \"perturbed_region_count\": " << profile.PerturbedRegionCount
            << ",\n"
            << "    \"perturbed_closure_boundary_edge_count\": "
            << profile.PerturbedClosureBoundaryEdgeCount << ",\n"
            << "    \"baseline_variation_of_information\": "
            << profile.BaselineVariationOfInformation << ",\n"
            << "    \"perturbed_boundary\": ";
        EmitBoundary(out, profile.PerturbedBoundary);
        out << ",\n"
            << "    \"baseline_stage_timings\": ";
        EmitPatchTimings(out, profile.BaselineTimings);
        out << ",\n"
            << "    \"perturbed_stage_timings\": ";
        EmitPatchTimings(out, profile.PerturbedTimings);
        out << "\n"
            << "  },\n"
            << "  \"status\": \"" << (profile.Succeeded ? "passed" : "failed")
            << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] std::string
    EmitFeaturePatchHealthResult(const FeaturePatchHealthProfile& profile,
                                 const std::string& commit)
    {
        const std::string benchmarkId = FeaturePatchHealthBenchmarkId();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9) << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": "
               "\"builtin.method_039.homogeneous_plane_100k.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << profile.RuntimeMilliseconds << ",\n"
            << "    \"memory_peak_bytes\": " << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << profile.UnassignedFaceFraction
            << ",\n"
            << "    \"quality_error_linf\": "
            << profile.DeterministicPayloadMismatch << ",\n"
            << "    \"population_count\": " << profile.FinalRegionCount << ",\n"
            << "    \"sample_count\": " << profile.FaceCount << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": "
               "\"IntrinsicCurvaturePatchProfile\",\n"
            << "    \"implementation_version\": "
               "\"method_039_local_patch_unadopted\",\n"
            << "    \"candidate_adopted\": false,\n"
            << "    \"intent\": \"bounded_health_not_performance\",\n"
            << "    \"warmup_iterations\": 0,\n"
            << "    \"measured_iterations\": 2,\n"
            << "    \"quality_error_l2_unit\": "
               "\"unassigned_live_face_fraction\",\n"
            << "    \"quality_error_linf_unit\": "
               "\"deterministic_payload_mismatch_indicator\",\n"
            << "    \"vertex_count\": " << profile.VertexCount << ",\n"
            << "    \"face_count\": " << profile.FaceCount << ",\n"
            << "    \"edge_count\": " << profile.EdgeCount << ",\n"
            << "    \"seed_count\": " << profile.SeedCount << ",\n"
            << "    \"provisional_region_count\": "
            << profile.ProvisionalRegionCount << ",\n"
            << "    \"accepted_merge_count\": " << profile.AcceptedMergeCount
            << ",\n"
            << "    \"final_region_count\": " << profile.FinalRegionCount << ",\n"
            << "    \"final_boundary_edge_count\": "
            << profile.FinalBoundaryEdgeCount << ",\n"
            << "    \"result_storage_entry_count\": "
            << profile.ResultStorageEntryCount << ",\n"
            << "    \"sparse_storage_bound_entries\": "
            << profile.SparseStorageBoundEntries << ",\n"
            << "    \"sparse_storage_bound_satisfied\": "
            << (profile.SparseStorageBoundSatisfied ? "true" : "false") << ",\n"
            << "    \"stage_timings\": ";
        EmitPatchTimings(out, profile.Timings);
        out << "\n"
            << "  },\n"
            << "  \"status\": \"" << (profile.Succeeded ? "passed" : "failed")
            << "\"\n"
            << "}\n";
        return out.str();
    }
    [[nodiscard]] bool WriteRawResult(
        const std::filesystem::path& outputRoot,
        const std::string& benchmarkId,
        const std::string& payload)
    {
        std::error_code error;
        std::filesystem::create_directories(outputRoot, error);
        if (error)
            return false;
        const std::filesystem::path path =
            outputRoot / (benchmarkId + ".json");
        std::ofstream output{path, std::ios::trunc};
        if (!output.is_open())
            return false;
        output << payload;
        return output.good();
    }

    [[nodiscard]] bool
    WriteFeaturePatchFeatureResult(const std::filesystem::path& outputRoot,
                                   const FeaturePatchFeatureProfile& profile,
                                   const std::string& commit)
    {
        return WriteRawResult(outputRoot, FeaturePatchFeatureBenchmarkId(),
                              EmitFeaturePatchFeatureResult(profile, commit));
    }

    [[nodiscard]] bool
    WriteFeaturePatchQualityResult(const std::filesystem::path& outputRoot,
                                   const FeaturePatchQualityProfile& profile,
                                   const std::string& commit)
    {
        return WriteRawResult(outputRoot, FeaturePatchQualityBenchmarkId(),
                              EmitFeaturePatchQualityResult(profile, commit));
    }

    [[nodiscard]] bool
    WriteFeaturePatchRefutationResult(const std::filesystem::path& outputRoot,
                                      const FeaturePatchRefutationProfile& profile,
                                      const std::string& commit)
    {
        return WriteRawResult(outputRoot, FeaturePatchRefutationBenchmarkId(),
                              EmitFeaturePatchRefutationResult(profile, commit));
    }

    [[nodiscard]] bool
    WriteFeaturePatchHealthResult(const std::filesystem::path& outputRoot,
                                  const FeaturePatchHealthProfile& profile,
                                  const std::string& commit)
    {
        return WriteRawResult(outputRoot, FeaturePatchHealthBenchmarkId(),
                              EmitFeaturePatchHealthResult(profile, commit));
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: IntrinsicCurvaturePatchProfile "
                     "<output-directory>\n";
        return 2;
    }

    const char* cohortEnvironment =
        std::getenv("INTRINSIC_CURVATURE_PROFILE_COHORT");
    const std::string_view cohort =
        cohortEnvironment == nullptr ? "feature_patch_smoke" : cohortEnvironment;
    if (cohort != "feature_patch_smoke" && cohort != "feature_patch_health")
    {
        std::cerr << "INTRINSIC_CURVATURE_PROFILE_COHORT must be "
                     "feature_patch_smoke or feature_patch_health\n";
        return 2;
    }

    const std::filesystem::path outputRoot{argv[1]};
    const std::string commit = ResolveCommit();
    if (cohort == "feature_patch_smoke")
    {
        bool allPassed = true;
        const FeaturePatchFeatureProfile feature =
            RunFeaturePatchFeatureSmoke();
        if (!WriteFeaturePatchFeatureResult(outputRoot, feature, commit))
        {
            std::cerr << "failed to write " << FeaturePatchFeatureBenchmarkId()
                      << '\n';
            return 1;
        }
        std::cout << "Wrote " << FeaturePatchFeatureBenchmarkId() << '\n';
        allPassed &= feature.Succeeded;

        const FeaturePatchQualityProfile quality =
            RunFeaturePatchQualitySmoke();
        if (!WriteFeaturePatchQualityResult(outputRoot, quality, commit))
        {
            std::cerr << "failed to write " << FeaturePatchQualityBenchmarkId()
                      << '\n';
            return 1;
        }
        std::cout << "Wrote " << FeaturePatchQualityBenchmarkId() << '\n';
        allPassed &= quality.Succeeded;

        const FeaturePatchRefutationProfile refutation =
            RunFeaturePatchSeedRefutation();
        if (!WriteFeaturePatchRefutationResult(outputRoot, refutation, commit))
        {
            std::cerr << "failed to write "
                      << FeaturePatchRefutationBenchmarkId() << '\n';
            return 1;
        }
        std::cout << "Wrote " << FeaturePatchRefutationBenchmarkId() << '\n';
        allPassed &= refutation.Succeeded;
        return allPassed ? 0 : 1;
    }

    const FeaturePatchHealthProfile profile = RunFeaturePatchHealth100k();
    if (!WriteFeaturePatchHealthResult(outputRoot, profile, commit))
    {
        std::cerr << "failed to write " << FeaturePatchHealthBenchmarkId()
                  << '\n';
        return 1;
    }
    std::cout << "Wrote " << FeaturePatchHealthBenchmarkId() << '\n';
    return profile.Succeeded ? 0 : 1;
}
