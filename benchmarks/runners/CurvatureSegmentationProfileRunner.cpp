// METHOD-038 — opt-in METHOD-037 baseline stage profiler.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/resource.h>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.Curvature;
import Geometry.HalfedgeMesh.CurvatureSegmentation;
import Geometry.Properties;

namespace
{
    namespace Segment = Geometry::CurvatureSegmentation;
    using ProfileClock = std::chrono::steady_clock;

    struct CohortSpec
    {
        std::string_view Token{};
        std::uint32_t Rows{0u};
        std::uint32_t Columns{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
    };

    enum class SelectionMode : std::uint8_t
    {
        Fixed,
        Automatic,
    };

    enum class DescriptorLane : std::uint8_t
    {
        Cold,
        Reusable,
    };

    struct Fixture
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<double> K1{};
        std::vector<double> K2{};
        std::vector<std::uint32_t> ExpectedFaceRegime{};
        bool Valid{true};
    };

    struct CandidateProfile
    {
        std::uint32_t ComponentCount{0u};
        std::uint32_t Iterations{0u};
        double FitMilliseconds{0.0};
        bool Selected{false};
    };

    struct BoundaryProfile
    {
        bool Valid{false};
        std::size_t EdgeCount{0u};
        std::size_t EndpointCount{0u};
        std::size_t JunctionCount{0u};
        std::uint32_t ReferenceSampleCount{0u};
        double ReferenceSampleSpacingNormalized{0.0};
        double SymmetricHausdorffUpperBoundNormalized{1.0};
        double ToleranceBandPrecision{0.0};
        double ToleranceBandRecall{0.0};
        double PredictedLengthNormalized{0.0};
        double ReferenceLengthNormalized{0.0};
    };

    struct VariantProfile
    {
        std::string_view Name{};
        bool Succeeded{false};
        std::size_t FaceCount{0u};
        std::size_t DualEdgeCount{0u};
        std::uint32_t SelectedComponentCount{0u};
        std::uint32_t SpatialIterations{0u};
        double MisclassifiedFaceFraction{1.0};
        double DescriptorSetupMilliseconds{0.0};
        Segment::CurvatureSegmentationStageTimings MedianTimings{};
        std::vector<CandidateProfile> Candidates{};
        BoundaryProfile Boundary{};
    };

    struct CohortProfile
    {
        CohortSpec Spec{};
        SelectionMode Mode{SelectionMode::Fixed};
        VariantProfile Uniform{};
        VariantProfile Anisotropic{};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    struct DescriptorPairProfile
    {
        CohortSpec Spec{};
        SelectionMode Mode{SelectionMode::Fixed};
        DescriptorLane Lane{DescriptorLane::Cold};
        VariantProfile DiagonalA{};
        VariantProfile DiagonalB{};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    struct TimingSamples
    {
        std::vector<double> Curvature{};
        std::vector<double> Aggregation{};
        std::vector<double> Fitting{};
        std::vector<double> Unary{};
        std::vector<double> DualGraph{};
        std::vector<double> Spatial{};
        std::vector<double> Connectivity{};
        std::vector<double> Total{};

        void Reserve(const std::size_t count)
        {
            Curvature.reserve(count);
            Aggregation.reserve(count);
            Fitting.reserve(count);
            Unary.reserve(count);
            DualGraph.reserve(count);
            Spatial.reserve(count);
            Connectivity.reserve(count);
            Total.reserve(count);
        }

        void Add(const Segment::CurvatureSegmentationStageTimings& timings)
        {
            Curvature.push_back(timings.CurvatureEstimationMilliseconds);
            Aggregation.push_back(
                timings.FaceAggregationAndNormalizationMilliseconds);
            Fitting.push_back(timings.GmmFittingMilliseconds);
            Unary.push_back(timings.UnaryConstructionMilliseconds);
            DualGraph.push_back(timings.DualGraphConstructionMilliseconds);
            Spatial.push_back(timings.SpatialOptimizationMilliseconds);
            Connectivity.push_back(
                timings.ConnectivityCleanupAndPublicationMilliseconds);
            Total.push_back(timings.TotalMilliseconds);
        }
    };

    [[nodiscard]] std::string ResolveCommit()
    {
        const char* value = std::getenv("GIT_COMMIT");
        return value != nullptr && value[0] != '\0'
            ? std::string{value}
            : std::string{"unknown"};
    }

    [[nodiscard]] std::uint64_t PeakWorkingSetBytes() noexcept
    {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0)
            return 0u;
        // Linux reports ru_maxrss in KiB.
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
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

    [[nodiscard]] double ElapsedMilliseconds(
        const ProfileClock::time_point start) noexcept
    {
        return std::chrono::duration<double, std::milli>(
            ProfileClock::now() - start).count();
    }

    [[nodiscard]] Segment::CurvatureSegmentationStageTimings MedianTimings(
        TimingSamples samples)
    {
        Segment::CurvatureSegmentationStageTimings timings{};
        timings.CurvatureEstimationMilliseconds =
            Median(std::move(samples.Curvature));
        timings.FaceAggregationAndNormalizationMilliseconds =
            Median(std::move(samples.Aggregation));
        timings.GmmFittingMilliseconds =
            Median(std::move(samples.Fitting));
        timings.UnaryConstructionMilliseconds =
            Median(std::move(samples.Unary));
        timings.DualGraphConstructionMilliseconds =
            Median(std::move(samples.DualGraph));
        timings.SpatialOptimizationMilliseconds =
            Median(std::move(samples.Spatial));
        timings.ConnectivityCleanupAndPublicationMilliseconds =
            Median(std::move(samples.Connectivity));
        timings.TotalMilliseconds = Median(std::move(samples.Total));
        return timings;
    }

    void CopyResultDiagnostics(
        VariantProfile& profile,
        const Segment::CurvatureSegmentationResult& result)
    {
        profile.DualEdgeCount = result.Diagnostics.DualEdgeCount;
        profile.SelectedComponentCount =
            result.Diagnostics.SelectedComponentCount;
        profile.SpatialIterations = result.Diagnostics.SpatialIterations;
        for (const auto& candidate : result.Diagnostics.Candidates)
        {
            profile.Candidates.push_back(CandidateProfile{
                .ComponentCount = candidate.ComponentCount,
                .Iterations = candidate.Iterations,
                .FitMilliseconds = candidate.FitMilliseconds,
                .Selected = candidate.Selected,
            });
        }
    }

    [[nodiscard]] glm::dvec3 ToDouble(const glm::vec3 value) noexcept
    {
        return {
            static_cast<double>(value.x),
            static_cast<double>(value.y),
            static_cast<double>(value.z),
        };
    }

    [[nodiscard]] double PointSegmentDistance(
        const glm::dvec3& point,
        const glm::dvec3& a,
        const glm::dvec3& b) noexcept
    {
        const glm::dvec3 edge = b - a;
        const double lengthSquared = glm::dot(edge, edge);
        if (!(lengthSquared > 0.0) || !std::isfinite(lengthSquared))
            return glm::length(point - a);
        const double parameter = std::clamp(
            glm::dot(point - a, edge) / lengthSquared, 0.0, 1.0);
        return glm::length(point - (a + parameter * edge));
    }

    [[nodiscard]] double SegmentFractionInsideVerticalBand(
        const glm::dvec3& a,
        const glm::dvec3& b,
        const double center,
        const double halfWidth) noexcept
    {
        const double x0 = a.x - center;
        const double delta = b.x - a.x;
        if (std::abs(delta) <= std::numeric_limits<double>::epsilon())
            return std::abs(x0) <= halfWidth ? 1.0 : 0.0;
        const double first = (-halfWidth - x0) / delta;
        const double second = (halfWidth - x0) / delta;
        const double lower = std::max(0.0, std::min(first, second));
        const double upper = std::min(1.0, std::max(first, second));
        return std::max(0.0, upper - lower);
    }

    struct BoundarySegment
    {
        glm::dvec3 A{};
        glm::dvec3 B{};
        double Length{0.0};
    };

    [[nodiscard]] BoundaryProfile MeasureUnitSquareTransitionBoundary(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const std::vector<std::uint8_t>& edgeBoundaries)
    {
        constexpr std::uint32_t referenceSampleCount = 4097u;
        constexpr double normalizedTolerance = 0.02;
        constexpr double referenceX = 0.5;
        const double diagonal = std::sqrt(2.0);
        const double tolerance = normalizedTolerance * diagonal;
        const glm::dvec3 referenceA{referenceX, 0.0, 0.0};
        const glm::dvec3 referenceB{referenceX, 1.0, 0.0};

        BoundaryProfile profile{};
        profile.ReferenceSampleCount = referenceSampleCount;
        profile.ReferenceSampleSpacingNormalized =
            1.0 / static_cast<double>(referenceSampleCount - 1u) /
            diagonal;
        profile.ReferenceLengthNormalized = 1.0 / diagonal;

        std::vector<BoundarySegment> segments;
        std::vector<std::uint32_t> degree(mesh.VerticesSize(), 0u);
        double predictedLength = 0.0;
        double inBandLength = 0.0;
        double predictedToReference = 0.0;
        for (const Geometry::EdgeHandle edge : mesh.LiveEdges())
        {
            if (edge.Index >= edgeBoundaries.size() ||
                edgeBoundaries[edge.Index] == 0u)
            {
                continue;
            }
            const Geometry::HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const Geometry::VertexHandle from = mesh.FromVertex(halfedge);
            const Geometry::VertexHandle to = mesh.ToVertex(halfedge);
            const glm::dvec3 a = ToDouble(mesh.Position(from));
            const glm::dvec3 b = ToDouble(mesh.Position(to));
            const double length = glm::length(b - a);
            if (!(length > 0.0) || !std::isfinite(length))
                continue;
            segments.push_back(BoundarySegment{a, b, length});
            predictedLength += length;
            inBandLength += length * SegmentFractionInsideVerticalBand(
                a, b, referenceX, tolerance);
            predictedToReference = std::max(
                predictedToReference,
                std::max(
                    PointSegmentDistance(a, referenceA, referenceB),
                    PointSegmentDistance(b, referenceA, referenceB)));
            ++degree[from.Index];
            ++degree[to.Index];
        }

        profile.EdgeCount = segments.size();
        profile.PredictedLengthNormalized = predictedLength / diagonal;
        if (segments.empty())
            return profile;

        double sampledReferenceToPredicted = 0.0;
        std::uint32_t coveredSamples = 0u;
        for (std::uint32_t sample = 0u;
             sample < referenceSampleCount;
             ++sample)
        {
            const double t = static_cast<double>(sample) /
                static_cast<double>(referenceSampleCount - 1u);
            const glm::dvec3 point{referenceX, t, 0.0};
            double nearest = std::numeric_limits<double>::infinity();
            for (const BoundarySegment& segment : segments)
            {
                nearest = std::min(
                    nearest,
                    PointSegmentDistance(point, segment.A, segment.B));
            }
            sampledReferenceToPredicted = std::max(
                sampledReferenceToPredicted, nearest);
            coveredSamples += nearest <= tolerance ? 1u : 0u;
        }

        // Distance to a fixed set is 1-Lipschitz. Adding half the reference
        // sample spacing turns the sampled directed distance into a declared
        // upper bound for every point on the exact continuous line segment.
        const double referenceSpacing =
            1.0 / static_cast<double>(referenceSampleCount - 1u);
        profile.SymmetricHausdorffUpperBoundNormalized = std::max(
            predictedToReference,
            sampledReferenceToPredicted + 0.5 * referenceSpacing) /
            diagonal;
        profile.ToleranceBandPrecision =
            inBandLength / predictedLength;
        profile.ToleranceBandRecall =
            static_cast<double>(coveredSamples) /
            static_cast<double>(referenceSampleCount);
        for (const std::uint32_t value : degree)
        {
            profile.EndpointCount += value == 1u ? 1u : 0u;
            profile.JunctionCount += value > 2u ? 1u : 0u;
        }
        profile.Valid = true;
        return profile;
    }

    [[nodiscard]] Fixture MakeGridFixture(
        const CohortSpec& spec,
        const bool anisotropic)
    {
        Fixture fixture{};
        const std::size_t vertexCount =
            static_cast<std::size_t>(spec.Rows + 1u) *
            static_cast<std::size_t>(spec.Columns + 1u);
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(spec.Rows) *
            static_cast<std::size_t>(spec.Columns);
        fixture.Mesh.Reserve(vertexCount, 2u * faceCount, faceCount);
        fixture.K1.resize(vertexCount);
        fixture.K2.resize(vertexCount);

        std::vector<Geometry::VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= spec.Rows; ++row)
        {
            const double v = static_cast<double>(row) /
                static_cast<double>(spec.Rows);
            for (std::uint32_t column = 0u;
                 column <= spec.Columns;
                 ++column)
            {
                const double u = static_cast<double>(column) /
                    static_cast<double>(spec.Columns);
                // Both variants sample the same unit square. Squared u spacing
                // creates a nonuniform/anisotropic triangulation without
                // changing the embedded reference surface.
                const double x = anisotropic ? u * u : u;
                const Geometry::VertexHandle vertex =
                    fixture.Mesh.AddVertex(glm::vec3{
                        static_cast<float>(x),
                        static_cast<float>(v),
                        0.0f,
                    });
                vertices.push_back(vertex);
                const bool rightRegime = x >= 0.5;
                fixture.K1[vertex.Index] = rightRegime ? 3.0 : -2.0;
                fixture.K2[vertex.Index] = rightRegime ? 1.0 : -4.0;
            }
        }

        fixture.ExpectedFaceRegime.reserve(faceCount);
        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(spec.Columns + 1u) +
                column];
        };
        const auto addFace = [&](const Geometry::VertexHandle a,
                                 const Geometry::VertexHandle b,
                                 const Geometry::VertexHandle c)
        {
            const auto face = fixture.Mesh.AddTriangle(a, b, c);
            if (!face.has_value())
            {
                fixture.Valid = false;
                return;
            }
            if (fixture.ExpectedFaceRegime.size() <= face->Index)
            {
                fixture.ExpectedFaceRegime.resize(
                    static_cast<std::size_t>(face->Index) + 1u, 0u);
            }
            const double centroidX =
                (static_cast<double>(fixture.Mesh.Position(a).x) +
                 static_cast<double>(fixture.Mesh.Position(b).x) +
                 static_cast<double>(fixture.Mesh.Position(c).x)) /
                3.0;
            fixture.ExpectedFaceRegime[face->Index] =
                centroidX >= 0.5 ? 1u : 0u;
        };

        for (std::uint32_t row = 0u; row < spec.Rows; ++row)
        {
            for (std::uint32_t column = 0u;
                 column < spec.Columns;
                 ++column)
            {
                const Geometry::VertexHandle v00 = vertexAt(row, column);
                const Geometry::VertexHandle v10 =
                    vertexAt(row, column + 1u);
                const Geometry::VertexHandle v01 =
                    vertexAt(row + 1u, column);
                const Geometry::VertexHandle v11 =
                    vertexAt(row + 1u, column + 1u);
                const bool alternate =
                    ((row + column + (anisotropic ? 1u : 0u)) & 1u) != 0u;
                if (alternate)
                {
                    addFace(v00, v10, v01);
                    addFace(v10, v11, v01);
                }
                else
                {
                    addFace(v00, v10, v11);
                    addFace(v00, v11, v01);
                }
                if (!fixture.Valid)
                    return fixture;
            }
        }
        fixture.Valid &= fixture.Mesh.FaceCount() == faceCount;
        return fixture;
    }

    [[nodiscard]] Fixture MakeSphereFixture(
        const CohortSpec& spec,
        const bool flippedDiagonals)
    {
        Fixture fixture{};
        if (spec.Rows == 0u || spec.Columns < 3u)
        {
            fixture.Valid = false;
            return fixture;
        }
        const std::size_t vertexCount = 2u +
            static_cast<std::size_t>(spec.Rows) * spec.Columns;
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(spec.Rows) * spec.Columns;
        fixture.Mesh.Reserve(vertexCount, 2u * faceCount, faceCount);

        const Geometry::VertexHandle north =
            fixture.Mesh.AddVertex({0.0f, 0.0f, 1.0f});
        std::vector<Geometry::VertexHandle> rings;
        rings.reserve(static_cast<std::size_t>(spec.Rows) * spec.Columns);
        for (std::uint32_t ring = 0u; ring < spec.Rows; ++ring)
        {
            const double phi = std::numbers::pi_v<double> *
                static_cast<double>(ring + 1u) /
                static_cast<double>(spec.Rows + 1u);
            const double radial = std::sin(phi);
            const double z = std::cos(phi);
            for (std::uint32_t column = 0u;
                 column < spec.Columns;
                 ++column)
            {
                const double theta = 2.0 * std::numbers::pi_v<double> *
                    static_cast<double>(column) /
                    static_cast<double>(spec.Columns);
                rings.push_back(fixture.Mesh.AddVertex(glm::vec3{
                    static_cast<float>(radial * std::cos(theta)),
                    static_cast<float>(radial * std::sin(theta)),
                    static_cast<float>(z),
                }));
            }
        }
        const Geometry::VertexHandle south =
            fixture.Mesh.AddVertex({0.0f, 0.0f, -1.0f});

        const auto vertexAt = [&](const std::uint32_t ring,
                                  const std::uint32_t column)
        {
            return rings[
                static_cast<std::size_t>(ring) * spec.Columns +
                (column % spec.Columns)];
        };
        const auto addFace = [&](const Geometry::VertexHandle a,
                                 const Geometry::VertexHandle b,
                                 const Geometry::VertexHandle c)
        {
            const auto face = fixture.Mesh.AddTriangle(a, b, c);
            if (!face.has_value())
            {
                fixture.Valid = false;
                return;
            }
            if (fixture.ExpectedFaceRegime.size() <= face->Index)
            {
                fixture.ExpectedFaceRegime.resize(
                    static_cast<std::size_t>(face->Index) + 1u, 0u);
            }
        };

        for (std::uint32_t column = 0u;
             column < spec.Columns;
             ++column)
        {
            addFace(
                north,
                vertexAt(0u, column),
                vertexAt(0u, column + 1u));
        }
        for (std::uint32_t ring = 0u; ring + 1u < spec.Rows; ++ring)
        {
            for (std::uint32_t column = 0u;
                 column < spec.Columns;
                 ++column)
            {
                const Geometry::VertexHandle upper =
                    vertexAt(ring, column);
                const Geometry::VertexHandle upperNext =
                    vertexAt(ring, column + 1u);
                const Geometry::VertexHandle lower =
                    vertexAt(ring + 1u, column);
                const Geometry::VertexHandle lowerNext =
                    vertexAt(ring + 1u, column + 1u);
                if (flippedDiagonals)
                {
                    addFace(upper, lower, upperNext);
                    addFace(upperNext, lower, lowerNext);
                }
                else
                {
                    addFace(upper, lower, lowerNext);
                    addFace(upper, lowerNext, upperNext);
                }
                if (!fixture.Valid)
                    return fixture;
            }
        }
        for (std::uint32_t column = 0u;
             column < spec.Columns;
             ++column)
        {
            addFace(
                south,
                vertexAt(spec.Rows - 1u, column + 1u),
                vertexAt(spec.Rows - 1u, column));
        }
        fixture.Valid &= fixture.Mesh.VertexCount() == vertexCount;
        fixture.Valid &= fixture.Mesh.FaceCount() == faceCount;
        return fixture;
    }

    [[nodiscard]] double MisclassifiedFaceFraction(
        const std::vector<std::uint32_t>& labels,
        const std::vector<std::uint32_t>& expected)
    {
        if (labels.size() != expected.size() || labels.empty())
            return 1.0;
        std::size_t direct = 0u;
        std::size_t swapped = 0u;
        for (std::size_t face = 0u; face < labels.size(); ++face)
        {
            direct += labels[face] == expected[face] ? 1u : 0u;
            swapped += labels[face] == 1u - expected[face] ? 1u : 0u;
        }
        return 1.0 - static_cast<double>(std::max(direct, swapped)) /
            static_cast<double>(labels.size());
    }

    [[nodiscard]] double DominantLabelError(
        const std::vector<std::uint32_t>& labels)
    {
        if (labels.empty())
            return 1.0;
        std::vector<std::pair<std::uint32_t, std::size_t>> counts;
        for (const std::uint32_t label : labels)
        {
            if (label == Segment::kInvalidLabel)
                return 1.0;
            const auto found = std::find_if(
                counts.begin(), counts.end(),
                [label](const auto& entry)
                {
                    return entry.first == label;
                });
            if (found == counts.end())
                counts.emplace_back(label, 1u);
            else
                ++found->second;
        }
        const std::size_t dominant = std::max_element(
            counts.begin(), counts.end(),
            [](const auto& a, const auto& b)
            {
                return a.second < b.second;
            })->second;
        return 1.0 - static_cast<double>(dominant) /
            static_cast<double>(labels.size());
    }

    [[nodiscard]] Segment::CurvatureSegmentationParams MakeParams(
        const SelectionMode mode,
        const std::uint32_t fixedComponentCount)
    {
        Segment::CurvatureSegmentationParams params{};
        params.SelectionMode = mode == SelectionMode::Fixed
            ? Segment::ComponentSelectionMode::FixedCount
            : Segment::ComponentSelectionMode::Automatic;
        params.FixedComponentCount = fixedComponentCount;
        params.AutomaticMinComponents = 1u;
        params.AutomaticMaxComponents = 4u;
        params.AutomaticFitTolerance = 0.12;
        params.MaxSpatialIterations = 12u;
        params.MinimumRegionFaces = 1u;
        params.Seed = 17u;
        return params;
    }

    [[nodiscard]] VariantProfile RunVariant(
        const CohortSpec& spec,
        const SelectionMode mode,
        const bool anisotropic)
    {
        VariantProfile profile{};
        profile.Name = anisotropic ? "anisotropic" : "uniform";
        Fixture fixture = MakeGridFixture(spec, anisotropic);
        profile.FaceCount = fixture.Mesh.FaceCount();
        if (!fixture.Valid)
            return profile;

        const Segment::CurvatureSegmentationParams params =
            MakeParams(mode, 2u);
        for (std::uint32_t iteration = 0u;
             iteration < spec.WarmupIterations;
             ++iteration)
        {
            const auto warmup = Segment::Segment(
                fixture.Mesh, fixture.K1, fixture.K2, params);
            if (!warmup.Succeeded())
                return profile;
        }

        TimingSamples timings;
        timings.Reserve(spec.MeasuredIterations);

        Segment::CurvatureSegmentationResult last{};
        for (std::uint32_t iteration = 0u;
             iteration < spec.MeasuredIterations;
             ++iteration)
        {
            Segment::CurvatureSegmentationResult result = Segment::Segment(
                fixture.Mesh, fixture.K1, fixture.K2, params);
            if (!result.Succeeded())
                return profile;
            timings.Add(result.Diagnostics.Timings);
            last = std::move(result);
        }

        profile.MedianTimings = MedianTimings(std::move(timings));
        CopyResultDiagnostics(profile, last);
        profile.MisclassifiedFaceFraction = MisclassifiedFaceFraction(
            last.FaceComponents, fixture.ExpectedFaceRegime);
        profile.Boundary = MeasureUnitSquareTransitionBoundary(
            fixture.Mesh, last.EdgeBoundaries);
        profile.Succeeded = profile.SelectedComponentCount == 2u &&
            profile.MisclassifiedFaceFraction <= 0.02;
        return profile;
    }

    [[nodiscard]] VariantProfile RunSphereVariant(
        const CohortSpec& spec,
        const SelectionMode mode,
        const DescriptorLane lane,
        const bool flippedDiagonals)
    {
        VariantProfile profile{};
        profile.Name = flippedDiagonals ? "diagonal_b" : "diagonal_a";
        Fixture fixture = MakeSphereFixture(spec, flippedDiagonals);
        profile.FaceCount = fixture.Mesh.FaceCount();
        if (!fixture.Valid)
            return profile;

        const Segment::CurvatureSegmentationParams params =
            MakeParams(mode, 1u);
        Geometry::Curvature::CurvatureField reusable{};
        if (lane == DescriptorLane::Reusable)
        {
            const ProfileClock::time_point setupStart = ProfileClock::now();
            reusable = Geometry::Curvature::ComputeCurvature(fixture.Mesh);
            profile.DescriptorSetupMilliseconds =
                ElapsedMilliseconds(setupStart);
            if (!reusable.MaxPrincipalCurvatureProperty ||
                !reusable.MinPrincipalCurvatureProperty)
            {
                return profile;
            }
        }

        const auto execute = [&]()
        {
            if (lane == DescriptorLane::Cold)
                return Segment::ComputeAndSegment(fixture.Mesh, params);
            const std::vector<double>& maximum =
                reusable.MaxPrincipalCurvatureProperty.Vector();
            const std::vector<double>& minimum =
                reusable.MinPrincipalCurvatureProperty.Vector();
            return Segment::Segment(
                fixture.Mesh,
                std::span<const double>{maximum.data(), maximum.size()},
                std::span<const double>{minimum.data(), minimum.size()},
                params);
        };

        for (std::uint32_t iteration = 0u;
             iteration < spec.WarmupIterations;
             ++iteration)
        {
            const auto warmup = execute();
            if (!warmup.Succeeded())
                return profile;
        }

        TimingSamples timings;
        timings.Reserve(spec.MeasuredIterations);
        Segment::CurvatureSegmentationResult last{};
        for (std::uint32_t iteration = 0u;
             iteration < spec.MeasuredIterations;
             ++iteration)
        {
            Segment::CurvatureSegmentationResult result = execute();
            if (!result.Succeeded())
                return profile;
            timings.Add(result.Diagnostics.Timings);
            last = std::move(result);
        }

        profile.MedianTimings = MedianTimings(std::move(timings));
        CopyResultDiagnostics(profile, last);
        profile.MisclassifiedFaceFraction =
            DominantLabelError(last.FaceComponents);
        profile.Succeeded = profile.SelectedComponentCount == 1u &&
            profile.MisclassifiedFaceFraction <= 0.02;
        return profile;
    }

    [[nodiscard]] CohortProfile RunCohort(
        const CohortSpec& spec,
        const SelectionMode mode)
    {
        CohortProfile profile{};
        profile.Spec = spec;
        profile.Mode = mode;
        profile.Uniform = RunVariant(spec, mode, false);
        profile.Anisotropic = RunVariant(spec, mode, true);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        profile.Succeeded =
            profile.Uniform.Succeeded && profile.Anisotropic.Succeeded;
        return profile;
    }

    [[nodiscard]] DescriptorPairProfile RunDescriptorPair(
        const CohortSpec& spec,
        const SelectionMode mode,
        const DescriptorLane lane)
    {
        DescriptorPairProfile profile{};
        profile.Spec = spec;
        profile.Mode = mode;
        profile.Lane = lane;
        profile.DiagonalA = RunSphereVariant(spec, mode, lane, false);
        profile.DiagonalB = RunSphereVariant(spec, mode, lane, true);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        profile.Succeeded =
            profile.DiagonalA.Succeeded && profile.DiagonalB.Succeeded;
        return profile;
    }

    [[nodiscard]] std::string BenchmarkId(const CohortProfile& profile)
    {
        return "geometry.curvature_segmentation.reference.profile." +
            std::string{profile.Spec.Token} + "." +
            (profile.Mode == SelectionMode::Fixed
                ? "fixed"
                : "automatic");
    }

    [[nodiscard]] std::string DescriptorBenchmarkId(
        const DescriptorPairProfile& profile)
    {
        return "geometry.curvature_segmentation.reference.profile." +
            std::string{profile.Spec.Token} + "." +
            (profile.Lane == DescriptorLane::Cold ? "cold" : "reuse") +
            "." +
            (profile.Mode == SelectionMode::Fixed ? "fixed" : "automatic");
    }

    [[nodiscard]] std::string RemeshingBenchmarkId(
        const CohortProfile& profile)
    {
        return "geometry.curvature_segmentation.reference.remeshing." +
            std::string{profile.Spec.Token} + "." +
            (profile.Mode == SelectionMode::Fixed ? "fixed" : "automatic");
    }

    void EmitCandidates(
        std::ostringstream& out,
        const std::vector<CandidateProfile>& candidates)
    {
        out << '[';
        for (std::size_t index = 0u; index < candidates.size(); ++index)
        {
            if (index != 0u)
                out << ',';
            const CandidateProfile& candidate = candidates[index];
            out << "{\"component_count\":" << candidate.ComponentCount
                << ",\"iterations\":" << candidate.Iterations
                << ",\"fit_ms\":" << candidate.FitMilliseconds
                << ",\"selected\":"
                << (candidate.Selected ? "true" : "false") << '}';
        }
        out << ']';
    }

    void EmitVariant(
        std::ostringstream& out,
        const VariantProfile& profile)
    {
        const auto& timings = profile.MedianTimings;
        out << "{\"name\":\"" << profile.Name << "\""
            << ",\"face_count\":" << profile.FaceCount
            << ",\"dual_edge_count\":" << profile.DualEdgeCount
            << ",\"selected_component_count\":"
            << profile.SelectedComponentCount
            << ",\"spatial_iterations\":"
            << profile.SpatialIterations
            << ",\"misclassified_face_fraction\":"
            << profile.MisclassifiedFaceFraction
            << ",\"descriptor_setup_ms\":"
            << profile.DescriptorSetupMilliseconds
            << ",\"curvature_estimation_ms\":"
            << timings.CurvatureEstimationMilliseconds
            << ",\"face_aggregation_ms\":"
            << timings.FaceAggregationAndNormalizationMilliseconds
            << ",\"gmm_fitting_ms\":"
            << timings.GmmFittingMilliseconds
            << ",\"unary_construction_ms\":"
            << timings.UnaryConstructionMilliseconds
            << ",\"dual_graph_construction_ms\":"
            << timings.DualGraphConstructionMilliseconds
            << ",\"spatial_optimization_ms\":"
            << timings.SpatialOptimizationMilliseconds
            << ",\"connectivity_publication_ms\":"
            << timings.ConnectivityCleanupAndPublicationMilliseconds
            << ",\"segmentation_total_ms\":"
            << timings.TotalMilliseconds << ",\"candidates\":";
        EmitCandidates(out, profile.Candidates);
        if (profile.Boundary.ReferenceSampleCount != 0u)
        {
            const BoundaryProfile& boundary = profile.Boundary;
            out << ",\"continuous_boundary\":{\"valid\":"
                << (boundary.Valid ? "true" : "false")
                << ",\"edge_count\":" << boundary.EdgeCount
                << ",\"endpoint_count\":" << boundary.EndpointCount
                << ",\"junction_count\":" << boundary.JunctionCount
                << ",\"reference_sample_count\":"
                << boundary.ReferenceSampleCount
                << ",\"reference_sample_spacing_normalized\":"
                << boundary.ReferenceSampleSpacingNormalized
                << ",\"symmetric_hausdorff_upper_bound_normalized\":"
                << boundary.SymmetricHausdorffUpperBoundNormalized
                << ",\"tolerance_band_precision\":"
                << boundary.ToleranceBandPrecision
                << ",\"tolerance_band_recall\":"
                << boundary.ToleranceBandRecall
                << ",\"predicted_length_normalized\":"
                << boundary.PredictedLengthNormalized
                << ",\"reference_length_normalized\":"
                << boundary.ReferenceLengthNormalized << '}';
        }
        out << '}';
    }

    [[nodiscard]] std::string EmitResult(
        const CohortProfile& profile,
        const std::string& commit)
    {
        const std::string benchmarkId = BenchmarkId(profile);
        const double runtime = std::max(
            profile.Uniform.MedianTimings.TotalMilliseconds,
            profile.Anisotropic.MedianTimings.TotalMilliseconds);
        const double qualityError = std::max(
            profile.Uniform.MisclassifiedFaceFraction,
            profile.Anisotropic.MisclassifiedFaceFraction);
        const std::size_t populationCount = std::max(
            profile.Uniform.SelectedComponentCount,
            profile.Anisotropic.SelectedComponentCount);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \"builtin.curvature_regime_grid_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << runtime << ",\n"
            << "    \"memory_peak_bytes\": "
            << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << qualityError << ",\n"
            << "    \"population_count\": " << populationCount << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicCurvatureSegmentationProfile\",\n"
            << "    \"implementation_version\": \"cpu_reference_v1\",\n"
            << "    \"quality_error_l2_unit\": "
            << "\"misclassified_face_fraction\",\n"
            << "    \"aggregation\": \"max_of_variant_medians\",\n"
            << "    \"variants\": [";
        EmitVariant(out, profile.Uniform);
        out << ',';
        EmitVariant(out, profile.Anisotropic);
        out << "]\n"
            << "  },\n"
            << "  \"status\": \""
            << (profile.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] std::string EmitDescriptorResult(
        const DescriptorPairProfile& profile,
        const std::string& commit)
    {
        const std::string benchmarkId = DescriptorBenchmarkId(profile);
        const double runtime = std::max(
            profile.DiagonalA.MedianTimings.TotalMilliseconds,
            profile.DiagonalB.MedianTimings.TotalMilliseconds);
        const double qualityError = std::max(
            profile.DiagonalA.MisclassifiedFaceFraction,
            profile.DiagonalB.MisclassifiedFaceFraction);
        const std::size_t populationCount = std::max(
            profile.DiagonalA.SelectedComponentCount,
            profile.DiagonalB.SelectedComponentCount);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \"builtin.unit_sphere_diagonal_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << runtime << ",\n"
            << "    \"memory_peak_bytes\": "
            << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << qualityError << ",\n"
            << "    \"population_count\": " << populationCount << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicCurvatureSegmentationProfile\",\n"
            << "    \"implementation_version\": \"cpu_reference_v1\",\n"
            << "    \"descriptor_lane\": \""
            << (profile.Lane == DescriptorLane::Cold
                    ? "cold_compute_and_segment"
                    : "reusable_precomputed_curvature")
            << "\",\n"
            << "    \"quality_error_l2_unit\": "
            << "\"dominant_component_misclassified_face_fraction\",\n"
            << "    \"aggregation\": \"max_of_diagonal_pair_medians\",\n"
            << "    \"variants\": [";
        EmitVariant(out, profile.DiagonalA);
        out << ',';
        EmitVariant(out, profile.DiagonalB);
        out << "]\n"
            << "  },\n"
            << "  \"status\": \""
            << (profile.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] bool RemeshingSucceeded(
        const CohortProfile& profile) noexcept
    {
        const double hausdorff = std::max(
            profile.Uniform.Boundary
                .SymmetricHausdorffUpperBoundNormalized,
            profile.Anisotropic.Boundary
                .SymmetricHausdorffUpperBoundNormalized);
        return profile.Succeeded && profile.Uniform.Boundary.Valid &&
            profile.Anisotropic.Boundary.Valid && hausdorff <= 0.02;
    }

    [[nodiscard]] std::string EmitRemeshingResult(
        const CohortProfile& profile,
        const std::string& commit)
    {
        const std::string benchmarkId = RemeshingBenchmarkId(profile);
        const double runtime = std::max(
            profile.Uniform.MedianTimings.TotalMilliseconds,
            profile.Anisotropic.MedianTimings.TotalMilliseconds);
        const double labelError = std::max(
            profile.Uniform.MisclassifiedFaceFraction,
            profile.Anisotropic.MisclassifiedFaceFraction);
        const double boundaryError = std::max(
            profile.Uniform.Boundary
                .SymmetricHausdorffUpperBoundNormalized,
            profile.Anisotropic.Boundary
                .SymmetricHausdorffUpperBoundNormalized);
        const std::size_t populationCount = std::max(
            profile.Uniform.SelectedComponentCount,
            profile.Anisotropic.SelectedComponentCount);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \"builtin.curvature_transition_grid_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << runtime << ",\n"
            << "    \"memory_peak_bytes\": "
            << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << labelError << ",\n"
            << "    \"quality_error_linf\": " << boundaryError << ",\n"
            << "    \"population_count\": " << populationCount << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicCurvatureSegmentationProfile\",\n"
            << "    \"implementation_version\": \"cpu_reference_v1\",\n"
            << "    \"descriptor_lane\": \"supplied_curvature\",\n"
            << "    \"reference_surface\": \"unit_square\",\n"
            << "    \"reference_boundary\": \"x=0.5, y in [0,1]\",\n"
            << "    \"quality_error_l2_unit\": "
            << "\"misclassified_face_fraction\",\n"
            << "    \"quality_error_linf_unit\": "
            << "\"upper_bound_normalized_symmetric_surface_hausdorff\",\n"
            << "    \"tolerance_band_normalized\": 0.020000000,\n"
            << "    \"aggregation\": \"max_of_variant_medians\",\n"
            << "    \"variants\": [";
        EmitVariant(out, profile.Uniform);
        out << ',';
        EmitVariant(out, profile.Anisotropic);
        out << "]\n"
            << "  },\n"
            << "  \"status\": \""
            << (RemeshingSucceeded(profile) ? "passed" : "failed")
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

    [[nodiscard]] bool WriteResult(
        const std::filesystem::path& outputRoot,
        const CohortProfile& profile,
        const std::string& commit)
    {
        return WriteRawResult(
            outputRoot, BenchmarkId(profile), EmitResult(profile, commit));
    }

    [[nodiscard]] bool WriteDescriptorResult(
        const std::filesystem::path& outputRoot,
        const DescriptorPairProfile& profile,
        const std::string& commit)
    {
        return WriteRawResult(
            outputRoot,
            DescriptorBenchmarkId(profile),
            EmitDescriptorResult(profile, commit));
    }

    [[nodiscard]] bool WriteRemeshingResult(
        const std::filesystem::path& outputRoot,
        const CohortProfile& profile,
        const std::string& commit)
    {
        return WriteRawResult(
            outputRoot,
            RemeshingBenchmarkId(profile),
            EmitRemeshingResult(profile, commit));
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: IntrinsicCurvatureSegmentationProfile "
                     "<output-directory>\n";
        return 2;
    }

    const char* cohortEnvironment =
        std::getenv("INTRINSIC_CURVATURE_PROFILE_COHORT");
    const std::string_view cohort =
        cohortEnvironment == nullptr ? "smoke" : cohortEnvironment;
    constexpr CohortSpec smoke{
        .Token = "10k",
        .Rows = 50u,
        .Columns = 100u,
        .WarmupIterations = 1u,
        .MeasuredIterations = 3u,
    };
    constexpr std::array<CohortSpec, 2u> heavy{{
        {
            .Token = "100k",
            .Rows = 200u,
            .Columns = 250u,
            .WarmupIterations = 1u,
            .MeasuredIterations = 3u,
        },
        {
            .Token = "1m",
            .Rows = 500u,
            .Columns = 1000u,
            .WarmupIterations = 0u,
            .MeasuredIterations = 1u,
        },
    }};

    std::vector<CohortSpec> selected;
    if (cohort == "smoke")
        selected.push_back(smoke);
    else if (cohort == "heavy")
        selected.assign(heavy.begin(), heavy.end());
    else if (cohort != "fixtures")
    {
        std::cerr << "INTRINSIC_CURVATURE_PROFILE_COHORT must be "
                     "smoke, fixtures, or heavy\n";
        return 2;
    }

    const std::filesystem::path outputRoot{argv[1]};
    const std::string commit = ResolveCommit();
    bool allPassed = true;
    if (cohort == "fixtures")
    {
        for (const SelectionMode mode :
             {SelectionMode::Fixed, SelectionMode::Automatic})
        {
            const CohortProfile remeshing = RunCohort(smoke, mode);
            if (!WriteRemeshingResult(outputRoot, remeshing, commit))
            {
                std::cerr << "failed to write "
                          << RemeshingBenchmarkId(remeshing) << '\n';
                return 1;
            }
            std::cout << "Wrote " << RemeshingBenchmarkId(remeshing)
                      << '\n';
            allPassed &= RemeshingSucceeded(remeshing);

            for (const DescriptorLane lane :
                 {DescriptorLane::Cold, DescriptorLane::Reusable})
            {
                const DescriptorPairProfile descriptors =
                    RunDescriptorPair(smoke, mode, lane);
                if (!WriteDescriptorResult(
                        outputRoot, descriptors, commit))
                {
                    std::cerr << "failed to write "
                              << DescriptorBenchmarkId(descriptors)
                              << '\n';
                    return 1;
                }
                std::cout << "Wrote "
                          << DescriptorBenchmarkId(descriptors) << '\n';
                allPassed &= descriptors.Succeeded;
            }
        }
        return allPassed ? 0 : 1;
    }

    for (const CohortSpec& spec : selected)
    {
        for (const SelectionMode mode :
             {SelectionMode::Fixed, SelectionMode::Automatic})
        {
            const CohortProfile profile = RunCohort(spec, mode);
            if (!WriteResult(outputRoot, profile, commit))
            {
                std::cerr << "failed to write " << BenchmarkId(profile)
                          << '\n';
                return 1;
            }
            std::cout << "Wrote " << BenchmarkId(profile) << '\n';
            allPassed &= profile.Succeeded;
        }
    }
    return allPassed ? 0 : 1;
}
