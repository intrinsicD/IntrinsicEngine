// METHOD-023 — Boundary First Flattening CPU reference correctness contract.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    namespace Param = Geometry::Parameterization;
    constexpr double kPi = 3.141592653589793238462643383279502884;

    Geometry::HalfedgeMesh::Mesh MakeSquareFan(const float centerHeight = 0.0f)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto c0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto c1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto c2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
        const auto c3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        const auto center = mesh.AddVertex({0.5f, 0.5f, centerHeight});
        (void)mesh.AddTriangle(center, c0, c1);
        (void)mesh.AddTriangle(center, c1, c2);
        (void)mesh.AddTriangle(center, c2, c3);
        (void)mesh.AddTriangle(center, c3, c0);
        return mesh;
    }

    Geometry::HalfedgeMesh::Mesh MakeCurvedGridDisk(const std::size_t cells)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const std::size_t side = cells + 1u;
        std::vector<Geometry::VertexHandle> vertices(side * side);
        for (std::size_t row = 0u; row < side; ++row)
        {
            for (std::size_t column = 0u; column < side; ++column)
            {
                const float x = static_cast<float>(column)
                    / static_cast<float>(cells);
                const float y = static_cast<float>(row)
                    / static_cast<float>(cells);
                const float z = 0.2f
                    * std::sin(static_cast<float>(kPi) * x)
                    * std::sin(static_cast<float>(kPi) * y);
                vertices[row * side + column] =
                    mesh.AddVertex({x, y, z});
            }
        }
        const auto at = [&vertices, side](const std::size_t row,
                                           const std::size_t column)
        {
            return vertices[row * side + column];
        };
        for (std::size_t row = 0u; row < cells; ++row)
        {
            for (std::size_t column = 0u; column < cells; ++column)
            {
                const auto v00 = at(row, column);
                const auto v10 = at(row, column + 1u);
                const auto v11 = at(row + 1u, column + 1u);
                const auto v01 = at(row + 1u, column);
                (void)mesh.AddTriangle(v00, v10, v11);
                (void)mesh.AddTriangle(v00, v11, v01);
            }
        }
        return mesh;
    }

    Geometry::HalfedgeMesh::Mesh MakeScaledSquareFan(const float scale)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto c0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto c1 = mesh.AddVertex({scale, 0.0f, 0.0f});
        const auto c2 = mesh.AddVertex({scale, scale, 0.0f});
        const auto c3 = mesh.AddVertex({0.0f, scale, 0.0f});
        const auto center = mesh.AddVertex(
            {0.5f * scale, 0.5f * scale, 0.2f * scale});
        (void)mesh.AddTriangle(center, c0, c1);
        (void)mesh.AddTriangle(center, c1, c2);
        (void)mesh.AddTriangle(center, c2, c3);
        (void)mesh.AddTriangle(center, c3, c0);
        return mesh;
    }

    Geometry::HalfedgeMesh::Mesh MakeIrregularTriangle()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto b = mesh.AddVertex({3.0f, 0.0f, 0.0f});
        const auto c = mesh.AddVertex({0.5f, 1.5f, 0.0f});
        (void)mesh.AddTriangle(a, b, c);
        return mesh;
    }

    Geometry::HalfedgeMesh::Mesh MakeCollinearTriangle()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto b = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto c = mesh.AddVertex({2.0f, 0.0f, 0.0f});
        (void)mesh.AddTriangle(a, b, c);
        return mesh;
    }

    void ExpectFiniteUvs(const std::vector<glm::vec2>& uvs)
    {
        for (std::size_t i = 0u; i < uvs.size(); ++i)
        {
            EXPECT_TRUE(std::isfinite(uvs[i].x)) << "vertex " << i;
            EXPECT_TRUE(std::isfinite(uvs[i].y)) << "vertex " << i;
        }
    }

    void ExpectUvsEqual(
        const std::vector<glm::vec2>& lhs,
        const std::vector<glm::vec2>& rhs)
    {
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            EXPECT_EQ(lhs[i].x, rhs[i].x) << "vertex " << i << " u";
            EXPECT_EQ(lhs[i].y, rhs[i].y) << "vertex " << i << " v";
        }
    }

    std::vector<double> BoundaryLengths(
        const std::vector<Geometry::VertexHandle>& boundary,
        const std::vector<glm::vec2>& uvs)
    {
        std::vector<double> lengths(boundary.size(), 0.0);
        for (std::size_t i = 0u; i < boundary.size(); ++i)
        {
            lengths[i] = glm::distance(
                glm::dvec2(uvs[boundary[i].Index]),
                glm::dvec2(uvs[boundary[(i + 1u) % boundary.size()].Index]));
        }
        return lengths;
    }

    std::vector<double> NormalizedBoundaryTurns(
        const std::vector<Geometry::VertexHandle>& boundary,
        const std::vector<glm::vec2>& uvs)
    {
        std::vector<double> turns(boundary.size(), 0.0);
        double sum = 0.0;
        for (std::size_t i = 0u; i < boundary.size(); ++i)
        {
            const glm::dvec2 previous(
                uvs[boundary[(i + boundary.size() - 1u) % boundary.size()].Index]);
            const glm::dvec2 vertex(uvs[boundary[i].Index]);
            const glm::dvec2 next(
                uvs[boundary[(i + 1u) % boundary.size()].Index]);
            const glm::dvec2 incoming = vertex - previous;
            const glm::dvec2 outgoing = next - vertex;
            turns[i] = std::atan2(
                incoming.x * outgoing.y - incoming.y * outgoing.x,
                glm::dot(incoming, outgoing));
            sum += turns[i];
        }
        if (sum < 0.0)
        {
            for (double& turn : turns)
                turn = -turn;
        }
        return turns;
    }
}

TEST(BoundaryFirstFlattening, AutomaticPlanarDiskIsDeterministicAndConformal)
{
    const auto mesh = MakeSquareFan();
    const Param::BffResult first = Param::ComputeBFF(mesh);
    const Param::BffResult second = Param::ComputeBFF(mesh);

    ASSERT_TRUE(first.Succeeded()) << Param::ToString(first.Status);
    ASSERT_TRUE(second.Succeeded()) << Param::ToString(second.Status);
    ASSERT_EQ(first.UVs.size(), mesh.VerticesSize());
    ExpectFiniteUvs(first.UVs);
    ExpectUvsEqual(first.UVs, second.UVs);
    EXPECT_EQ(first.Diagnostics.Backend, "cpu_reference");
    EXPECT_EQ(first.Diagnostics.BoundaryVertexCount, 4u);
    EXPECT_EQ(first.Diagnostics.InteriorVertexCount, 1u);
    EXPECT_EQ(first.Diagnostics.Quality.Status,
              Param::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(first.Diagnostics.Quality.FlippedElementCount, 0u);
    EXPECT_NEAR(first.Diagnostics.Quality.MeanConformalDistortion, 1.0, 1.0e-4);
}

TEST(BoundaryFirstFlattening, AutomaticCurvedDiskMeetsAbsoluteQualityBounds)
{
    const auto mesh = MakeCurvedGridDisk(4u);
    const Param::BffResult result = Param::ComputeBFF(mesh);

    ASSERT_TRUE(result.Succeeded()) << Param::ToString(result.Status);
    ExpectFiniteUvs(result.UVs);
    EXPECT_EQ(result.Diagnostics.Quality.EvaluatedFaceCount, mesh.FaceCount());
    EXPECT_EQ(result.Diagnostics.Quality.FlippedElementCount, 0u);
    EXPECT_TRUE(std::isfinite(
        result.Diagnostics.Quality.MeanConformalDistortion));
    EXPECT_LT(result.Diagnostics.Quality.MeanConformalError, 1.0);
    EXPECT_LT(result.Diagnostics.Quality.MaxConformalDistortion, 1.25);
    EXPECT_LT(result.Diagnostics.Quality.RootMeanSquareConformalError, 0.2);
}

TEST(BoundaryFirstFlattening, AutomaticModeIsInvariantToUniformInputScale)
{
    const Param::BffResult unit = Param::ComputeBFF(MakeScaledSquareFan(1.0f));
    const Param::BffResult small = Param::ComputeBFF(MakeScaledSquareFan(1.0e-6f));
    const Param::BffResult large = Param::ComputeBFF(MakeScaledSquareFan(1.0e6f));

    ASSERT_TRUE(unit.Succeeded()) << Param::ToString(unit.Status);
    ASSERT_TRUE(small.Succeeded()) << Param::ToString(small.Status);
    ASSERT_TRUE(large.Succeeded()) << Param::ToString(large.Status);
    EXPECT_EQ(small.Diagnostics.Quality.FlippedElementCount, 0u);
    EXPECT_EQ(large.Diagnostics.Quality.FlippedElementCount, 0u);
    EXPECT_NEAR(
        small.Diagnostics.Quality.RootMeanSquareConformalError,
        unit.Diagnostics.Quality.RootMeanSquareConformalError,
        1.0e-4);
    EXPECT_NEAR(
        large.Diagnostics.Quality.RootMeanSquareConformalError,
        unit.Diagnostics.Quality.RootMeanSquareConformalError,
        1.0e-4);
}

TEST(BoundaryFirstFlattening, UniformTargetLengthsAreRealized)
{
    const auto mesh = MakeSquareFan();
    Param::BffParams params{};
    params.Mode = Param::BffBoundaryMode::TargetLengths;
    params.BoundaryData = {2.0, 2.0, 2.0, 2.0};

    const Param::BffResult result = Param::ComputeBFF(mesh, params);
    const Param::BffResult repeated = Param::ComputeBFF(mesh, params);
    ASSERT_TRUE(result.Succeeded()) << Param::ToString(result.Status);
    ASSERT_TRUE(repeated.Succeeded()) << Param::ToString(repeated.Status);
    ExpectUvsEqual(result.UVs, repeated.UVs);
    EXPECT_LT(result.Diagnostics.RequestedLengthRmsRelativeError, 1.0e-5);
    EXPECT_LT(result.Diagnostics.RequestedLengthMaxRelativeError, 1.0e-5);
    EXPECT_LT(result.Diagnostics.ClosureAdjustmentRmsRelative, 1.0e-8);
}

TEST(BoundaryFirstFlattening, NonIntegrableLengthRequestReportsApproximation)
{
    const auto mesh = MakeSquareFan();
    Param::BffParams params{};
    params.Mode = Param::BffBoundaryMode::TargetLengths;
    params.BoundaryData = {1.25, 1.0, 1.0, 1.0};

    const Param::BffResult result = Param::ComputeBFF(mesh, params);
    ASSERT_TRUE(result.Succeeded()) << Param::ToString(result.Status);
    EXPECT_GT(result.Diagnostics.RequestedLengthRmsRelativeError, 1.0e-6);
    EXPECT_LT(result.Diagnostics.RequestedLengthRmsRelativeError, 0.25);
    EXPECT_GT(result.Diagnostics.ClosureAdjustmentRmsRelative, 0.0);
}

TEST(BoundaryFirstFlattening, TargetAnglesPreserveSquareCorners)
{
    const auto mesh = MakeSquareFan(0.2f);
    Param::BffParams params{};
    params.Mode = Param::BffBoundaryMode::TargetAngles;
    params.BoundaryData = {
        0.5 * kPi, 0.5 * kPi, 0.5 * kPi, 0.5 * kPi};

    const Param::BffResult result = Param::ComputeBFF(mesh, params);
    ASSERT_TRUE(result.Succeeded()) << Param::ToString(result.Status);
    EXPECT_NEAR(result.Diagnostics.TargetAngleSum, 2.0 * kPi, 1.0e-12);
    EXPECT_LT(result.Diagnostics.TargetAngleRmsError, 1.0e-5);
    EXPECT_LT(result.Diagnostics.TargetAngleMaxError, 1.0e-5);
    EXPECT_EQ(result.Diagnostics.Quality.FlippedElementCount, 0u);
}

TEST(BoundaryFirstFlattening, IrregularTriangleUsesPerEdgeClosureWeights)
{
    const auto mesh = MakeIrregularTriangle();
    const auto loops = Geometry::MeshUtils::CollectBoundaryLoops(mesh);
    ASSERT_EQ(loops.size(), 1u);
    const auto& boundary = loops.front().Vertices;
    ASSERT_EQ(boundary.size(), 3u);

    const std::vector<double> knownScaleFactors{
        0.0, std::log(1.35), std::log(0.75)};
    std::vector<std::size_t> boundaryOf(
        mesh.VerticesSize(), std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0u; i < boundary.size(); ++i)
        boundaryOf[boundary[i].Index] = i;

    std::vector<double> originalLengths(boundary.size(), 0.0);
    std::vector<double> targetAngles(boundary.size(), 0.0);
    for (std::size_t i = 0u; i < boundary.size(); ++i)
    {
        const auto previous = boundary[
            (i + boundary.size() - 1u) % boundary.size()];
        const auto vertex = boundary[i];
        const auto next = boundary[(i + 1u) % boundary.size()];
        const glm::dvec3 p(mesh.Position(vertex));
        const glm::dvec3 a = glm::dvec3(mesh.Position(previous)) - p;
        const glm::dvec3 b = glm::dvec3(mesh.Position(next)) - p;
        const double interiorAngle = std::atan2(
            glm::length(glm::cross(a, b)), glm::dot(a, b));
        double laplacian = 0.0;
        for (const auto halfedge : mesh.HalfedgesAroundVertex(vertex))
        {
            const auto neighbor = mesh.ToVertex(halfedge);
            const double weight = Geometry::MeshUtils::EdgeCotanWeight(
                mesh, mesh.Edge(halfedge));
            laplacian += weight
                * (knownScaleFactors[i]
                   - knownScaleFactors[boundaryOf[neighbor.Index]]);
        }
        targetAngles[i] = kPi - interiorAngle + laplacian;
        originalLengths[i] = glm::distance(
            glm::dvec3(mesh.Position(vertex)),
            glm::dvec3(mesh.Position(next)));
    }
    EXPECT_NEAR(
        std::accumulate(targetAngles.begin(), targetAngles.end(), 0.0),
        2.0 * kPi,
        1.0e-12);

    std::vector<double> scaledLengths(boundary.size(), 0.0);
    std::vector<glm::dvec2> tangents(boundary.size(), glm::dvec2(0.0));
    tangents[0] = {1.0, 0.0};
    double direction = 0.0;
    for (std::size_t i = 0u; i < boundary.size(); ++i)
    {
        const std::size_t next = (i + 1u) % boundary.size();
        scaledLengths[i] = originalLengths[i]
            * std::exp(0.5 * (knownScaleFactors[i]
                              + knownScaleFactors[next]));
        if (i > 0u)
        {
            direction += targetAngles[i];
            tangents[i] = {std::cos(direction), std::sin(direction)};
        }
    }

    double m00 = 0.0;
    double m01 = 0.0;
    double m11 = 0.0;
    glm::dvec2 residual(0.0);
    for (std::size_t i = 0u; i < boundary.size(); ++i)
    {
        m00 += originalLengths[i] * tangents[i].x * tangents[i].x;
        m01 += originalLengths[i] * tangents[i].x * tangents[i].y;
        m11 += originalLengths[i] * tangents[i].y * tangents[i].y;
        residual += scaledLengths[i] * tangents[i];
    }
    const double determinant = m00 * m11 - m01 * m01;
    ASSERT_GT(std::abs(determinant), 1.0e-12);
    const glm::dvec2 multiplier{
        (m11 * residual.x - m01 * residual.y) / determinant,
        (-m01 * residual.x + m00 * residual.y) / determinant};
    std::vector<double> expectedLengths(boundary.size(), 0.0);
    double expectedRelativeSquared = 0.0;
    for (std::size_t i = 0u; i < boundary.size(); ++i)
    {
        const double correction = originalLengths[i]
            * glm::dot(tangents[i], multiplier);
        expectedLengths[i] = scaledLengths[i] - correction;
        ASSERT_GT(expectedLengths[i], 0.0);
        const double relative = std::abs(correction) / scaledLengths[i];
        expectedRelativeSquared += relative * relative;
    }

    Param::BffParams params{};
    params.Mode = Param::BffBoundaryMode::TargetAngles;
    params.BoundaryData = targetAngles;
    const Param::BffResult result = Param::ComputeBFF(mesh, params);
    const Param::BffResult repeated = Param::ComputeBFF(mesh, params);
    ASSERT_TRUE(result.Succeeded()) << Param::ToString(result.Status);
    ASSERT_TRUE(repeated.Succeeded()) << Param::ToString(repeated.Status);
    ExpectUvsEqual(result.UVs, repeated.UVs);
    EXPECT_EQ(result.Diagnostics.InteriorVertexCount, 0u);
    EXPECT_EQ(result.Diagnostics.DirichletSolveCount, 0u);

    const std::vector<double> actualLengths =
        BoundaryLengths(boundary, result.UVs);
    const std::vector<double> actualTurns =
        NormalizedBoundaryTurns(boundary, result.UVs);
    for (std::size_t i = 0u; i < boundary.size(); ++i)
    {
        EXPECT_NEAR(actualLengths[i], expectedLengths[i], 2.0e-5)
            << "boundary edge " << i;
        EXPECT_NEAR(actualTurns[i], targetAngles[i], 2.0e-5)
            << "boundary vertex " << i;
    }
    EXPECT_NEAR(
        result.Diagnostics.ClosureAdjustmentRmsRelative,
        std::sqrt(expectedRelativeSquared
                  / static_cast<double>(boundary.size())),
        1.0e-10);
}

TEST(BoundaryFirstFlattening, InvalidTopologyAndGeometryFailClosed)
{
    const Geometry::HalfedgeMesh::Mesh empty{};
    EXPECT_EQ(Param::ComputeBFF(empty).Status, Param::BffStatus::EmptyMesh);

    const auto closed = MakeCube();
    EXPECT_EQ(Param::ComputeBFF(closed).Status,
              Param::BffStatus::NotDiskTopology);

    const auto quad = MakeSingleQuad();
    EXPECT_EQ(Param::ComputeBFF(quad).Status,
              Param::BffStatus::NotTriangleMesh);

    const auto degenerate = MakeCollinearTriangle();
    const Param::BffResult degenerateResult = Param::ComputeBFF(degenerate);
    EXPECT_EQ(degenerateResult.Status, Param::BffStatus::DegenerateGeometry);
    EXPECT_TRUE(degenerateResult.UVs.empty());

    auto nonFinite = MakeSquareFan();
    nonFinite.Position(Geometry::VertexHandle{0}) = glm::vec3{
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    const Param::BffResult nonFiniteResult = Param::ComputeBFF(nonFinite);
    EXPECT_EQ(nonFiniteResult.Status, Param::BffStatus::NonFiniteGeometry);
    EXPECT_TRUE(nonFiniteResult.UVs.empty());
}

TEST(BoundaryFirstFlattening, InvalidBoundaryDataFailsClosed)
{
    const auto mesh = MakeSquareFan();

    Param::BffParams automaticWithData{};
    automaticWithData.BoundaryData = {1.0};
    EXPECT_EQ(Param::ComputeBFF(mesh, automaticWithData).Status,
              Param::BffStatus::MismatchedBoundaryArray);

    Param::BffParams mismatched{};
    mismatched.Mode = Param::BffBoundaryMode::TargetLengths;
    mismatched.BoundaryData = {1.0, 1.0, 1.0};
    EXPECT_EQ(Param::ComputeBFF(mesh, mismatched).Status,
              Param::BffStatus::MismatchedBoundaryArray);

    Param::BffParams zeroLength{};
    zeroLength.Mode = Param::BffBoundaryMode::TargetLengths;
    zeroLength.BoundaryData = {1.0, 1.0, 0.0, 1.0};
    EXPECT_EQ(Param::ComputeBFF(mesh, zeroLength).Status,
              Param::BffStatus::NonPositiveTargetLength);

    Param::BffParams nonFinite{};
    nonFinite.Mode = Param::BffBoundaryMode::TargetLengths;
    nonFinite.BoundaryData = {
        1.0, 1.0, std::numeric_limits<double>::quiet_NaN(), 1.0};
    EXPECT_EQ(Param::ComputeBFF(mesh, nonFinite).Status,
              Param::BffStatus::NonFiniteBoundaryData);

    Param::BffParams badAngleSum{};
    badAngleSum.Mode = Param::BffBoundaryMode::TargetAngles;
    badAngleSum.BoundaryData = {1.0, 1.0, 1.0, 1.0};
    EXPECT_EQ(Param::ComputeBFF(mesh, badAngleSum).Status,
              Param::BffStatus::InconsistentAngleSum);

    Param::BffParams overflowingAngleSum{};
    overflowingAngleSum.Mode = Param::BffBoundaryMode::TargetAngles;
    overflowingAngleSum.BoundaryData = {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    EXPECT_EQ(Param::ComputeBFF(mesh, overflowingAngleSum).Status,
              Param::BffStatus::InconsistentAngleSum);

    Param::BffParams invalidMode{};
    invalidMode.Mode = static_cast<Param::BffBoundaryMode>(255u);
    EXPECT_EQ(Param::ComputeBFF(mesh, invalidMode).Status,
              Param::BffStatus::InvalidBoundaryMode);

    Param::BffParams invalidTolerance{};
    invalidTolerance.DegeneracyTolerance = 0.0;
    EXPECT_EQ(Param::ComputeBFF(mesh, invalidTolerance).Status,
              Param::BffStatus::InvalidTolerance);
    invalidTolerance = {};
    invalidTolerance.AngleSumTolerance =
        std::numeric_limits<double>::infinity();
    EXPECT_EQ(Param::ComputeBFF(mesh, invalidTolerance).Status,
              Param::BffStatus::InvalidTolerance);
}

TEST(BoundaryFirstFlattening, TypedDispatchMatchesDirectAndFailsClosed)
{
    const auto mesh = MakeSquareFan(0.1f);
    const Param::BffParams params{};
    const Param::BffResult direct = Param::ComputeBFF(mesh, params);
    ASSERT_TRUE(direct.Succeeded()) << Param::ToString(direct.Status);

    const Param::ParameterizationStrategy strategy{params};
    const Param::ParameterizeResult dispatched =
        Param::ParameterizeMesh(mesh, strategy);
    ASSERT_TRUE(dispatched.Succeeded());
    ExpectUvsEqual(dispatched.UVs, direct.UVs);
    EXPECT_EQ(dispatched.Diagnostics.MeanConformalDistortion,
              direct.Diagnostics.Quality.MeanConformalDistortion);

    const Geometry::HalfedgeMesh::Mesh empty{};
    const Param::ParameterizeResult failed =
        Param::ParameterizeMesh(empty, strategy);
    EXPECT_EQ(failed.Status, Param::ParameterizationStatus::InvalidInput);
    EXPECT_TRUE(failed.UVs.empty());
}
