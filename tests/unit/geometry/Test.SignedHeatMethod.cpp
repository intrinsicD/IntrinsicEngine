#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    struct GridMesh
    {
        Geometry::HalfedgeMesh::Mesh Mesh;
        std::vector<Geometry::VertexHandle> Vertices;
        int Columns{0};
        float Step{0.0f};

        [[nodiscard]] Geometry::VertexHandle At(int x, int y) const
        {
            return Vertices[static_cast<std::size_t>(y * (Columns + 1) + x)];
        }
    };

    [[nodiscard]] GridMesh MakeFlatGrid(int columns)
    {
        GridMesh grid;
        grid.Columns = columns;
        grid.Step = 2.0f / static_cast<float>(columns);
        grid.Vertices.reserve(static_cast<std::size_t>((columns + 1) * (columns + 1)));

        for (int y = 0; y <= columns; ++y)
        {
            for (int x = 0; x <= columns; ++x)
            {
                const float px = -1.0f + grid.Step * static_cast<float>(x);
                const float py = -1.0f + grid.Step * static_cast<float>(y);
                grid.Vertices.push_back(grid.Mesh.AddVertex({px, py, 0.0f}));
            }
        }

        for (int y = 0; y < columns; ++y)
        {
            for (int x = 0; x < columns; ++x)
            {
                const auto v00 = grid.At(x, y);
                const auto v10 = grid.At(x + 1, y);
                const auto v01 = grid.At(x, y + 1);
                const auto v11 = grid.At(x + 1, y + 1);
                (void)grid.Mesh.AddTriangle(v00, v10, v11);
                (void)grid.Mesh.AddTriangle(v00, v11, v01);
            }
        }

        return grid;
    }

    [[nodiscard]] std::vector<Geometry::HalfedgeHandle> MakeSquareBoundary(
        const GridMesh& grid,
        bool reverse = false)
    {
        constexpr int lo = 2;
        constexpr int hi = 6;
        std::vector<Geometry::VertexHandle> loop;
        for (int x = lo; x < hi; ++x) loop.push_back(grid.At(x, lo));
        for (int y = lo; y < hi; ++y) loop.push_back(grid.At(hi, y));
        for (int x = hi; x > lo; --x) loop.push_back(grid.At(x, hi));
        for (int y = hi; y > lo; --y) loop.push_back(grid.At(lo, y));

        std::vector<Geometry::HalfedgeHandle> boundary;
        boundary.reserve(loop.size());
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const Geometry::VertexHandle from = reverse ? loop[(i + 1) % loop.size()] : loop[i];
            const Geometry::VertexHandle to = reverse ? loop[i] : loop[(i + 1) % loop.size()];
            auto h = grid.Mesh.FindHalfedge(from, to);
            EXPECT_TRUE(h.has_value());
            if (h.has_value())
            {
                boundary.push_back(*h);
            }
        }
        return boundary;
    }

    [[nodiscard]] double ExpectedSquareSdfInsidePositive(const glm::vec3& p)
    {
        constexpr double halfExtent = 0.5;
        const double ax = std::abs(static_cast<double>(p.x));
        const double ay = std::abs(static_cast<double>(p.y));
        const double outsideX = std::max(ax - halfExtent, 0.0);
        const double outsideY = std::max(ay - halfExtent, 0.0);
        const double outsideDistance = std::sqrt(outsideX * outsideX + outsideY * outsideY);
        if (ax <= halfExtent && ay <= halfExtent)
        {
            return halfExtent - std::max(ax, ay);
        }
        return -outsideDistance;
    }

    [[nodiscard]] double RootMeanSquareError(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const Geometry::VertexProperty<double>& values)
    {
        double sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
            if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
            {
                continue;
            }
            const double expected = ExpectedSquareSdfInsidePositive(mesh.Position(v));
            const double actual = values[v];
            sumSq += (actual - expected) * (actual - expected);
            ++count;
        }
        return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
    }

    void ExpectFiniteDistances(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const Geometry::VertexProperty<double>& values)
    {
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
            if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v))
            {
                EXPECT_TRUE(std::isfinite(values[v])) << "vertex " << vi;
            }
        }
    }
}

TEST(SignedHeatMethod, ClosedSquareProducesFiniteSignedField)
{
    GridMesh grid = MakeFlatGrid(8);
    const std::vector<Geometry::HalfedgeHandle> boundary = MakeSquareBoundary(grid);

    const Geometry::SignedHeatMethod::SignedHeatResult result =
        Geometry::SignedHeatMethod::ComputeSignedDistance(grid.Mesh, boundary);

    ASSERT_TRUE(result.Diagnostics.Succeeded());
    EXPECT_EQ(result.Diagnostics.Status, Geometry::SignedHeatMethod::SignedHeatStatus::Success);
    EXPECT_EQ(result.Diagnostics.InvalidBoundaryHalfedgeCount, 0u);
    EXPECT_EQ(result.Diagnostics.DegenerateBoundaryVertexCount, 0u);
    ExpectFiniteDistances(grid.Mesh, result.SignedDistanceProperty);

    const Geometry::VertexHandle center = grid.At(4, 4);
    const Geometry::VertexHandle lowerLeft = grid.At(0, 0);
    EXPECT_GT(result.SignedDistanceProperty[center], 0.05);
    EXPECT_LT(result.SignedDistanceProperty[lowerLeft], -0.05);
    EXPECT_NEAR(result.Diagnostics.MeanBoundaryOffset, 0.0, 1.0e-8);
    EXPECT_LT(RootMeanSquareError(grid.Mesh, result.SignedDistanceProperty), 0.40);
}

TEST(SignedHeatMethod, ReversingBoundaryFlipsSign)
{
    GridMesh forwardGrid = MakeFlatGrid(8);
    const std::vector<Geometry::HalfedgeHandle> forwardBoundary = MakeSquareBoundary(forwardGrid);
    const Geometry::SignedHeatMethod::SignedHeatResult forward =
        Geometry::SignedHeatMethod::ComputeSignedDistance(forwardGrid.Mesh, forwardBoundary);
    ASSERT_TRUE(forward.Diagnostics.Succeeded());

    GridMesh reverseGrid = MakeFlatGrid(8);
    const std::vector<Geometry::HalfedgeHandle> reverseBoundary = MakeSquareBoundary(reverseGrid, true);
    const Geometry::SignedHeatMethod::SignedHeatResult reverse =
        Geometry::SignedHeatMethod::ComputeSignedDistance(reverseGrid.Mesh, reverseBoundary);
    ASSERT_TRUE(reverse.Diagnostics.Succeeded());

    const Geometry::VertexHandle center = forwardGrid.At(4, 4);
    EXPECT_GT(forward.SignedDistanceProperty[center], 0.05);
    EXPECT_LT(reverse.SignedDistanceProperty[center], -0.05);
    EXPECT_NEAR(
        forward.SignedDistanceProperty[center],
        -reverse.SignedDistanceProperty[center],
        1.0e-8);
}

TEST(SignedHeatMethod, OpenBoundaryReportsDegenerateButFinite)
{
    GridMesh grid = MakeFlatGrid(8);
    std::vector<Geometry::HalfedgeHandle> boundary = MakeSquareBoundary(grid);
    boundary.resize(boundary.size() - 1);

    const Geometry::SignedHeatMethod::SignedHeatResult result =
        Geometry::SignedHeatMethod::ComputeSignedDistance(grid.Mesh, boundary);

    EXPECT_EQ(
        result.Diagnostics.Status,
        Geometry::SignedHeatMethod::SignedHeatStatus::DegenerateBoundaryInput);
    EXPECT_TRUE(result.Diagnostics.Succeeded());
    EXPECT_TRUE(result.Diagnostics.BoundaryDegenerate());
    EXPECT_GT(result.Diagnostics.DegenerateBoundaryVertexCount, 0u);
    ExpectFiniteDistances(grid.Mesh, result.SignedDistanceProperty);
}

TEST(SignedHeatMethod, InvalidInputsFailClosed)
{
    Geometry::HalfedgeMesh::Mesh empty;
    const Geometry::SignedHeatMethod::SignedHeatResult emptyResult =
        Geometry::SignedHeatMethod::ComputeSignedDistance(empty, {});
    EXPECT_EQ(emptyResult.Diagnostics.Status, Geometry::SignedHeatMethod::SignedHeatStatus::InvalidInput);
    EXPECT_FALSE(emptyResult.Diagnostics.Succeeded());

    GridMesh grid = MakeFlatGrid(8);
    const Geometry::SignedHeatMethod::SignedHeatResult noBoundary =
        Geometry::SignedHeatMethod::ComputeSignedDistance(grid.Mesh, {});
    EXPECT_EQ(noBoundary.Diagnostics.Status, Geometry::SignedHeatMethod::SignedHeatStatus::InvalidInput);

    Geometry::SignedHeatMethod::SignedHeatParams badParams;
    badParams.PoissonRegularization = 0.0;
    const std::vector<Geometry::HalfedgeHandle> boundary = MakeSquareBoundary(grid);
    const Geometry::SignedHeatMethod::SignedHeatResult badParamsResult =
        Geometry::SignedHeatMethod::ComputeSignedDistance(grid.Mesh, boundary, badParams);
    EXPECT_EQ(badParamsResult.Diagnostics.Status, Geometry::SignedHeatMethod::SignedHeatStatus::InvalidInput);
}

TEST(SignedHeatMethod, IdenticalInputsAreBitStable)
{
    GridMesh firstGrid = MakeFlatGrid(8);
    const std::vector<Geometry::HalfedgeHandle> firstBoundary = MakeSquareBoundary(firstGrid);
    const Geometry::SignedHeatMethod::SignedHeatResult first =
        Geometry::SignedHeatMethod::ComputeSignedDistance(firstGrid.Mesh, firstBoundary);
    ASSERT_TRUE(first.Diagnostics.Succeeded());

    GridMesh secondGrid = MakeFlatGrid(8);
    const std::vector<Geometry::HalfedgeHandle> secondBoundary = MakeSquareBoundary(secondGrid);
    const Geometry::SignedHeatMethod::SignedHeatResult second =
        Geometry::SignedHeatMethod::ComputeSignedDistance(secondGrid.Mesh, secondBoundary);
    ASSERT_TRUE(second.Diagnostics.Succeeded());

    ASSERT_EQ(firstGrid.Mesh.VerticesSize(), secondGrid.Mesh.VerticesSize());
    for (std::size_t vi = 0; vi < firstGrid.Mesh.VerticesSize(); ++vi)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        EXPECT_DOUBLE_EQ(first.SignedDistanceProperty[v], second.SignedDistanceProperty[v]);
    }
    EXPECT_EQ(first.Diagnostics.Status, second.Diagnostics.Status);
    EXPECT_DOUBLE_EQ(first.Diagnostics.MaxAbsDistance, second.Diagnostics.MaxAbsDistance);
}
