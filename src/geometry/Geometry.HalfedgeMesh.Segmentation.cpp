module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.HalfedgeMesh.Segmentation;

import Geometry.Curvature;
import Geometry.GaussianMixture;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;

namespace Geometry::Segmentation
{
    namespace Gmm = Geometry::GaussianMixture;

    namespace
    {
        constexpr double kTiny = 1.0e-12;
        constexpr double kPosteriorFloor = 1.0e-15;
        using ProfileClock = std::chrono::steady_clock;

        [[nodiscard]] double ElapsedMilliseconds(
            const ProfileClock::time_point start) noexcept
        {
            return std::chrono::duration<double, std::milli>(
                ProfileClock::now() - start).count();
        }

        void InitializeResult(
            const HalfedgeMesh::Mesh& mesh,
            SegmentationResult& result)
        {
            SegmentationDiagnostics& diagnostics = result.Diagnostics;
            diagnostics.FaceSlotCount = mesh.FacesSize();
            diagnostics.LiveFaceCount = mesh.FaceCount();
            diagnostics.EdgeSlotCount = mesh.EdgesSize();
            diagnostics.LiveEdgeCount = mesh.EdgeCount();
            result.FaceComponents.assign(mesh.FacesSize(), kInvalidLabel);
            result.FaceRegions.assign(mesh.FacesSize(), kInvalidLabel);
            result.EdgeBoundaries.assign(mesh.EdgesSize(), 0u);
            result.FaceRegionColors.assign(
                mesh.FacesSize(), glm::vec4{0.0f});
            result.EdgeBoundaryColors.assign(
                mesh.EdgesSize(), glm::vec4{0.0f});
        }

        struct FaceSample
        {
            FaceHandle Face{};
            glm::dvec3 Feature{0.0};
            glm::dvec3 NormalizedFeature{0.0};
            glm::dvec3 UnitNormal{0.0};
        };

        struct DualEdge
        {
            std::uint32_t FaceA{0u};
            std::uint32_t FaceB{0u};
            EdgeHandle Edge{};
            double Weight{1.0};
        };

        struct RegionPartition
        {
            std::vector<std::uint32_t> RegionByFace{};
            std::vector<std::vector<std::uint32_t>> Members{};
        };

        struct FittedCandidate
        {
            ModelCandidateDiagnostics Diagnostics{};
            Gmm::FitResult Fit{};
        };

        [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(
            const glm::dvec3& value,
            const std::uint32_t dimension) noexcept
        {
            for (std::uint32_t channel = 0u; channel < dimension; ++channel)
            {
                if (!std::isfinite(value[channel]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] double Median(std::vector<double> values)
        {
            if (values.empty())
                return 0.0;
            const std::size_t middle = values.size() / 2u;
            std::nth_element(
                values.begin(), values.begin() + middle, values.end());
            const double upper = values[middle];
            if ((values.size() & 1u) != 0u)
                return upper;
            const double lower = *std::max_element(
                values.begin(), values.begin() + middle);
            return 0.5 * (lower + upper);
        }

        [[nodiscard]] double RobustScale(
            const std::span<const double> values,
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
            const double rms = std::sqrt(
                squared / static_cast<double>(values.size()));
            return std::isfinite(rms) && rms > kTiny ? rms : 1.0;
        }

        [[nodiscard]] SegmentationStatus BuildFaceSamples(
            const HalfedgeMesh::Mesh& mesh,
            const std::span<const glm::dvec3> faceFeatures,
            const std::uint32_t dimension,
            std::vector<FaceSample>& samples,
            std::vector<std::uint32_t>& faceSlotToSample,
            SegmentationDiagnostics& diagnostics)
        {
            if (faceFeatures.size() != mesh.FacesSize())
                return SegmentationStatus::FeatureCountMismatch;

            faceSlotToSample.assign(mesh.FacesSize(), kInvalidLabel);
            samples.clear();
            samples.reserve(mesh.FaceCount());
            std::array<std::vector<double>, 3u> channelValues{};
            for (std::uint32_t channel = 0u; channel < dimension; ++channel)
                channelValues[channel].reserve(mesh.FaceCount());

            for (const FaceHandle face : mesh.LiveFaces())
            {
                if (mesh.Valence(face) != 3u)
                    return SegmentationStatus::NonTriangleFace;

                std::size_t vertexCount = 0u;
                for (const VertexHandle vertex :
                     mesh.VerticesAroundFace(face))
                {
                    const glm::vec3 position = mesh.Position(vertex);
                    if (!IsFinite(position))
                        return SegmentationStatus::NonFinitePosition;
                    ++vertexCount;
                }
                if (vertexCount != 3u)
                    return SegmentationStatus::NonTriangleFace;

                const glm::dvec3 feature = faceFeatures[face.Index];
                if (!IsFinite(feature, dimension))
                    return SegmentationStatus::NonFiniteFeature;

                glm::dvec3 normal = glm::dvec3(
                    MeshUtils::FaceNormal(mesh, face));
                const double normalLength = glm::length(normal);
                if (!std::isfinite(normalLength) ||
                    normalLength <= kTiny)
                {
                    return SegmentationStatus::DegenerateFace;
                }
                normal /= normalLength;

                faceSlotToSample[face.Index] =
                    static_cast<std::uint32_t>(samples.size());
                samples.push_back(FaceSample{
                    .Face = face,
                    .Feature = feature,
                    .UnitNormal = normal,
                });
                for (std::uint32_t channel = 0u; channel < dimension; ++channel)
                    channelValues[channel].push_back(feature[channel]);
            }

            for (std::uint32_t channel = 0u; channel < dimension; ++channel)
            {
                const FeatureNormalization normalization =
                    ComputeFeatureNormalization(channelValues[channel]);
                diagnostics.FeatureCenter[channel] = normalization.Center;
                diagnostics.FeatureScale[channel] = normalization.Scale;
            }

            for (FaceSample& sample : samples)
            {
                for (std::uint32_t channel = 0u; channel < dimension; ++channel)
                {
                    sample.NormalizedFeature[channel] =
                        (sample.Feature[channel] -
                         diagnostics.FeatureCenter[channel]) /
                        diagnostics.FeatureScale[channel];
                }
                if (!IsFinite(sample.NormalizedFeature, dimension))
                    return SegmentationStatus::NonFiniteFeature;
            }
            return SegmentationStatus::Success;
        }

        [[nodiscard]] std::vector<DualEdge> BuildDualEdges(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<FaceSample>& samples,
            const std::vector<std::uint32_t>& faceSlotToSample,
            const std::uint32_t dimension,
            const double featureSensitivity,
            std::vector<std::vector<std::uint32_t>>& incidentDualEdges)
        {
            std::vector<DualEdge> dualEdges;
            dualEdges.reserve(mesh.EdgeCount());
            incidentDualEdges.assign(samples.size(), {});

            for (const EdgeHandle edge : mesh.LiveEdges())
            {
                const FaceHandle face0 =
                    mesh.Face(mesh.Halfedge(edge, 0u));
                const FaceHandle face1 =
                    mesh.Face(mesh.Halfedge(edge, 1u));
                if (!face0.IsValid() || !face1.IsValid() ||
                    mesh.IsDeleted(face0) || mesh.IsDeleted(face1) ||
                    face0.Index >= faceSlotToSample.size() ||
                    face1.Index >= faceSlotToSample.size())
                {
                    continue;
                }

                const std::uint32_t a = faceSlotToSample[face0.Index];
                const std::uint32_t b = faceSlotToSample[face1.Index];
                if (a == kInvalidLabel || b == kInvalidLabel || a == b)
                    continue;

                double featureJump = 0.0;
                for (std::uint32_t channel = 0u;
                     channel < dimension;
                     ++channel)
                {
                    const double delta =
                        samples[a].NormalizedFeature[channel] -
                        samples[b].NormalizedFeature[channel];
                    featureJump += delta * delta;
                }
                const double cosine = std::clamp(
                    glm::dot(samples[a].UnitNormal,
                             samples[b].UnitNormal),
                    -1.0,
                    1.0);
                const double normalizedAngle =
                    std::acos(cosine) / std::acos(-1.0);
                const double contrast = featureJump +
                    normalizedAngle * normalizedAngle;
                const double weight = std::max(
                    1.0e-9,
                    std::exp(-featureSensitivity * contrast));

                const std::uint32_t dualIndex =
                    static_cast<std::uint32_t>(dualEdges.size());
                dualEdges.push_back(DualEdge{
                    .FaceA = std::min(a, b),
                    .FaceB = std::max(a, b),
                    .Edge = edge,
                    .Weight = weight,
                });
                incidentDualEdges[a].push_back(dualIndex);
                incidentDualEdges[b].push_back(dualIndex);
            }
            return dualEdges;
        }

        [[nodiscard]] std::vector<glm::vec3> BuildGmmPoints(
            const std::vector<FaceSample>& samples,
            const std::uint32_t dimension,
            bool& finite)
        {
            std::vector<glm::vec3> points;
            points.reserve(samples.size());
            finite = true;
            for (const FaceSample& sample : samples)
            {
                glm::vec3 point{0.0f};
                for (std::uint32_t channel = 0u;
                     channel < dimension;
                     ++channel)
                {
                    point[channel] = static_cast<float>(
                        sample.NormalizedFeature[channel]);
                }
                if (!IsFinite(point))
                {
                    finite = false;
                    return {};
                }
                points.push_back(point);
            }
            return points;
        }

        [[nodiscard]] FittedCandidate FitCandidate(
            const std::span<const glm::vec3> points,
            const std::uint32_t componentCount,
            const std::uint32_t dimension,
            const SegmentationParams& params)
        {
            FittedCandidate candidate{};
            candidate.Diagnostics.ComponentCount = componentCount;

            Gmm::FitParams fitParams{};
            fitParams.MaxIterations = params.MaxEmIterations;
            fitParams.RelativeTolerance = params.EmRelativeTolerance;
            fitParams.CovarianceFloor = params.CovarianceFloor;
            fitParams.Seed = params.Seed;
            fitParams.Acceleration = Gmm::AccelerationPolicy::None;
            const ProfileClock::time_point fitStart = ProfileClock::now();
            candidate.Fit = Gmm::FitEM(
                points, componentCount, fitParams);
            candidate.Diagnostics.FitMilliseconds =
                ElapsedMilliseconds(fitStart);
            const Gmm::FitDiagnostics& fit =
                candidate.Fit.Diagnostics;
            candidate.Diagnostics.FitSucceeded = fit.Succeeded();
            candidate.Diagnostics.Converged = fit.Converged;
            candidate.Diagnostics.Iterations = fit.Iterations;
            candidate.Diagnostics.RegularizedCovariances =
                fit.RegularizedCovariances;
            candidate.Diagnostics.FinalLogLikelihood =
                fit.FinalLogLikelihood;
            if (!fit.Succeeded())
                return candidate;

            double squaredResidual = 0.0;
            for (const glm::vec3& point : points)
            {
                const auto responsibilities = Gmm::Responsibilities(
                    candidate.Fit.Mixture, glm::dvec3(point));
                if (!responsibilities || responsibilities->empty())
                {
                    candidate.Diagnostics.FitSucceeded = false;
                    return candidate;
                }
                const auto best = std::max_element(
                    responsibilities->begin(),
                    responsibilities->end());
                const std::size_t component =
                    static_cast<std::size_t>(std::distance(
                        responsibilities->begin(), best));
                const glm::dvec3 mean =
                    candidate.Fit.Mixture.Components[component].Mean;
                double pointResidual = 0.0;
                for (std::uint32_t channel = 0u;
                     channel < dimension;
                     ++channel)
                {
                    const double delta =
                        static_cast<double>(point[channel]) - mean[channel];
                    pointResidual += delta * delta;
                }
                squaredResidual += pointResidual;
            }
            candidate.Diagnostics.NormalizedRmsFit = std::sqrt(
                squaredResidual / static_cast<double>(points.size()));
            candidate.Diagnostics.FitToleranceSatisfied =
                candidate.Diagnostics.NormalizedRmsFit <=
                params.AutomaticFitTolerance;

            // FitEM uses a 3D carrier; model selection counts only the active
            // signal dimensions and their full covariance entries.
            const std::uint32_t parametersPerComponent =
                dimension + dimension * (dimension + 1u) / 2u + 1u;
            const double parameterCount =
                static_cast<double>(
                    parametersPerComponent * componentCount - 1u);
            candidate.Diagnostics.BayesianInformationCriterion =
                -2.0 * fit.FinalLogLikelihood +
                params.AutomaticComplexityWeight * parameterCount *
                    std::log(static_cast<double>(points.size()));
            return candidate;
        }

        [[nodiscard]] std::optional<std::size_t> SelectCandidate(
            const std::vector<FittedCandidate>& candidates,
            const ComponentSelectionMode mode)
        {
            bool anyWithinTolerance = false;
            if (mode == ComponentSelectionMode::Automatic)
            {
                for (const FittedCandidate& candidate : candidates)
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
                    anyWithinTolerance &&
                    !candidate.FitToleranceSatisfied)
                {
                    continue;
                }
                if (!selected.has_value())
                {
                    selected = i;
                    continue;
                }
                const ModelCandidateDiagnostics& current =
                    candidates[*selected].Diagnostics;
                if (candidate.BayesianInformationCriterion <
                        current.BayesianInformationCriterion - kTiny ||
                    (std::abs(
                         candidate.BayesianInformationCriterion -
                         current.BayesianInformationCriterion) <= kTiny &&
                     candidate.ComponentCount < current.ComponentCount))
                {
                    selected = i;
                }
            }
            return selected;
        }

        [[nodiscard]] bool BuildDataCosts(
            const std::span<const glm::vec3> points,
            const Gmm::Model& mixture,
            std::vector<double>& dataCosts,
            std::vector<std::uint32_t>& labels)
        {
            const std::size_t componentCount = mixture.Components.size();
            dataCosts.assign(points.size() * componentCount, 0.0);
            labels.assign(points.size(), 0u);
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                const auto responsibilities = Gmm::Responsibilities(
                    mixture, glm::dvec3(points[i]));
                if (!responsibilities ||
                    responsibilities->size() != componentCount)
                {
                    return false;
                }
                double minimum = std::numeric_limits<double>::infinity();
                std::uint32_t minimumLabel = 0u;
                for (std::size_t component = 0u;
                     component < componentCount;
                     ++component)
                {
                    const double posterior = std::max(
                        (*responsibilities)[component],
                        kPosteriorFloor);
                    const double cost = -std::log(posterior);
                    dataCosts[i * componentCount + component] = cost;
                    if (cost < minimum)
                    {
                        minimum = cost;
                        minimumLabel =
                            static_cast<std::uint32_t>(component);
                    }
                }
                // Remove the per-face common offset. It does not affect label
                // choice but keeps the spatial-weight scale interpretable.
                for (std::size_t component = 0u;
                     component < componentCount;
                     ++component)
                {
                    dataCosts[i * componentCount + component] -= minimum;
                }
                labels[i] = minimumLabel;
            }
            return true;
        }

        [[nodiscard]] double LabelEnergy(
            const std::vector<std::uint32_t>& labels,
            const std::vector<double>& dataCosts,
            const std::size_t componentCount,
            const std::vector<DualEdge>& dualEdges,
            const double spatialWeight)
        {
            double energy = 0.0;
            for (std::size_t i = 0u; i < labels.size(); ++i)
            {
                energy += dataCosts[
                    i * componentCount + labels[i]];
            }
            for (const DualEdge& edge : dualEdges)
            {
                if (labels[edge.FaceA] != labels[edge.FaceB])
                    energy += spatialWeight * edge.Weight;
            }
            return energy;
        }

        void SpatiallyRegularize(
            std::vector<std::uint32_t>& labels,
            const std::vector<double>& dataCosts,
            const std::size_t componentCount,
            const std::vector<DualEdge>& dualEdges,
            const std::vector<std::vector<std::uint32_t>>& incidentDualEdges,
            const SegmentationParams& params,
            SegmentationDiagnostics& diagnostics)
        {
            diagnostics.InitialEnergy = LabelEnergy(
                labels,
                dataCosts,
                componentCount,
                dualEdges,
                params.SpatialWeight);

            for (std::uint32_t iteration = 0u;
                 iteration < params.MaxSpatialIterations;
                 ++iteration)
            {
                std::size_t moved = 0u;
                const bool reverse = (iteration & 1u) != 0u;
                for (std::size_t order = 0u;
                     order < labels.size();
                     ++order)
                {
                    const std::size_t face = reverse
                        ? labels.size() - 1u - order
                        : order;
                    std::uint32_t bestLabel = labels[face];
                    double bestEnergy =
                        dataCosts[face * componentCount + bestLabel];
                    for (const std::uint32_t dualIndex :
                         incidentDualEdges[face])
                    {
                        const DualEdge& edge = dualEdges[dualIndex];
                        const std::uint32_t neighbor =
                            edge.FaceA == face ? edge.FaceB : edge.FaceA;
                        if (labels[neighbor] != bestLabel)
                        {
                            bestEnergy +=
                                params.SpatialWeight * edge.Weight;
                        }
                    }

                    for (std::uint32_t candidate = 0u;
                         candidate < componentCount;
                         ++candidate)
                    {
                        double localEnergy =
                            dataCosts[face * componentCount + candidate];
                        for (const std::uint32_t dualIndex :
                             incidentDualEdges[face])
                        {
                            const DualEdge& edge = dualEdges[dualIndex];
                            const std::uint32_t neighbor =
                                edge.FaceA == face ? edge.FaceB : edge.FaceA;
                            if (labels[neighbor] != candidate)
                            {
                                localEnergy +=
                                    params.SpatialWeight * edge.Weight;
                            }
                        }
                        if (localEnergy < bestEnergy - kTiny ||
                            (std::abs(localEnergy - bestEnergy) <= kTiny &&
                             candidate < bestLabel))
                        {
                            bestEnergy = localEnergy;
                            bestLabel = candidate;
                        }
                    }
                    if (bestLabel != labels[face])
                    {
                        labels[face] = bestLabel;
                        ++moved;
                    }
                }
                diagnostics.SpatialIterations = iteration + 1u;
                diagnostics.SpatialLabelMoves += moved;
                if (moved == 0u)
                    break;
            }
        }

        [[nodiscard]] RegionPartition BuildRegions(
            const std::vector<std::uint32_t>& labels,
            const std::vector<DualEdge>& dualEdges,
            const std::vector<std::vector<std::uint32_t>>& incidentDualEdges)
        {
            RegionPartition result{};
            result.RegionByFace.assign(labels.size(), kInvalidLabel);
            std::vector<std::uint32_t> stack;
            for (std::uint32_t seed = 0u;
                 seed < labels.size();
                 ++seed)
            {
                if (result.RegionByFace[seed] != kInvalidLabel)
                    continue;
                const std::uint32_t region =
                    static_cast<std::uint32_t>(result.Members.size());
                result.Members.emplace_back();
                stack.clear();
                stack.push_back(seed);
                result.RegionByFace[seed] = region;
                while (!stack.empty())
                {
                    const std::uint32_t face = stack.back();
                    stack.pop_back();
                    result.Members.back().push_back(face);
                    for (const std::uint32_t dualIndex :
                         incidentDualEdges[face])
                    {
                        const DualEdge& edge = dualEdges[dualIndex];
                        const std::uint32_t neighbor =
                            edge.FaceA == face ? edge.FaceB : edge.FaceA;
                        if (labels[neighbor] == labels[face] &&
                            result.RegionByFace[neighbor] == kInvalidLabel)
                        {
                            result.RegionByFace[neighbor] = region;
                            stack.push_back(neighbor);
                        }
                    }
                }
                std::sort(
                    result.Members.back().begin(),
                    result.Members.back().end());
            }
            return result;
        }

        void MergeSmallRegions(
            std::vector<std::uint32_t>& labels,
            const std::vector<double>& dataCosts,
            const std::size_t componentCount,
            const std::vector<DualEdge>& dualEdges,
            const std::vector<std::vector<std::uint32_t>>& incidentDualEdges,
            const SegmentationParams& params,
            SegmentationDiagnostics& diagnostics)
        {
            if (params.MinimumRegionFaces <= 1u)
                return;

            for (std::size_t pass = 0u; pass < labels.size(); ++pass)
            {
                const RegionPartition regions = BuildRegions(
                    labels, dualEdges, incidentDualEdges);
                bool merged = false;
                for (std::uint32_t region = 0u;
                     region < regions.Members.size();
                     ++region)
                {
                    const std::vector<std::uint32_t>& members =
                        regions.Members[region];
                    if (members.size() >= params.MinimumRegionFaces)
                        continue;

                    std::vector<std::uint32_t> candidates;
                    for (const std::uint32_t face : members)
                    {
                        for (const std::uint32_t dualIndex :
                             incidentDualEdges[face])
                        {
                            const DualEdge& edge = dualEdges[dualIndex];
                            const std::uint32_t neighbor =
                                edge.FaceA == face ? edge.FaceB : edge.FaceA;
                            if (regions.RegionByFace[neighbor] != region)
                                candidates.push_back(labels[neighbor]);
                        }
                    }
                    std::sort(candidates.begin(), candidates.end());
                    candidates.erase(
                        std::unique(candidates.begin(), candidates.end()),
                        candidates.end());
                    if (candidates.empty())
                        continue;

                    const std::uint32_t currentLabel = labels[members.front()];
                    std::uint32_t bestLabel = currentLabel;
                    double bestDelta = std::numeric_limits<double>::infinity();
                    for (const std::uint32_t candidate : candidates)
                    {
                        double delta = 0.0;
                        for (const std::uint32_t face : members)
                        {
                            delta += dataCosts[
                                face * componentCount + candidate] -
                                dataCosts[
                                    face * componentCount + currentLabel];
                        }
                        for (const DualEdge& edge : dualEdges)
                        {
                            const bool aInside =
                                regions.RegionByFace[edge.FaceA] == region;
                            const bool bInside =
                                regions.RegionByFace[edge.FaceB] == region;
                            if (aInside == bInside)
                                continue;
                            const std::uint32_t outside =
                                aInside ? edge.FaceB : edge.FaceA;
                            const bool beforeCut =
                                currentLabel != labels[outside];
                            const bool afterCut =
                                candidate != labels[outside];
                            delta += params.SpatialWeight * edge.Weight *
                                (static_cast<double>(afterCut) -
                                 static_cast<double>(beforeCut));
                        }
                        if (delta < bestDelta - kTiny ||
                            (std::abs(delta - bestDelta) <= kTiny &&
                             candidate < bestLabel))
                        {
                            bestDelta = delta;
                            bestLabel = candidate;
                        }
                    }
                    if (bestLabel == currentLabel)
                        continue;
                    for (const std::uint32_t face : members)
                        labels[face] = bestLabel;
                    ++diagnostics.SmallRegionsMerged;
                    merged = true;
                    break;
                }
                if (!merged)
                    break;
            }
        }

        [[nodiscard]] glm::vec4 RegionColor(
            const std::uint32_t region) noexcept
        {
            const float hue = std::fmod(
                0.61803398875f * static_cast<float>(region) + 0.08f,
                1.0f);
            constexpr float saturation = 0.68f;
            constexpr float value = 0.92f;
            const float scaled = hue * 6.0f;
            const int sector = static_cast<int>(std::floor(scaled));
            const float fraction = scaled - static_cast<float>(sector);
            const float p = value * (1.0f - saturation);
            const float q = value * (1.0f - saturation * fraction);
            const float t = value *
                (1.0f - saturation * (1.0f - fraction));
            switch (sector % 6)
            {
            case 0: return {value, t, p, 1.0f};
            case 1: return {q, value, p, 1.0f};
            case 2: return {p, value, t, 1.0f};
            case 3: return {p, q, value, 1.0f};
            case 4: return {t, p, value, 1.0f};
            default: return {value, p, q, 1.0f};
            }
        }
    }

    [[nodiscard]] bool
    IsValidMixtureParams(const SegmentationParams& params) noexcept
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

    [[nodiscard]] bool IsValidSegmentationParams(
        const SegmentationParams& params) noexcept
    {
        return IsValidMixtureParams(params) &&
               std::isfinite(params.SpatialWeight) &&
               params.SpatialWeight >= 0.0 &&
               std::isfinite(params.FeatureSensitivity) &&
               params.FeatureSensitivity >= 0.0 &&
               params.MaxSpatialIterations > 0u &&
               params.MinimumRegionFaces > 0u;
    }

    FeatureNormalization ComputeFeatureNormalization(
        const std::span<const double> values)
    {
        const double center = Median(std::vector<double>(values.begin(), values.end()));
        return FeatureNormalization{center, RobustScale(values, center)};
    }

    const char* ToString(const ComponentSelectionMode mode) noexcept
    {
        switch (mode)
        {
        case ComponentSelectionMode::FixedCount: return "fixed_count";
        case ComponentSelectionMode::Automatic: return "automatic";
        }
        return "unknown";
    }

    const char* ToString(const SegmentationStatus status) noexcept
    {
        switch (status)
        {
        case SegmentationStatus::Success: return "success";
        case SegmentationStatus::EmptyMesh: return "empty_mesh";
        case SegmentationStatus::UnsupportedSubmeshView:
            return "unsupported_submesh_view";
        case SegmentationStatus::InvalidParameters:
            return "invalid_parameters";
        case SegmentationStatus::FeatureCountMismatch:
            return "feature_count_mismatch";
        case SegmentationStatus::NonTriangleFace:
            return "non_triangle_face";
        case SegmentationStatus::NonFinitePosition:
            return "non_finite_position";
        case SegmentationStatus::DegenerateFace: return "degenerate_face";
        case SegmentationStatus::NonFiniteFeature:
            return "non_finite_feature";
        case SegmentationStatus::GaussianMixtureFitFailed:
            return "gaussian_mixture_fit_failed";
        case SegmentationStatus::PosteriorEvaluationFailed:
            return "posterior_evaluation_failed";
        case SegmentationStatus::MissingGuideProperty:
            return "missing_guide_property";
        }
        return "unknown";
    }

    SegmentationResult SegmentFaceFeatures(
        const HalfedgeMesh::Mesh& mesh,
        const std::span<const glm::dvec3> faceFeatures,
        const std::uint32_t dimension,
        const SegmentationParams& params)
    {
        SegmentationResult result{};
        SegmentationDiagnostics& diagnostics = result.Diagnostics;
        const ProfileClock::time_point totalStart = ProfileClock::now();
        const auto finish = [&]() -> SegmentationResult
        {
            diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return std::move(result);
        };
        InitializeResult(mesh, result);
        diagnostics.FeatureDimension = dimension;

        if (mesh.IsEmpty() || mesh.FaceCount() == 0u)
        {
            diagnostics.Status = SegmentationStatus::EmptyMesh;
            return finish();
        }
        if (mesh.IsSubmeshView())
        {
            diagnostics.Status =
                SegmentationStatus::UnsupportedSubmeshView;
            return finish();
        }
        if (dimension < 1u || dimension > 3u ||
            !IsValidSegmentationParams(params))
        {
            diagnostics.Status = SegmentationStatus::InvalidParameters;
            return finish();
        }

        std::vector<FaceSample> samples;
        std::vector<std::uint32_t> faceSlotToSample;
        const ProfileClock::time_point aggregationStart =
            ProfileClock::now();
        diagnostics.Status = BuildFaceSamples(
            mesh,
            faceFeatures,
            dimension,
            samples,
            faceSlotToSample,
            diagnostics);
        diagnostics.Timings.FaceAggregationAndNormalizationMilliseconds =
            ElapsedMilliseconds(aggregationStart);
        if (diagnostics.Status != SegmentationStatus::Success)
            return finish();

        const std::uint32_t faceCount =
            static_cast<std::uint32_t>(samples.size());
        std::uint32_t firstComponentCount = params.FixedComponentCount;
        std::uint32_t lastComponentCount = params.FixedComponentCount;
        if (params.SelectionMode == ComponentSelectionMode::Automatic)
        {
            firstComponentCount = params.AutomaticMinComponents;
            lastComponentCount = std::min(
                params.AutomaticMaxComponents, faceCount);
        }
        if (firstComponentCount == 0u ||
            firstComponentCount > lastComponentCount ||
            firstComponentCount > faceCount ||
            lastComponentCount > faceCount)
        {
            diagnostics.Status = SegmentationStatus::InvalidParameters;
            return finish();
        }
        diagnostics.RequestedComponentCount =
            params.SelectionMode == ComponentSelectionMode::FixedCount
                ? params.FixedComponentCount
                : 0u;

        bool finiteGmmPoints = false;
        const std::vector<glm::vec3> points = BuildGmmPoints(
            samples, dimension, finiteGmmPoints);
        if (!finiteGmmPoints)
        {
            diagnostics.Status = SegmentationStatus::NonFiniteFeature;
            return finish();
        }

        std::vector<FittedCandidate> candidates;
        candidates.reserve(
            lastComponentCount - firstComponentCount + 1u);
        const ProfileClock::time_point fittingStart = ProfileClock::now();
        for (std::uint32_t componentCount = firstComponentCount;
             componentCount <= lastComponentCount;
             ++componentCount)
        {
            candidates.push_back(FitCandidate(
                std::span<const glm::vec3>{points.data(), points.size()},
                componentCount,
                dimension,
                params));
        }
        diagnostics.Timings.GmmFittingMilliseconds =
            ElapsedMilliseconds(fittingStart);
        const std::optional<std::size_t> selected = SelectCandidate(
            candidates, params.SelectionMode);
        if (!selected.has_value())
        {
            for (const FittedCandidate& candidate : candidates)
                diagnostics.Candidates.push_back(candidate.Diagnostics);
            diagnostics.Status =
                SegmentationStatus::GaussianMixtureFitFailed;
            return finish();
        }
        candidates[*selected].Diagnostics.Selected = true;
        for (const FittedCandidate& candidate : candidates)
            diagnostics.Candidates.push_back(candidate.Diagnostics);

        const FittedCandidate& chosen = candidates[*selected];
        diagnostics.SelectedComponentCount =
            chosen.Diagnostics.ComponentCount;
        diagnostics.AutomaticFitToleranceSatisfied =
            chosen.Diagnostics.FitToleranceSatisfied;
        diagnostics.GmmConverged = chosen.Diagnostics.Converged;
        diagnostics.GmmIterations = chosen.Diagnostics.Iterations;
        diagnostics.GmmRegularizedCovariances =
            chosen.Diagnostics.RegularizedCovariances;
        diagnostics.GmmFinalLogLikelihood =
            chosen.Diagnostics.FinalLogLikelihood;
        diagnostics.NormalizedRmsFit =
            chosen.Diagnostics.NormalizedRmsFit;

        for (std::uint32_t component = 0u;
             component < chosen.Fit.Mixture.Components.size();
             ++component)
        {
            const Gmm::MultivariateGaussian& gaussian =
                chosen.Fit.Mixture.Components[component];
            CurvatureComponentSummary summary{
                .Component = component,
                .Weight = chosen.Fit.Mixture.Weights[component],
            };
            for (std::uint32_t channel = 0u;
                 channel < dimension;
                 ++channel)
            {
                summary.NormalizedFeatureMean[channel] =
                    gaussian.Mean[channel];
                summary.FeatureMean[channel] =
                    diagnostics.FeatureCenter[channel] +
                    diagnostics.FeatureScale[channel] *
                        gaussian.Mean[channel];
            }
            diagnostics.Components.push_back(summary);
        }

        std::vector<double> dataCosts;
        std::vector<std::uint32_t> labels;
        const ProfileClock::time_point unaryStart = ProfileClock::now();
        if (!BuildDataCosts(
                std::span<const glm::vec3>{points.data(), points.size()},
                chosen.Fit.Mixture,
                dataCosts,
                labels))
        {
            diagnostics.Timings.UnaryConstructionMilliseconds =
                ElapsedMilliseconds(unaryStart);
            diagnostics.Status =
                SegmentationStatus::PosteriorEvaluationFailed;
            return finish();
        }
        diagnostics.Timings.UnaryConstructionMilliseconds =
            ElapsedMilliseconds(unaryStart);

        std::vector<std::vector<std::uint32_t>> incidentDualEdges;
        const ProfileClock::time_point dualGraphStart = ProfileClock::now();
        const std::vector<DualEdge> dualEdges = BuildDualEdges(
            mesh,
            samples,
            faceSlotToSample,
            dimension,
            params.FeatureSensitivity,
            incidentDualEdges);
        diagnostics.DualEdgeCount = dualEdges.size();
        diagnostics.Timings.DualGraphConstructionMilliseconds =
            ElapsedMilliseconds(dualGraphStart);

        const ProfileClock::time_point spatialStart = ProfileClock::now();
        SpatiallyRegularize(
            labels,
            dataCosts,
            chosen.Fit.Mixture.Components.size(),
            dualEdges,
            incidentDualEdges,
            params,
            diagnostics);
        diagnostics.Timings.SpatialOptimizationMilliseconds =
            ElapsedMilliseconds(spatialStart);

        const ProfileClock::time_point connectivityStart =
            ProfileClock::now();
        MergeSmallRegions(
            labels,
            dataCosts,
            chosen.Fit.Mixture.Components.size(),
            dualEdges,
            incidentDualEdges,
            params,
            diagnostics);
        diagnostics.FinalEnergy = LabelEnergy(
            labels,
            dataCosts,
            chosen.Fit.Mixture.Components.size(),
            dualEdges,
            params.SpatialWeight);

        const RegionPartition regions = BuildRegions(
            labels, dualEdges, incidentDualEdges);
        diagnostics.ConnectedRegionCount =
            static_cast<std::uint32_t>(regions.Members.size());

        std::vector<std::uint8_t> active(
            chosen.Fit.Mixture.Components.size(), 0u);
        for (std::size_t sample = 0u; sample < samples.size(); ++sample)
        {
            const FaceHandle face = samples[sample].Face;
            const std::uint32_t component = labels[sample];
            const std::uint32_t region = regions.RegionByFace[sample];
            result.FaceComponents[face.Index] = component;
            result.FaceRegions[face.Index] = region;
            result.FaceRegionColors[face.Index] = RegionColor(region);
            active[component] = 1u;
        }
        diagnostics.ActiveComponentCount =
            static_cast<std::uint32_t>(std::count(
                active.begin(), active.end(), std::uint8_t{1u}));

        constexpr glm::vec4 kBoundaryColor{
            1.0f, 0.08f, 0.02f, 1.0f};
        for (const DualEdge& edge : dualEdges)
        {
            if (regions.RegionByFace[edge.FaceA] ==
                regions.RegionByFace[edge.FaceB])
            {
                continue;
            }
            result.EdgeBoundaries[edge.Edge.Index] = 1u;
            result.EdgeBoundaryColors[edge.Edge.Index] = kBoundaryColor;
            ++diagnostics.BoundaryEdgeCount;
        }

        diagnostics.Status = SegmentationStatus::Success;
        diagnostics.Timings.ConnectivityCleanupAndPublicationMilliseconds =
            ElapsedMilliseconds(connectivityStart);
        return finish();
    }

    SegmentationResult Segment(
        const HalfedgeMesh::Mesh& mesh,
        const std::span<const Guide> guides,
        const SegmentationParams& params)
    {
        const ProfileClock::time_point totalStart = ProfileClock::now();
        const auto dimension = static_cast<std::uint32_t>(guides.size());
        const bool sized = std::ranges::all_of(guides, [&](const Guide& guide)
        {
            return guide.Values.size() == (guide.Domain == GuideDomain::Vertex
                                               ? mesh.VerticesSize()
                                               : mesh.FacesSize());
        });
        if (mesh.IsEmpty() || mesh.FaceCount() == 0u ||
            mesh.IsSubmeshView() || !IsValidSegmentationParams(params) ||
            dimension < 1u || dimension > 3u || !sized)
        {
            // Dimension 1..3 with an empty feature span reports the size
            // mismatch; other rejections keep their own status.
            SegmentationResult result = SegmentFaceFeatures(
                mesh, {}, dimension, params);
            result.Diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return result;
        }

        const ProfileClock::time_point aggregationStart = ProfileClock::now();
        const auto reject = [&](const SegmentationStatus status)
        {
            SegmentationResult result{};
            InitializeResult(mesh, result);
            result.Diagnostics.FeatureDimension = dimension;
            result.Diagnostics.Status = status;
            result.Diagnostics.Timings
                .FaceAggregationAndNormalizationMilliseconds =
                ElapsedMilliseconds(aggregationStart);
            result.Diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return result;
        };
        std::vector<glm::dvec3> faceFeatures(
            mesh.FacesSize(), glm::dvec3{0.0});
        for (const FaceHandle face : mesh.LiveFaces())
        {
            if (mesh.Valence(face) != 3u)
                return reject(SegmentationStatus::NonTriangleFace);

            glm::dvec3 sum{0.0};
            std::size_t vertexCount = 0u;
            for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
            {
                if (!IsFinite(mesh.Position(vertex)))
                    return reject(SegmentationStatus::NonFinitePosition);
                // Corner order decides between position and guide failures.
                for (std::uint32_t channel = 0u; channel < dimension; ++channel)
                {
                    if (guides[channel].Domain != GuideDomain::Vertex)
                        continue;
                    const double value = guides[channel].Values[vertex.Index];
                    if (!std::isfinite(value))
                        return reject(SegmentationStatus::NonFiniteFeature);
                    sum[channel] += value;
                }
                ++vertexCount;
            }
            if (vertexCount != 3u)
                return reject(SegmentationStatus::NonTriangleFace);

            const glm::dvec3 normal{
                MeshUtils::FaceNormal(mesh, face)};
            const double normalLength = glm::length(normal);
            if (!std::isfinite(normalLength) || normalLength <= kTiny)
                return reject(SegmentationStatus::DegenerateFace);

            for (std::uint32_t channel = 0u; channel < dimension; ++channel)
                faceFeatures[face.Index][channel] =
                    guides[channel].Domain == GuideDomain::Vertex
                        ? sum[channel] / static_cast<double>(vertexCount)
                        : guides[channel].Values[face.Index];
            if (!IsFinite(faceFeatures[face.Index], dimension))
                return reject(SegmentationStatus::NonFiniteFeature);
        }
        const double aggregationMilliseconds =
            ElapsedMilliseconds(aggregationStart);

        SegmentationResult result = SegmentFaceFeatures(
            mesh, faceFeatures, dimension, params);
        result.Diagnostics.Timings
            .FaceAggregationAndNormalizationMilliseconds +=
            aggregationMilliseconds;
        result.Diagnostics.Timings.TotalMilliseconds =
            ElapsedMilliseconds(totalStart);
        return result;
    }

    SegmentationResult Segment(
        const HalfedgeMesh::Mesh& mesh,
        const std::span<const GuideProperty> guides,
        const SegmentationParams& params)
    {
        std::vector<std::vector<double>> channels;
        std::vector<GuideDomain> domains;
        const auto add = [&]<typename T>(const auto& properties,
                                         const GuideProperty& guide) -> bool
        {
            const auto property = properties.template Get<T>(guide.Name);
            if (!property)
                return false;
            std::size_t width = 1u;
            if constexpr (!std::is_floating_point_v<T>)
                width = static_cast<std::size_t>(T::length());
            for (std::size_t c = 0u; c < width; ++c)
            {
                auto& channel = channels.emplace_back();
                channel.reserve(property.Vector().size());
                for (const T& value : property.Vector())
                {
                    if constexpr (std::is_floating_point_v<T>)
                        channel.push_back(static_cast<double>(value));
                    else
                        channel.push_back(static_cast<double>(value[static_cast<int>(c)]));
                }
                domains.push_back(guide.Domain);
            }
            return true;
        };
        for (const GuideProperty& guide : guides)
        {
            const auto properties = guide.Domain == GuideDomain::Vertex
                                        ? mesh.VertexProperties()
                                        : mesh.FaceProperties();
            if (!add.template operator()<double>(properties, guide) &&
                !add.template operator()<float>(properties, guide) &&
                !add.template operator()<glm::vec2>(properties, guide) &&
                !add.template operator()<glm::vec3>(properties, guide))
            {
                SegmentationResult result{};
                InitializeResult(mesh, result);
                result.Diagnostics.Status = SegmentationStatus::MissingGuideProperty;
                return result;
            }
        }
        std::vector<Guide> spans;
        for (std::size_t i = 0u; i < channels.size(); ++i)
            spans.push_back({domains[i], channels[i]});
        return Segment(mesh, std::span<const Guide>{spans}, params);
    }

    SegmentationResult SegmentCurvature(
        const HalfedgeMesh::Mesh& mesh,
        const std::span<const double> maxPrincipal,
        const std::span<const double> minPrincipal,
        const SegmentationParams& params)
    {
        const std::array<Guide, 2u> guides{
            Guide{GuideDomain::Vertex, maxPrincipal},
            Guide{GuideDomain::Vertex, minPrincipal}};
        return Segment(mesh, std::span<const Guide>{guides}, params);
    }

    SegmentationResult ComputeAndSegment(
        HalfedgeMesh::Mesh& mesh,
        const SegmentationParams& params)
    {
        const ProfileClock::time_point totalStart = ProfileClock::now();
        if (mesh.IsEmpty() || mesh.FaceCount() == 0u)
        {
            SegmentationResult result{};
            result.Diagnostics.Status = SegmentationStatus::EmptyMesh;
            result.Diagnostics.FeatureDimension = 2u;
            result.Diagnostics.FaceSlotCount = mesh.FacesSize();
            result.Diagnostics.LiveFaceCount = mesh.FaceCount();
            result.Diagnostics.EdgeSlotCount = mesh.EdgesSize();
            result.Diagnostics.LiveEdgeCount = mesh.EdgeCount();
            result.Diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return result;
        }
        const ProfileClock::time_point curvatureStart =
            ProfileClock::now();
        Curvature::CurvatureField curvature =
            Curvature::ComputeCurvature(mesh);
        const double curvatureMilliseconds =
            ElapsedMilliseconds(curvatureStart);
        if (!curvature.MaxPrincipalCurvatureProperty ||
            !curvature.MinPrincipalCurvatureProperty)
        {
            SegmentationResult result{};
            result.Diagnostics.Status =
                SegmentationStatus::FeatureCountMismatch;
            result.Diagnostics.FeatureDimension = 2u;
            result.Diagnostics.Timings.CurvatureEstimationMilliseconds =
                curvatureMilliseconds;
            result.Diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return result;
        }
        const std::vector<double>& maximum =
            curvature.MaxPrincipalCurvatureProperty.Vector();
        const std::vector<double>& minimum =
            curvature.MinPrincipalCurvatureProperty.Vector();
        SegmentationResult result = SegmentCurvature(
            mesh,
            std::span<const double>{maximum.data(), maximum.size()},
            std::span<const double>{minimum.data(), minimum.size()},
            params);
        result.Diagnostics.Timings.CurvatureEstimationMilliseconds =
            curvatureMilliseconds;
        result.Diagnostics.Timings.TotalMilliseconds =
            ElapsedMilliseconds(totalStart);
        return result;
    }
}
