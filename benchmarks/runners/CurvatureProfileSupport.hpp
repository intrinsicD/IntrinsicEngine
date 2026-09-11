// Shared curvature-profile fixtures and measurement oracles, independent of
// the production segmentation and patch implementations under measurement.
// Include from a non-module runner translation unit.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <sys/resource.h>
#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Intrinsic::Bench::CurvatureProfile
{
    struct CohortSpec
    {
        std::string_view Token{};
        std::uint32_t Rows{0u};
        std::uint32_t Columns{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
    };

    struct Fixture
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<double> K1{};
        std::vector<double> K2{};
        std::vector<std::uint32_t> ExpectedFaceRegime{};
        bool Valid{true};
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

    struct BoundarySegment
    {
        glm::dvec3 A{};
        glm::dvec3 B{};
        double Length{0.0};
    };

    [[nodiscard]] inline std::string ResolveCommit()
    {
        const char* value = std::getenv("GIT_COMMIT");
        return value != nullptr && value[0] != '\0'
            ? std::string{value}
            : std::string{"unknown"};
    }

    [[nodiscard]] inline std::uint64_t PeakWorkingSetBytes() noexcept
    {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0)
            return 0u;
        // Linux reports ru_maxrss in KiB.
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
    }

    [[nodiscard]] inline glm::dvec3 ToDouble(const glm::vec3 value) noexcept
    {
        return {
            static_cast<double>(value.x),
            static_cast<double>(value.y),
            static_cast<double>(value.z),
        };
    }

    [[nodiscard]] inline double PointSegmentDistance(
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

    [[nodiscard]] inline double SegmentFractionInsideVerticalTube(
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

    [[nodiscard]] inline BoundaryProfile MeasureVerticalTransitionBoundary(
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

    [[nodiscard]] inline Fixture MakeGridFixture(
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

    [[nodiscard]] inline bool PopulateGeometricFaceNormals(
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

    [[nodiscard]] inline double SmoothTransitionHeight(const double x) noexcept
    {
        constexpr double kWidth = 0.08;
        return 0.5 * (1.0 + std::tanh(x / kWidth));
    }

    [[nodiscard]] inline double SmoothTransitionCurvature(const double x) noexcept
    {
        constexpr double kWidth = 0.08;
        const double tangent = std::tanh(x / kWidth);
        const double sechSquared = 1.0 - tangent * tangent;
        const double first = 0.5 * sechSquared / kWidth;
        const double second =
            -sechSquared * tangent / (kWidth * kWidth);
        return second / std::pow(1.0 + first * first, 1.5);
    }

    [[nodiscard]] inline SurfaceControlFixture MakeSmoothTransitionFixture(
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

    [[nodiscard]] inline double LiveEdgeMaskErrorFraction(
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
}
