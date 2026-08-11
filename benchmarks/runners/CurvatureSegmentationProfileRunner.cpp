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
import Geometry.HalfedgeMesh.Features;
import Geometry.Curvature;
import Geometry.HalfedgeMesh.CurvatureSegmentation;
import Geometry.Properties;

namespace
{
    namespace Segment = Geometry::CurvatureSegmentation;
    namespace Features = Geometry::HalfedgeMesh::Features;
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

    struct FoldFixture
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<double> K1{};
        std::vector<double> K2{};
        std::vector<glm::dvec3> FaceNormals{};
        std::vector<std::uint8_t> ExpectedFeatureMask{};
        bool Valid{true};
    };

    struct FoldVariantProfile
    {
        double DihedralDegrees{0.0};
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t ExpectedFeatureEdgeCount{0u};
        std::array<std::size_t, 2u> ClassifiedFeatureEdgeCounts{};
        double FeatureMaskErrorFraction{0.0};
        std::uint32_t V1SelectedComponentCount{0u};
        std::uint32_t V1ConnectedRegionCount{0u};
        std::size_t V1BoundaryEdgeCount{0u};
        bool V1MatchesFlatControl{false};
        bool Succeeded{false};
    };

    struct FoldScreeningProfile
    {
        std::array<FoldVariantProfile, 3u> Variants{};
        double RuntimeMilliseconds{0.0};
        double MaxFeatureMaskErrorFraction{1.0};
        double MaxIsometricEdgeLengthDeltaNormalized{1.0};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    struct SurfaceControlFixture
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<double> K1{};
        std::vector<double> K2{};
        std::vector<glm::dvec3> FaceNormals{};
        std::vector<std::uint8_t> ExpectedFeatureMask{};
        std::vector<std::uint32_t> ExpectedFaceRegime{};
        std::vector<std::uint8_t> ExpectedBoundaryMask{};
        bool Valid{true};
    };

    struct CylinderControlVariantProfile
    {
        std::string_view Name{};
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t ClassifiedFeatureEdgeCount{0u};
        double FeatureMaskErrorFraction{1.0};
        std::uint32_t V1SelectedComponentCount{0u};
        std::uint32_t V1ConnectedRegionCount{0u};
        std::size_t V1BoundaryEdgeCount{0u};
        bool FaceNormalsPointOutward{false};
        bool Succeeded{false};
    };

    struct CylinderControlProfile
    {
        std::array<CylinderControlVariantProfile, 2u> Variants{};
        double MaxRadialErrorNormalized{1.0};
        double MaxPairedEdgeLengthDeltaNormalized{1.0};
        bool V1PayloadsMatch{false};
        bool Succeeded{false};
    };

    struct SmoothTransitionVariantProfile
    {
        std::string_view Name{};
        std::size_t FaceCount{0u};
        std::size_t EdgeCount{0u};
        std::size_t ExpectedBoundaryEdgeCount{0u};
        std::size_t ClassifiedFeatureEdgeCount{0u};
        double FeatureMaskErrorFraction{1.0};
        std::uint32_t V1SelectedComponentCount{0u};
        std::uint32_t V1ConnectedRegionCount{0u};
        std::size_t V1BoundaryEdgeCount{0u};
        double V1FaceRegimeErrorFraction{1.0};
        double V1BoundaryMaskErrorFraction{1.0};
        double SurfaceEquationErrorNormalized{1.0};
        bool FaceNormalsPointUpward{false};
        bool AnalyticCurvatureContractSatisfied{false};
        BoundaryProfile Boundary{};
        bool Succeeded{false};
    };

    struct SurfaceScreeningProfile
    {
        CylinderControlProfile Cylinder{};
        std::array<SmoothTransitionVariantProfile, 2u> SmoothTransition{};
        double RuntimeMilliseconds{0.0};
        double MaxControlErrorFraction{1.0};
        double MaxGeometricOrBoundaryErrorNormalized{1.0};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] Segment::CurvatureSegmentationParams MakeParams(
        SelectionMode mode,
        std::uint32_t fixedComponentCount);

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

    [[nodiscard]] double SegmentFractionInsideVerticalTube(
        const glm::dvec3& a,
        const glm::dvec3& b,
        const double centerX,
        const double centerZ,
        const double radius) noexcept
    {
        const double x0 = a.x - centerX;
        const double z0 = a.z - centerZ;
        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double quadratic = dx * dx + dz * dz;
        const double linear = 2.0 * (x0 * dx + z0 * dz);
        const double constant = x0 * x0 + z0 * z0 - radius * radius;
        if (quadratic <= std::numeric_limits<double>::epsilon())
            return constant <= 0.0 ? 1.0 : 0.0;

        const double discriminant = linear * linear
            - 4.0 * quadratic * constant;
        if (discriminant < 0.0)
            return constant <= 0.0 ? 1.0 : 0.0;
        const double root = std::sqrt(std::max(0.0, discriminant));
        const double first = (-linear - root) / (2.0 * quadratic);
        const double second = (-linear + root) / (2.0 * quadratic);
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

    [[nodiscard]] BoundaryProfile MeasureVerticalTransitionBoundary(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const std::vector<std::uint8_t>& edgeBoundaries,
        const double referenceX,
        const double referenceZ,
        const double referenceYMin,
        const double referenceYMax,
        const double diagonal)
    {
        constexpr std::uint32_t referenceSampleCount = 4097u;
        constexpr double normalizedTolerance = 0.02;
        const double tolerance = normalizedTolerance * diagonal;
        const double referenceLength = referenceYMax - referenceYMin;
        const glm::dvec3 referenceA{
            referenceX, referenceYMin, referenceZ};
        const glm::dvec3 referenceB{
            referenceX, referenceYMax, referenceZ};

        BoundaryProfile profile{};
        profile.ReferenceSampleCount = referenceSampleCount;
        profile.ReferenceSampleSpacingNormalized =
            referenceLength /
            static_cast<double>(referenceSampleCount - 1u) /
            diagonal;
        profile.ReferenceLengthNormalized = referenceLength / diagonal;

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
            inBandLength += length * SegmentFractionInsideVerticalTube(
                a, b, referenceX, referenceZ, tolerance);
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
            const glm::dvec3 point{
                referenceX,
                referenceYMin + t * referenceLength,
                referenceZ};
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
            referenceLength /
            static_cast<double>(referenceSampleCount - 1u);
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

    [[nodiscard]] BoundaryProfile MeasureUnitSquareTransitionBoundary(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const std::vector<std::uint8_t>& edgeBoundaries)
    {
        return MeasureVerticalTransitionBoundary(
            mesh,
            edgeBoundaries,
            0.5,
            0.0,
            0.0,
            1.0,
            std::sqrt(2.0));
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

    [[nodiscard]] FoldFixture MakeFoldFixture(
        const std::uint32_t rows,
        const std::uint32_t columns,
        const double dihedralDegrees,
        const bool flippedDiagonals)
    {
        FoldFixture fixture{};
        if (rows == 0u || columns < 2u || (columns & 1u) != 0u)
        {
            fixture.Valid = false;
            return fixture;
        }

        const std::size_t vertexCount =
            static_cast<std::size_t>(rows + 1u) * (columns + 1u);
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(rows) * columns;
        fixture.Mesh.Reserve(vertexCount, 2u * faceCount, faceCount);
        fixture.K1.assign(vertexCount, 1.0);
        fixture.K2.assign(vertexCount, 0.0);

        const double angle = dihedralDegrees
            * std::numbers::pi_v<double> / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const glm::dvec3 leftNormal{0.0, 0.0, 1.0};
        const glm::dvec3 rightNormal{-sine, 0.0, cosine};

        std::vector<Geometry::VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= rows; ++row)
        {
            const double y = static_cast<double>(row) /
                static_cast<double>(rows);
            for (std::uint32_t column = 0u; column <= columns; ++column)
            {
                const double u = 2.0 * static_cast<double>(column) /
                    static_cast<double>(columns) - 1.0;
                const bool right = u > 0.0;
                const double x = right ? u * cosine : u;
                const double z = right ? u * sine : 0.0;
                vertices.push_back(fixture.Mesh.AddVertex(glm::vec3{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                }));
            }
        }

        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) * (columns + 1u) + column];
        };
        const auto addFace = [&](const Geometry::VertexHandle a,
                                 const Geometry::VertexHandle b,
                                 const Geometry::VertexHandle c,
                                 const bool right)
        {
            const auto face = fixture.Mesh.AddTriangle(a, b, c);
            if (!face.has_value())
            {
                fixture.Valid = false;
                return;
            }
            if (fixture.FaceNormals.size() <= face->Index)
            {
                fixture.FaceNormals.resize(
                    static_cast<std::size_t>(face->Index) + 1u,
                    glm::dvec3{0.0});
            }
            fixture.FaceNormals[face->Index] =
                right ? rightNormal : leftNormal;
        };

        const std::uint32_t creaseColumn = columns / 2u;
        for (std::uint32_t row = 0u; row < rows; ++row)
        {
            for (std::uint32_t column = 0u; column < columns; ++column)
            {
                const Geometry::VertexHandle v00 = vertexAt(row, column);
                const Geometry::VertexHandle v10 = vertexAt(row, column + 1u);
                const Geometry::VertexHandle v01 = vertexAt(row + 1u, column);
                const Geometry::VertexHandle v11 =
                    vertexAt(row + 1u, column + 1u);
                const bool right = column >= creaseColumn;
                const bool alternate =
                    ((row + column + (flippedDiagonals ? 1u : 0u)) & 1u)
                    != 0u;
                if (alternate)
                {
                    addFace(v00, v10, v01, right);
                    addFace(v10, v11, v01, right);
                }
                else
                {
                    addFace(v00, v10, v11, right);
                    addFace(v00, v11, v01, right);
                }
                if (!fixture.Valid)
                    return fixture;
            }
        }

        fixture.ExpectedFeatureMask.assign(fixture.Mesh.EdgesSize(), 0u);
        if (dihedralDegrees > 45.0)
        {
            for (std::uint32_t row = 0u; row < rows; ++row)
            {
                const auto edge = fixture.Mesh.FindEdge(
                    vertexAt(row, creaseColumn),
                    vertexAt(row + 1u, creaseColumn));
                if (!edge.has_value())
                {
                    fixture.Valid = false;
                    return fixture;
                }
                fixture.ExpectedFeatureMask[edge->Index] = 1u;
            }
        }

        fixture.Valid &= fixture.Mesh.VertexCount() == vertexCount;
        fixture.Valid &= fixture.Mesh.FaceCount() == faceCount;
        fixture.Valid &= fixture.FaceNormals.size() == fixture.Mesh.FacesSize();
        return fixture;
    }

    [[nodiscard]] bool PopulateGeometricFaceNormals(
        SurfaceControlFixture& fixture)
    {
        fixture.FaceNormals.assign(
            fixture.Mesh.FacesSize(), glm::dvec3{0.0});
        for (const Geometry::FaceHandle face : fixture.Mesh.LiveFaces())
        {
            const Geometry::HalfedgeHandle h0 = fixture.Mesh.Halfedge(face);
            const Geometry::HalfedgeHandle h1 = fixture.Mesh.NextHalfedge(h0);
            const Geometry::HalfedgeHandle h2 = fixture.Mesh.NextHalfedge(h1);
            if (!h0.IsValid() || !h1.IsValid() || !h2.IsValid())
                return false;
            const glm::dvec3 p0 = ToDouble(
                fixture.Mesh.Position(fixture.Mesh.ToVertex(h0)));
            const glm::dvec3 p1 = ToDouble(
                fixture.Mesh.Position(fixture.Mesh.ToVertex(h1)));
            const glm::dvec3 p2 = ToDouble(
                fixture.Mesh.Position(fixture.Mesh.ToVertex(h2)));
            const glm::dvec3 normal = glm::cross(p1 - p0, p2 - p0);
            const double squaredLength = glm::dot(normal, normal);
            if (!(squaredLength > 0.0)
                || !std::isfinite(squaredLength)
                || !std::isfinite(normal.x)
                || !std::isfinite(normal.y)
                || !std::isfinite(normal.z))
            {
                return false;
            }
            fixture.FaceNormals[face.Index] = normal;
        }
        return true;
    }

    [[nodiscard]] SurfaceControlFixture MakeCylinderControlFixture(
        const double phaseTurns)
    {
        constexpr std::uint32_t kAxialCells = 32u;
        constexpr std::uint32_t kAngularCells = 64u;
        constexpr double kRadius = 1.0;
        constexpr double kLength = 2.0;
        SurfaceControlFixture fixture{};
        const std::size_t vertexCount =
            static_cast<std::size_t>(kAxialCells + 1u) * kAngularCells;
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(kAxialCells) * kAngularCells;
        fixture.Mesh.Reserve(vertexCount, 2u * faceCount, faceCount);
        fixture.K1.assign(vertexCount, 1.0 / kRadius);
        fixture.K2.assign(vertexCount, 0.0);

        std::vector<Geometry::VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= kAxialCells; ++row)
        {
            const double y = kLength * static_cast<double>(row) /
                static_cast<double>(kAxialCells);
            for (std::uint32_t column = 0u;
                 column < kAngularCells;
                 ++column)
            {
                const double turns = static_cast<double>(column) /
                    static_cast<double>(kAngularCells) + phaseTurns;
                const double theta =
                    2.0 * std::numbers::pi_v<double> * turns;
                vertices.push_back(fixture.Mesh.AddVertex(glm::vec3{
                    static_cast<float>(kRadius * std::cos(theta)),
                    static_cast<float>(y),
                    static_cast<float>(-kRadius * std::sin(theta)),
                }));
            }
        }

        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) * kAngularCells
                + (column % kAngularCells)];
        };
        const auto addFace = [&](const Geometry::VertexHandle a,
                                 const Geometry::VertexHandle b,
                                 const Geometry::VertexHandle c)
        {
            if (!fixture.Mesh.AddTriangle(a, b, c).has_value())
                fixture.Valid = false;
        };

        for (std::uint32_t row = 0u; row < kAxialCells; ++row)
        {
            for (std::uint32_t column = 0u;
                 column < kAngularCells;
                 ++column)
            {
                const Geometry::VertexHandle v00 = vertexAt(row, column);
                const Geometry::VertexHandle v10 = vertexAt(row, column + 1u);
                const Geometry::VertexHandle v01 =
                    vertexAt(row + 1u, column);
                const Geometry::VertexHandle v11 =
                    vertexAt(row + 1u, column + 1u);
                const bool alternate = ((row + column) & 1u) != 0u;
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

        fixture.ExpectedFeatureMask.assign(fixture.Mesh.EdgesSize(), 0u);
        fixture.ExpectedBoundaryMask.assign(fixture.Mesh.EdgesSize(), 0u);
        fixture.Valid &= fixture.Mesh.VertexCount() == vertexCount;
        fixture.Valid &= fixture.Mesh.FaceCount() == faceCount;
        fixture.Valid &= PopulateGeometricFaceNormals(fixture);
        return fixture;
    }

    [[nodiscard]] double SmoothTransitionHeight(const double x) noexcept
    {
        constexpr double kWidth = 0.08;
        return 0.5 * (1.0 + std::tanh(x / kWidth));
    }

    [[nodiscard]] double SmoothTransitionCurvature(const double x) noexcept
    {
        constexpr double kWidth = 0.08;
        const double tangent = std::tanh(x / kWidth);
        const double sechSquared = 1.0 - tangent * tangent;
        const double first = 0.5 * sechSquared / kWidth;
        const double second =
            -sechSquared * tangent / (kWidth * kWidth);
        return second / std::pow(1.0 + first * first, 1.5);
    }

    [[nodiscard]] SurfaceControlFixture MakeSmoothTransitionFixture(
        const bool flippedDiagonals)
    {
        constexpr std::uint32_t kRows = 24u;
        constexpr std::uint32_t kColumns = 48u;
        static_assert((kColumns & 1u) == 0u);
        SurfaceControlFixture fixture{};
        const std::size_t vertexCount =
            static_cast<std::size_t>(kRows + 1u) * (kColumns + 1u);
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(kRows) * kColumns;
        fixture.Mesh.Reserve(vertexCount, 2u * faceCount, faceCount);
        fixture.K1.resize(vertexCount);
        fixture.K2.resize(vertexCount);

        std::vector<Geometry::VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= kRows; ++row)
        {
            const double y = static_cast<double>(row) /
                static_cast<double>(kRows);
            for (std::uint32_t column = 0u;
                 column <= kColumns;
                 ++column)
            {
                const double x = 2.0 * static_cast<double>(column) /
                    static_cast<double>(kColumns) - 1.0;
                const Geometry::VertexHandle vertex =
                    fixture.Mesh.AddVertex(glm::vec3{
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(SmoothTransitionHeight(x)),
                    });
                vertices.push_back(vertex);
                const double curvature = SmoothTransitionCurvature(x);
                fixture.K1[vertex.Index] = std::max(curvature, 0.0);
                fixture.K2[vertex.Index] = std::min(curvature, 0.0);
            }
        }

        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) * (kColumns + 1u) + column];
        };
        const auto addFace = [&](const Geometry::VertexHandle a,
                                 const Geometry::VertexHandle b,
                                 const Geometry::VertexHandle c,
                                 const bool rightRegime)
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
            fixture.ExpectedFaceRegime[face->Index] =
                rightRegime ? 1u : 0u;
        };

        const std::uint32_t transitionColumn = kColumns / 2u;
        for (std::uint32_t row = 0u; row < kRows; ++row)
        {
            for (std::uint32_t column = 0u; column < kColumns; ++column)
            {
                const Geometry::VertexHandle v00 = vertexAt(row, column);
                const Geometry::VertexHandle v10 = vertexAt(row, column + 1u);
                const Geometry::VertexHandle v01 =
                    vertexAt(row + 1u, column);
                const Geometry::VertexHandle v11 =
                    vertexAt(row + 1u, column + 1u);
                const bool rightRegime = column >= transitionColumn;
                const bool alternate =
                    ((row + column + (flippedDiagonals ? 1u : 0u)) & 1u)
                    != 0u;
                if (alternate)
                {
                    addFace(v00, v10, v01, rightRegime);
                    addFace(v10, v11, v01, rightRegime);
                }
                else
                {
                    addFace(v00, v10, v11, rightRegime);
                    addFace(v00, v11, v01, rightRegime);
                }
                if (!fixture.Valid)
                    return fixture;
            }
        }

        fixture.ExpectedFeatureMask.assign(fixture.Mesh.EdgesSize(), 0u);
        fixture.ExpectedBoundaryMask.assign(fixture.Mesh.EdgesSize(), 0u);
        for (std::uint32_t row = 0u; row < kRows; ++row)
        {
            const auto edge = fixture.Mesh.FindEdge(
                vertexAt(row, transitionColumn),
                vertexAt(row + 1u, transitionColumn));
            if (!edge.has_value())
            {
                fixture.Valid = false;
                return fixture;
            }
            fixture.ExpectedBoundaryMask[edge->Index] = 1u;
        }

        fixture.Valid &= fixture.Mesh.VertexCount() == vertexCount;
        fixture.Valid &= fixture.Mesh.FaceCount() == faceCount;
        fixture.Valid &= fixture.ExpectedFaceRegime.size()
            == fixture.Mesh.FacesSize();
        fixture.Valid &= PopulateGeometricFaceNormals(fixture);
        return fixture;
    }

    [[nodiscard]] double EdgeLength(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const Geometry::EdgeHandle edge) noexcept
    {
        const Geometry::HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
        if (!halfedge.IsValid() || !mesh.IsValid(halfedge))
            return std::numeric_limits<double>::infinity();
        const Geometry::VertexHandle from = mesh.FromVertex(halfedge);
        const Geometry::VertexHandle to = mesh.ToVertex(halfedge);
        if (!from.IsValid() || !to.IsValid()
            || !mesh.IsValid(from) || !mesh.IsValid(to))
        {
            return std::numeric_limits<double>::infinity();
        }
        const glm::dvec3 delta =
            ToDouble(mesh.Position(to)) - ToDouble(mesh.Position(from));
        return std::sqrt(glm::dot(delta, delta));
    }

    [[nodiscard]] double MaxEdgeLengthDeltaNormalized(
        const Geometry::HalfedgeMesh::Mesh& flat,
        const Geometry::HalfedgeMesh::Mesh& folded,
        const double referenceDiagonal) noexcept
    {
        if (flat.EdgesSize() != folded.EdgesSize())
            return 1.0;
        double maximum = 0.0;
        for (std::size_t edgeIndex = 0u;
             edgeIndex < flat.EdgesSize();
             ++edgeIndex)
        {
            const Geometry::EdgeHandle edge{
                static_cast<Geometry::PropertyIndex>(edgeIndex)};
            if (flat.IsDeleted(edge) != folded.IsDeleted(edge))
                return 1.0;
            if (flat.IsDeleted(edge))
                continue;
            const double flatLength = EdgeLength(flat, edge);
            const double foldedLength = EdgeLength(folded, edge);
            if (!std::isfinite(flatLength) || !std::isfinite(foldedLength))
                return 1.0;
            maximum = std::max(maximum, std::abs(flatLength - foldedLength));
        }
        return maximum / referenceDiagonal;
    }

    [[nodiscard]] double MaxCylinderRadialErrorNormalized(
        const Geometry::HalfedgeMesh::Mesh& mesh) noexcept
    {
        const double kReferenceDiagonal = std::sqrt(12.0);
        double maximum = 0.0;
        for (const Geometry::VertexHandle vertex : mesh.LiveVertices())
        {
            const glm::vec3 position = mesh.Position(vertex);
            const double radius = std::hypot(
                static_cast<double>(position.x),
                static_cast<double>(position.z));
            if (!std::isfinite(radius))
                return 1.0;
            maximum = std::max(maximum, std::abs(radius - 1.0));
        }
        return maximum / kReferenceDiagonal;
    }

    [[nodiscard]] bool CylinderFaceNormalsPointOutward(
        const SurfaceControlFixture& fixture) noexcept
    {
        for (const Geometry::FaceHandle face : fixture.Mesh.LiveFaces())
        {
            const Geometry::HalfedgeHandle h0 = fixture.Mesh.Halfedge(face);
            const Geometry::HalfedgeHandle h1 = fixture.Mesh.NextHalfedge(h0);
            const Geometry::HalfedgeHandle h2 = fixture.Mesh.NextHalfedge(h1);
            const glm::dvec3 centroid =
                (ToDouble(fixture.Mesh.Position(fixture.Mesh.ToVertex(h0)))
                 + ToDouble(fixture.Mesh.Position(fixture.Mesh.ToVertex(h1)))
                 + ToDouble(fixture.Mesh.Position(fixture.Mesh.ToVertex(h2))))
                / 3.0;
            const glm::dvec3 radial{centroid.x, 0.0, centroid.z};
            if (!(glm::dot(fixture.FaceNormals[face.Index], radial) > 0.0))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool SmoothFaceNormalsPointUpward(
        const SurfaceControlFixture& fixture) noexcept
    {
        for (const Geometry::FaceHandle face : fixture.Mesh.LiveFaces())
        {
            if (!(fixture.FaceNormals[face.Index].z > 0.0))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool SmoothAnalyticCurvatureContractSatisfied(
        const SurfaceControlFixture& fixture) noexcept
    {
        constexpr double kZeroTolerance = 1.0e-12;
        for (const Geometry::VertexHandle vertex : fixture.Mesh.LiveVertices())
        {
            const double x = static_cast<double>(
                fixture.Mesh.Position(vertex).x);
            const double k1 = fixture.K1[vertex.Index];
            const double k2 = fixture.K2[vertex.Index];
            if (!std::isfinite(k1) || !std::isfinite(k2) || k1 < k2)
                return false;
            if (x < -kZeroTolerance)
            {
                if (!(k1 > 0.0) || std::abs(k2) > kZeroTolerance)
                    return false;
            }
            else if (x > kZeroTolerance)
            {
                if (std::abs(k1) > kZeroTolerance || !(k2 < 0.0))
                    return false;
            }
            else if (std::abs(k1) > kZeroTolerance
                     || std::abs(k2) > kZeroTolerance)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] double SmoothSurfaceEquationErrorNormalized(
        const SurfaceControlFixture& fixture) noexcept
    {
        const double heightRange =
            SmoothTransitionHeight(1.0) - SmoothTransitionHeight(-1.0);
        const double referenceDiagonal =
            std::sqrt(5.0 + heightRange * heightRange);
        double maximum = 0.0;
        for (const Geometry::VertexHandle vertex : fixture.Mesh.LiveVertices())
        {
            const glm::vec3 position = fixture.Mesh.Position(vertex);
            const double error = std::abs(
                static_cast<double>(position.z)
                - SmoothTransitionHeight(static_cast<double>(position.x)));
            if (!std::isfinite(error))
                return 1.0;
            maximum = std::max(maximum, error);
        }
        return maximum / referenceDiagonal;
    }

    [[nodiscard]] double LiveEdgeMaskErrorFraction(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const std::vector<std::uint8_t>& actual,
        const std::vector<std::uint8_t>& expected) noexcept
    {
        if (actual.size() != mesh.EdgesSize()
            || expected.size() != mesh.EdgesSize())
        {
            return 1.0;
        }
        std::size_t live = 0u;
        std::size_t mismatches = 0u;
        for (std::size_t edgeIndex = 0u;
             edgeIndex < mesh.EdgesSize();
             ++edgeIndex)
        {
            const Geometry::EdgeHandle edge{
                static_cast<Geometry::PropertyIndex>(edgeIndex)};
            if (mesh.IsDeleted(edge))
                continue;
            ++live;
            mismatches += actual[edgeIndex] != expected[edgeIndex] ? 1u : 0u;
        }
        return live == 0u
            ? 1.0
            : static_cast<double>(mismatches) / static_cast<double>(live);
    }

    [[nodiscard]] bool SameV1Payload(
        const Segment::CurvatureSegmentationResult& a,
        const Segment::CurvatureSegmentationResult& b) noexcept
    {
        return a.Succeeded() && b.Succeeded()
            && a.FaceComponents == b.FaceComponents
            && a.FaceRegions == b.FaceRegions
            && a.EdgeBoundaries == b.EdgeBoundaries;
    }

    [[nodiscard]] FoldVariantProfile RunFoldVariant(
        const double dihedralDegrees,
        const std::array<FoldFixture, 2u>& flatFixtures,
        const std::array<Segment::CurvatureSegmentationResult, 2u>& flatResults,
        double& maxIsometricDelta)
    {
        constexpr std::uint32_t kRows = 24u;
        constexpr std::uint32_t kColumns = 48u;
        FoldVariantProfile profile{};
        profile.DihedralDegrees = dihedralDegrees;

        Features::Params featureParams{};
        featureParams.BoundaryIsFeature = false;
        featureParams.DihedralThresholdDegrees = 45.0;
        const Segment::CurvatureSegmentationParams segmentParams =
            MakeParams(SelectionMode::Fixed, 1u);

        bool allValid = true;
        for (std::size_t triangulation = 0u;
             triangulation < 2u;
             ++triangulation)
        {
            FoldFixture fixture = MakeFoldFixture(
                kRows,
                kColumns,
                dihedralDegrees,
                triangulation != 0u);
            allValid &= fixture.Valid;
            if (!fixture.Valid)
                continue;

            const Features::Classification classification = Features::Classify(
                fixture.Mesh, fixture.FaceNormals, featureParams);
            allValid &= classification.Status
                == Features::ClassificationStatus::Success;
            profile.ClassifiedFeatureEdgeCounts[triangulation] =
                classification.FeatureEdgeCount;
            profile.FeatureMaskErrorFraction = std::max(
                profile.FeatureMaskErrorFraction,
                LiveEdgeMaskErrorFraction(
                    fixture.Mesh,
                    classification.EdgeFeatureMask,
                    fixture.ExpectedFeatureMask));

            Segment::CurvatureSegmentationResult result = Segment::Segment(
                fixture.Mesh,
                fixture.K1,
                fixture.K2,
                segmentParams);
            allValid &= result.Succeeded();
            profile.V1MatchesFlatControl = triangulation == 0u
                ? SameV1Payload(result, flatResults[triangulation])
                : profile.V1MatchesFlatControl
                    && SameV1Payload(result, flatResults[triangulation]);
            if (triangulation == 0u)
            {
                profile.FaceCount = fixture.Mesh.FaceCount();
                profile.EdgeCount = fixture.Mesh.EdgeCount();
                profile.ExpectedFeatureEdgeCount = static_cast<std::size_t>(
                    std::count(
                        fixture.ExpectedFeatureMask.begin(),
                        fixture.ExpectedFeatureMask.end(),
                        std::uint8_t{1u}));
                profile.V1SelectedComponentCount =
                    result.Diagnostics.SelectedComponentCount;
                profile.V1ConnectedRegionCount =
                    result.Diagnostics.ConnectedRegionCount;
                profile.V1BoundaryEdgeCount =
                    result.Diagnostics.BoundaryEdgeCount;
            }
            else
            {
                allValid &= profile.FaceCount == fixture.Mesh.FaceCount();
                allValid &= profile.EdgeCount == fixture.Mesh.EdgeCount();
                allValid &= profile.V1SelectedComponentCount
                    == result.Diagnostics.SelectedComponentCount;
                allValid &= profile.V1ConnectedRegionCount
                    == result.Diagnostics.ConnectedRegionCount;
                allValid &= profile.V1BoundaryEdgeCount
                    == result.Diagnostics.BoundaryEdgeCount;
            }
            maxIsometricDelta = std::max(
                maxIsometricDelta,
                MaxEdgeLengthDeltaNormalized(
                    flatFixtures[triangulation].Mesh,
                    fixture.Mesh,
                    std::sqrt(5.0)));
        }

        const std::size_t expected = profile.ExpectedFeatureEdgeCount;
        profile.Succeeded = allValid
            && profile.FeatureMaskErrorFraction == 0.0
            && profile.ClassifiedFeatureEdgeCounts[0u] == expected
            && profile.ClassifiedFeatureEdgeCounts[1u] == expected
            && profile.V1SelectedComponentCount == 1u
            && profile.V1ConnectedRegionCount == 1u
            && profile.V1BoundaryEdgeCount == 0u
            && profile.V1MatchesFlatControl;
        return profile;
    }

    [[nodiscard]] FoldScreeningProfile RunFoldScreening()
    {
        constexpr std::uint32_t kRows = 24u;
        constexpr std::uint32_t kColumns = 48u;
        const ProfileClock::time_point start = ProfileClock::now();
        FoldScreeningProfile profile{};
        std::array<FoldFixture, 2u> flatFixtures{
            MakeFoldFixture(kRows, kColumns, 0.0, false),
            MakeFoldFixture(kRows, kColumns, 0.0, true),
        };
        const Segment::CurvatureSegmentationParams params =
            MakeParams(SelectionMode::Fixed, 1u);
        std::array<Segment::CurvatureSegmentationResult, 2u> flatResults{
            Segment::Segment(
                flatFixtures[0u].Mesh,
                flatFixtures[0u].K1,
                flatFixtures[0u].K2,
                params),
            Segment::Segment(
                flatFixtures[1u].Mesh,
                flatFixtures[1u].K1,
                flatFixtures[1u].K2,
                params),
        };

        constexpr std::array<double, 3u> kDihedrals{30.0, 45.0, 60.0};
        profile.MaxIsometricEdgeLengthDeltaNormalized = 0.0;
        profile.MaxFeatureMaskErrorFraction = 0.0;
        profile.Succeeded = true;
        for (std::size_t index = 0u; index < kDihedrals.size(); ++index)
        {
            profile.Variants[index] = RunFoldVariant(
                kDihedrals[index],
                flatFixtures,
                flatResults,
                profile.MaxIsometricEdgeLengthDeltaNormalized);
            profile.MaxFeatureMaskErrorFraction = std::max(
                profile.MaxFeatureMaskErrorFraction,
                profile.Variants[index].FeatureMaskErrorFraction);
            profile.Succeeded &= profile.Variants[index].Succeeded;
        }
        profile.Succeeded &= flatFixtures[0u].Valid && flatFixtures[1u].Valid;
        profile.Succeeded &= flatResults[0u].Succeeded()
            && flatResults[1u].Succeeded();
        profile.Succeeded &= profile.MaxIsometricEdgeLengthDeltaNormalized
            <= 1.0e-6;
        profile.RuntimeMilliseconds = ElapsedMilliseconds(start);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        return profile;
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

    [[nodiscard]] CylinderControlProfile RunCylinderControls()
    {
        const double referenceDiagonal = std::sqrt(12.0);
        std::array<SurfaceControlFixture, 2u> fixtures{
            MakeCylinderControlFixture(0.0),
            MakeCylinderControlFixture(1.0 / 128.0),
        };
        std::array<Segment::CurvatureSegmentationResult, 2u> results{};
        constexpr std::array<std::string_view, 2u> kNames{
            "phase_0", "phase_1_over_128_turn"};

        Features::Params featureParams{};
        featureParams.BoundaryIsFeature = false;
        featureParams.DihedralThresholdDegrees = 45.0;
        Segment::CurvatureSegmentationParams segmentParams =
            MakeParams(SelectionMode::Fixed, 1u);
        segmentParams.Seed = 1709u;

        CylinderControlProfile profile{};
        profile.MaxRadialErrorNormalized = 0.0;
        profile.MaxPairedEdgeLengthDeltaNormalized = 0.0;
        profile.Succeeded = true;
        for (std::size_t index = 0u; index < fixtures.size(); ++index)
        {
            SurfaceControlFixture& fixture = fixtures[index];
            CylinderControlVariantProfile& variant = profile.Variants[index];
            variant.Name = kNames[index];
            variant.FeatureMaskErrorFraction = 0.0;
            variant.FaceCount = fixture.Mesh.FaceCount();
            variant.EdgeCount = fixture.Mesh.EdgeCount();
            bool valid = fixture.Valid;
            if (!fixture.Valid)
            {
                profile.Succeeded = false;
                continue;
            }
            variant.FaceNormalsPointOutward =
                CylinderFaceNormalsPointOutward(fixture);

            const Features::Classification classification = Features::Classify(
                fixture.Mesh, fixture.FaceNormals, featureParams);
            valid &= classification.Status
                == Features::ClassificationStatus::Success;
            variant.ClassifiedFeatureEdgeCount =
                classification.FeatureEdgeCount;
            variant.FeatureMaskErrorFraction = LiveEdgeMaskErrorFraction(
                fixture.Mesh,
                classification.EdgeFeatureMask,
                fixture.ExpectedFeatureMask);

            results[index] = Segment::Segment(
                fixture.Mesh, fixture.K1, fixture.K2, segmentParams);
            valid &= results[index].Succeeded();
            if (results[index].Succeeded())
            {
                variant.V1SelectedComponentCount =
                    results[index].Diagnostics.SelectedComponentCount;
                variant.V1ConnectedRegionCount =
                    results[index].Diagnostics.ConnectedRegionCount;
                variant.V1BoundaryEdgeCount =
                    results[index].Diagnostics.BoundaryEdgeCount;
            }
            profile.MaxRadialErrorNormalized = std::max(
                profile.MaxRadialErrorNormalized,
                MaxCylinderRadialErrorNormalized(fixture.Mesh));
            variant.Succeeded = valid
                && variant.ClassifiedFeatureEdgeCount == 0u
                && variant.FeatureMaskErrorFraction == 0.0
                && variant.V1SelectedComponentCount == 1u
                && variant.V1ConnectedRegionCount == 1u
                && variant.V1BoundaryEdgeCount == 0u
                && variant.FaceNormalsPointOutward;
            profile.Succeeded &= variant.Succeeded;
        }

        if (fixtures[0u].Valid && fixtures[1u].Valid)
        {
            profile.MaxPairedEdgeLengthDeltaNormalized =
                MaxEdgeLengthDeltaNormalized(
                    fixtures[0u].Mesh,
                    fixtures[1u].Mesh,
                    referenceDiagonal);
        }
        profile.V1PayloadsMatch = SameV1Payload(results[0u], results[1u]);
        profile.Succeeded &= profile.MaxRadialErrorNormalized <= 1.0e-6;
        profile.Succeeded &= profile.MaxPairedEdgeLengthDeltaNormalized
            <= 1.0e-6;
        profile.Succeeded &= profile.V1PayloadsMatch;
        return profile;
    }

    [[nodiscard]] SmoothTransitionVariantProfile
    RunSmoothTransitionVariant(const bool flippedDiagonals)
    {
        SurfaceControlFixture fixture =
            MakeSmoothTransitionFixture(flippedDiagonals);
        SmoothTransitionVariantProfile profile{};
        profile.Name = flippedDiagonals ? "diagonal_b" : "diagonal_a";
        profile.FaceCount = fixture.Mesh.FaceCount();
        profile.EdgeCount = fixture.Mesh.EdgeCount();
        profile.ExpectedBoundaryEdgeCount = static_cast<std::size_t>(
            std::count(
                fixture.ExpectedBoundaryMask.begin(),
                fixture.ExpectedBoundaryMask.end(),
                std::uint8_t{1u}));
        if (!fixture.Valid)
            return profile;
        profile.FaceNormalsPointUpward =
            SmoothFaceNormalsPointUpward(fixture);
        profile.AnalyticCurvatureContractSatisfied =
            SmoothAnalyticCurvatureContractSatisfied(fixture);
        profile.SurfaceEquationErrorNormalized =
            SmoothSurfaceEquationErrorNormalized(fixture);

        Features::Params featureParams{};
        featureParams.BoundaryIsFeature = false;
        featureParams.DihedralThresholdDegrees = 45.0;
        const Features::Classification classification = Features::Classify(
            fixture.Mesh, fixture.FaceNormals, featureParams);
        const bool classificationSucceeded = classification.Status
            == Features::ClassificationStatus::Success;
        profile.ClassifiedFeatureEdgeCount =
            classification.FeatureEdgeCount;
        profile.FeatureMaskErrorFraction = LiveEdgeMaskErrorFraction(
            fixture.Mesh,
            classification.EdgeFeatureMask,
            fixture.ExpectedFeatureMask);

        Segment::CurvatureSegmentationParams segmentParams =
            MakeParams(SelectionMode::Fixed, 2u);
        segmentParams.Seed = 1741u;
        const Segment::CurvatureSegmentationResult result = Segment::Segment(
            fixture.Mesh, fixture.K1, fixture.K2, segmentParams);
        if (result.Succeeded())
        {
            profile.V1SelectedComponentCount =
                result.Diagnostics.SelectedComponentCount;
            profile.V1ConnectedRegionCount =
                result.Diagnostics.ConnectedRegionCount;
            profile.V1BoundaryEdgeCount =
                result.Diagnostics.BoundaryEdgeCount;
            profile.V1FaceRegimeErrorFraction = MisclassifiedFaceFraction(
                result.FaceComponents, fixture.ExpectedFaceRegime);
            profile.V1BoundaryMaskErrorFraction = LiveEdgeMaskErrorFraction(
                fixture.Mesh,
                result.EdgeBoundaries,
                fixture.ExpectedBoundaryMask);
            const double heightRange =
                SmoothTransitionHeight(1.0)
                - SmoothTransitionHeight(-1.0);
            const double referenceDiagonal =
                std::sqrt(5.0 + heightRange * heightRange);
            profile.Boundary = MeasureVerticalTransitionBoundary(
                fixture.Mesh,
                result.EdgeBoundaries,
                0.0,
                0.5,
                0.0,
                1.0,
                referenceDiagonal);
        }

        profile.Succeeded = classificationSucceeded
            && profile.ClassifiedFeatureEdgeCount == 0u
            && profile.FeatureMaskErrorFraction == 0.0
            && result.Succeeded()
            && profile.V1SelectedComponentCount == 2u
            && profile.V1ConnectedRegionCount == 2u
            && profile.V1BoundaryEdgeCount
                == profile.ExpectedBoundaryEdgeCount
            && profile.V1FaceRegimeErrorFraction <= 0.02
            && profile.V1BoundaryMaskErrorFraction == 0.0
            && profile.SurfaceEquationErrorNormalized <= 1.0e-6
            && profile.FaceNormalsPointUpward
            && profile.AnalyticCurvatureContractSatisfied
            && profile.Boundary.Valid
            && profile.Boundary.SymmetricHausdorffUpperBoundNormalized
                <= 0.02
            && profile.Boundary.EndpointCount == 2u
            && profile.Boundary.JunctionCount == 0u;
        return profile;
    }

    [[nodiscard]] SurfaceScreeningProfile RunSurfaceScreening()
    {
        const ProfileClock::time_point start = ProfileClock::now();
        SurfaceScreeningProfile profile{};
        profile.Cylinder = RunCylinderControls();
        profile.SmoothTransition[0u] = RunSmoothTransitionVariant(false);
        profile.SmoothTransition[1u] = RunSmoothTransitionVariant(true);
        profile.MaxControlErrorFraction = 0.0;
        profile.MaxGeometricOrBoundaryErrorNormalized = std::max(
            profile.Cylinder.MaxRadialErrorNormalized,
            profile.Cylinder.MaxPairedEdgeLengthDeltaNormalized);
        profile.Succeeded = profile.Cylinder.Succeeded;
        for (const CylinderControlVariantProfile& variant :
             profile.Cylinder.Variants)
        {
            profile.MaxControlErrorFraction = std::max(
                profile.MaxControlErrorFraction,
                variant.FeatureMaskErrorFraction);
        }
        for (const SmoothTransitionVariantProfile& variant :
             profile.SmoothTransition)
        {
            profile.MaxControlErrorFraction = std::max({
                profile.MaxControlErrorFraction,
                variant.FeatureMaskErrorFraction,
                variant.V1FaceRegimeErrorFraction,
                variant.V1BoundaryMaskErrorFraction});
            profile.MaxGeometricOrBoundaryErrorNormalized = std::max(
                profile.MaxGeometricOrBoundaryErrorNormalized,
                std::max(
                    variant.SurfaceEquationErrorNormalized,
                    variant.Boundary
                        .SymmetricHausdorffUpperBoundNormalized));
            profile.Succeeded &= variant.Succeeded;
        }
        profile.Succeeded &= profile.MaxControlErrorFraction <= 0.02;
        profile.Succeeded &= profile.MaxGeometricOrBoundaryErrorNormalized
            <= 0.02;
        profile.RuntimeMilliseconds = ElapsedMilliseconds(start);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        return profile;
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

    [[nodiscard]] std::string FoldScreeningBenchmarkId()
    {
        return "geometry.curvature_segmentation.screening.fold_controls";
    }

    [[nodiscard]] std::string SurfaceScreeningBenchmarkId()
    {
        return "geometry.curvature_segmentation.screening.surface_controls";
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

    [[nodiscard]] std::string EmitFoldScreeningResult(
        const FoldScreeningProfile& profile,
        const std::string& commit)
    {
        const std::string benchmarkId = FoldScreeningBenchmarkId();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \"builtin.fold_threshold_triplet.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << profile.RuntimeMilliseconds << ",\n"
            << "    \"memory_peak_bytes\": "
            << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": "
            << profile.MaxFeatureMaskErrorFraction << ",\n"
            << "    \"quality_error_linf\": "
            << profile.MaxIsometricEdgeLengthDeltaNormalized << ",\n"
            << "    \"population_count\": " << profile.Variants.size()
            << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicCurvatureSegmentationProfile\",\n"
            << "    \"implementation_version\": \"slice_a_control_v1\",\n"
            << "    \"candidate_selection\": \"none\",\n"
            << "    \"dihedral_threshold_degrees\": 45.000000000,\n"
            << "    \"threshold_comparison\": \"strictly_greater\",\n"
            << "    \"boundary_is_feature\": false,\n"
            << "    \"triangulations\": "
            << "[\"diagonal_a\",\"diagonal_b\"],\n"
            << "    \"curvature_lane\": \"constant_supplied_negative_control\",\n"
            << "    \"quality_error_l2_unit\": "
            << "\"maximum_live_edge_feature_mask_mismatch_fraction\",\n"
            << "    \"quality_error_linf_unit\": "
            << "\"bbox_normalized_flat_fold_edge_length_delta\",\n"
            << "    \"variants\": [";
        for (std::size_t index = 0u; index < profile.Variants.size(); ++index)
        {
            if (index != 0u)
                out << ',';
            const FoldVariantProfile& variant = profile.Variants[index];
            out << "{\"dihedral_degrees\":" << variant.DihedralDegrees
                << ",\"face_count\":" << variant.FaceCount
                << ",\"edge_count\":" << variant.EdgeCount
                << ",\"expected_feature_edge_count\":"
                << variant.ExpectedFeatureEdgeCount
                << ",\"classified_feature_edge_counts\":["
                << variant.ClassifiedFeatureEdgeCounts[0u] << ','
                << variant.ClassifiedFeatureEdgeCounts[1u] << ']'
                << ",\"feature_mask_error_fraction\":"
                << variant.FeatureMaskErrorFraction
                << ",\"v1_selected_component_count\":"
                << variant.V1SelectedComponentCount
                << ",\"v1_connected_region_count\":"
                << variant.V1ConnectedRegionCount
                << ",\"v1_boundary_edge_count\":"
                << variant.V1BoundaryEdgeCount
                << ",\"v1_matches_flat_control\":"
                << (variant.V1MatchesFlatControl ? "true" : "false")
                << ",\"passed\":"
                << (variant.Succeeded ? "true" : "false") << '}';
        }
        out << "]\n"
            << "  },\n"
            << "  \"status\": \""
            << (profile.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] std::string EmitSurfaceScreeningResult(
        const SurfaceScreeningProfile& profile,
        const std::string& commit)
    {
        const std::string benchmarkId = SurfaceScreeningBenchmarkId();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": "
            << "\"builtin.cylinder_smooth_transition_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << profile.RuntimeMilliseconds << ",\n"
            << "    \"memory_peak_bytes\": "
            << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": "
            << profile.MaxControlErrorFraction << ",\n"
            << "    \"quality_error_linf\": "
            << profile.MaxGeometricOrBoundaryErrorNormalized << ",\n"
            << "    \"population_count\": 4\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicCurvatureSegmentationProfile\",\n"
            << "    \"implementation_version\": "
            << "\"slice_a_surface_control_v1\",\n"
            << "    \"candidate_selection\": \"none\",\n"
            << "    \"warmup_iterations\": 0,\n"
            << "    \"measured_iterations\": 1,\n"
            << "    \"dihedral_threshold_degrees\": 45.000000000,\n"
            << "    \"threshold_comparison\": \"strictly_greater\",\n"
            << "    \"boundary_is_feature\": false,\n"
            << "    \"quality_error_l2_unit\": "
            << "\"maximum_control_fraction\",\n"
            << "    \"quality_error_linf_unit\": "
            << "\"maximum_bbox_normalized_geometric_or_boundary_error\",\n"
            << "    \"cylinder\": {"
            << "\"radius\":1.000000000"
            << ",\"length\":2.000000000"
            << ",\"axial_cells\":32"
            << ",\"angular_cells\":64"
            << ",\"angular_phase_turns\":[0.000000000,0.007812500]"
            << ",\"max_radial_error_normalized\":"
            << profile.Cylinder.MaxRadialErrorNormalized
            << ",\"max_paired_edge_length_delta_normalized\":"
            << profile.Cylinder.MaxPairedEdgeLengthDeltaNormalized
            << ",\"v1_payloads_match\":"
            << (profile.Cylinder.V1PayloadsMatch ? "true" : "false")
            << ",\"variants\":[";
        for (std::size_t index = 0u;
             index < profile.Cylinder.Variants.size();
             ++index)
        {
            if (index != 0u)
                out << ',';
            const CylinderControlVariantProfile& variant =
                profile.Cylinder.Variants[index];
            out << "{\"name\":\"" << variant.Name << "\""
                << ",\"face_count\":" << variant.FaceCount
                << ",\"edge_count\":" << variant.EdgeCount
                << ",\"classified_feature_edge_count\":"
                << variant.ClassifiedFeatureEdgeCount
                << ",\"feature_mask_error_fraction\":"
                << variant.FeatureMaskErrorFraction
                << ",\"v1_selected_component_count\":"
                << variant.V1SelectedComponentCount
                << ",\"v1_connected_region_count\":"
                << variant.V1ConnectedRegionCount
                << ",\"v1_boundary_edge_count\":"
                << variant.V1BoundaryEdgeCount
                << ",\"face_normals_point_outward\":"
                << (variant.FaceNormalsPointOutward ? "true" : "false")
                << ",\"passed\":"
                << (variant.Succeeded ? "true" : "false") << '}';
        }
        out << "]},\n"
            << "    \"smooth_transition\": {"
            << "\"graph\":\"z=q(x)\""
            << ",\"width\":0.080000000"
            << ",\"rows\":24"
            << ",\"columns\":48"
            << ",\"reference_curve\":\"x=0,z=0.5,y in [0,1]\""
            << ",\"normalized_boundary_tolerance\":0.020000000"
            << ",\"variants\":[";
        for (std::size_t index = 0u;
             index < profile.SmoothTransition.size();
             ++index)
        {
            if (index != 0u)
                out << ',';
            const SmoothTransitionVariantProfile& variant =
                profile.SmoothTransition[index];
            const BoundaryProfile& boundary = variant.Boundary;
            out << "{\"name\":\"" << variant.Name << "\""
                << ",\"face_count\":" << variant.FaceCount
                << ",\"edge_count\":" << variant.EdgeCount
                << ",\"expected_boundary_edge_count\":"
                << variant.ExpectedBoundaryEdgeCount
                << ",\"classified_feature_edge_count\":"
                << variant.ClassifiedFeatureEdgeCount
                << ",\"feature_mask_error_fraction\":"
                << variant.FeatureMaskErrorFraction
                << ",\"v1_selected_component_count\":"
                << variant.V1SelectedComponentCount
                << ",\"v1_connected_region_count\":"
                << variant.V1ConnectedRegionCount
                << ",\"v1_boundary_edge_count\":"
                << variant.V1BoundaryEdgeCount
                << ",\"v1_face_regime_error_fraction\":"
                << variant.V1FaceRegimeErrorFraction
                << ",\"v1_boundary_mask_error_fraction\":"
                << variant.V1BoundaryMaskErrorFraction
                << ",\"surface_equation_error_normalized\":"
                << variant.SurfaceEquationErrorNormalized
                << ",\"face_normals_point_upward\":"
                << (variant.FaceNormalsPointUpward ? "true" : "false")
                << ",\"analytic_curvature_contract_satisfied\":"
                << (variant.AnalyticCurvatureContractSatisfied
                        ? "true"
                        : "false")
                << ",\"continuous_boundary\":{\"valid\":"
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
                << boundary.ReferenceLengthNormalized << '}'
                << ",\"passed\":"
                << (variant.Succeeded ? "true" : "false") << '}';
        }
        out << "]}\n"
            << "  },\n"
            << "  \"status\": \""
            << (profile.Succeeded ? "passed" : "failed") << "\"\n"
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

    [[nodiscard]] bool WriteFoldScreeningResult(
        const std::filesystem::path& outputRoot,
        const FoldScreeningProfile& profile,
        const std::string& commit)
    {
        return WriteRawResult(
            outputRoot,
            FoldScreeningBenchmarkId(),
            EmitFoldScreeningResult(profile, commit));
    }

    [[nodiscard]] bool WriteSurfaceScreeningResult(
        const std::filesystem::path& outputRoot,
        const SurfaceScreeningProfile& profile,
        const std::string& commit)
    {
        return WriteRawResult(
            outputRoot,
            SurfaceScreeningBenchmarkId(),
            EmitSurfaceScreeningResult(profile, commit));
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
    else if (cohort != "fixtures"
             && cohort != "screening"
             && cohort != "surface_controls")
    {
        std::cerr << "INTRINSIC_CURVATURE_PROFILE_COHORT must be "
                     "smoke, fixtures, screening, surface_controls, or "
                     "heavy\n";
        return 2;
    }

    const std::filesystem::path outputRoot{argv[1]};
    const std::string commit = ResolveCommit();
    bool allPassed = true;
    if (cohort == "screening")
    {
        const FoldScreeningProfile profile = RunFoldScreening();
        if (!WriteFoldScreeningResult(outputRoot, profile, commit))
        {
            std::cerr << "failed to write "
                      << FoldScreeningBenchmarkId() << '\n';
            return 1;
        }
        std::cout << "Wrote " << FoldScreeningBenchmarkId() << '\n';
        return profile.Succeeded ? 0 : 1;
    }
    if (cohort == "surface_controls")
    {
        const SurfaceScreeningProfile profile = RunSurfaceScreening();
        if (!WriteSurfaceScreeningResult(outputRoot, profile, commit))
        {
            std::cerr << "failed to write "
                      << SurfaceScreeningBenchmarkId() << '\n';
            return 1;
        }
        std::cout << "Wrote " << SurfaceScreeningBenchmarkId() << '\n';
        return profile.Succeeded ? 0 : 1;
    }
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
