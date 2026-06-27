#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

// =============================================================================
// d0 — Exterior Derivative 0 (vertex → edge)
// =============================================================================

TEST(DEC_ExteriorDerivative0, DimensionsMatchMesh)
{
    auto mesh = MakeSingleTriangle();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);

    EXPECT_EQ(d0.Rows, mesh.EdgesSize());
    EXPECT_EQ(d0.Cols, mesh.VerticesSize());
}

TEST(DEC_ExteriorDerivative0, EachRowHasTwoEntries)
{
    auto mesh = MakeTwoTriangleSquare();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);

    for (std::size_t i = 0; i < d0.Rows; ++i)
    {
        std::size_t nnzInRow = d0.RowOffsets[i + 1] - d0.RowOffsets[i];
        EXPECT_EQ(nnzInRow, 2u) << "Row " << i << " should have exactly 2 entries";
    }
}

TEST(DEC_ExteriorDerivative0, RowSumIsZero)
{
    // Each row has +1 and -1, so row sums should be zero.
    auto mesh = MakeTetrahedron();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);

    for (std::size_t i = 0; i < d0.Rows; ++i)
    {
        double sum = 0.0;
        for (std::size_t k = d0.RowOffsets[i]; k < d0.RowOffsets[i + 1]; ++k)
        {
            sum += d0.Values[k];
        }
        EXPECT_NEAR(sum, 0.0, 1e-12) << "Row " << i;
    }
}

TEST(DEC_ExteriorDerivative0, GradientOfConstantIsZero)
{
    // d0 applied to a constant function should give all zeros.
    auto mesh = MakeTetrahedron();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);

    std::vector<double> constFunc(d0.Cols, 3.14);
    std::vector<double> result(d0.Rows, 999.0);

    d0.Multiply(constFunc, result);

    for (std::size_t i = 0; i < d0.Rows; ++i)
    {
        EXPECT_NEAR(result[i], 0.0, 1e-12) << "Edge " << i;
    }
}

TEST(DEC_ExteriorDerivative0, GradientOfLinearFunction)
{
    // For a linear function f(v) = x-coordinate of vertex,
    // d0 * f should give the x-difference along each edge.
    auto mesh = MakeTwoTriangleSquare();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);

    std::vector<double> f(d0.Cols);
    for (std::size_t i = 0; i < d0.Cols; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        f[i] = static_cast<double>(mesh.Position(vh).x);
    }

    std::vector<double> df(d0.Rows);
    d0.Multiply(f, df);

    // Each edge's d0*f should equal x(to) - x(from)
    for (std::size_t e = 0; e < d0.Rows; ++e)
    {
        Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(2u * e)};
        double xTo = static_cast<double>(mesh.Position(mesh.ToVertex(h)).x);
        double xFrom = static_cast<double>(mesh.Position(mesh.FromVertex(h)).x);
        EXPECT_NEAR(df[e], xTo - xFrom, 1e-12) << "Edge " << e;
    }
}

// =============================================================================
// d1 — Exterior Derivative 1 (edge → face)
// =============================================================================

TEST(DEC_ExteriorDerivative1, DimensionsMatchMesh)
{
    auto mesh = MakeTwoTriangleSquare();
    auto d1 = Geometry::DEC::BuildExteriorDerivative1(mesh);

    EXPECT_EQ(d1.Rows, mesh.FacesSize());
    EXPECT_EQ(d1.Cols, mesh.EdgesSize());
}

TEST(DEC_ExteriorDerivative1, TriangleFacesHaveThreeEntries)
{
    auto mesh = MakeTetrahedron();
    auto d1 = Geometry::DEC::BuildExteriorDerivative1(mesh);

    for (std::size_t i = 0; i < d1.Rows; ++i)
    {
        std::size_t nnzInRow = d1.RowOffsets[i + 1] - d1.RowOffsets[i];
        EXPECT_EQ(nnzInRow, 3u) << "Face " << i << " should have exactly 3 edge entries";
    }
}

TEST(DEC_ExteriorDerivative1, EntriesArePlusMinusOne)
{
    auto mesh = MakeTetrahedron();
    auto d1 = Geometry::DEC::BuildExteriorDerivative1(mesh);

    for (std::size_t k = 0; k < d1.Values.size(); ++k)
    {
        EXPECT_TRUE(d1.Values[k] == 1.0 || d1.Values[k] == -1.0)
            << "Entry " << k << " = " << d1.Values[k];
    }
}

TEST(DEC_ExteriorDerivative1, ExactnessD1D0IsZero)
{
    // d1 * d0 = 0 (the "exactness" property of the de Rham complex).
    // For any 0-form f, d1(d0(f)) = 0.
    auto mesh = MakeTetrahedron();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);
    auto d1 = Geometry::DEC::BuildExteriorDerivative1(mesh);

    // Test with a few different 0-forms
    std::vector<double> f(d0.Cols);

    // Linear function
    for (std::size_t i = 0; i < d0.Cols; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        f[i] = static_cast<double>(mesh.Position(vh).x + 2.0f * mesh.Position(vh).y);
    }

    std::vector<double> df(d0.Rows);
    d0.Multiply(f, df);

    std::vector<double> ddf(d1.Rows);
    d1.Multiply(df, ddf);

    for (std::size_t i = 0; i < d1.Rows; ++i)
    {
        EXPECT_NEAR(ddf[i], 0.0, 1e-10) << "d1(d0(f))[" << i << "] should be 0";
    }

    // Quadratic function — exactness is algebraic, holds for any function
    for (std::size_t i = 0; i < d0.Cols; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        auto p = mesh.Position(vh);
        f[i] = static_cast<double>(p.x * p.x + p.y * p.z);
    }

    d0.Multiply(f, df);
    d1.Multiply(df, ddf);

    for (std::size_t i = 0; i < d1.Rows; ++i)
    {
        EXPECT_NEAR(ddf[i], 0.0, 1e-10) << "d1(d0(f^2))[" << i << "] should be 0";
    }
}

// =============================================================================
// Hodge Star 0 — Vertex areas (mixed Voronoi)
// =============================================================================

TEST(DEC_HodgeStar0, DimensionsMatchVertexCount)
{
    auto mesh = MakeTwoTriangleSquare();
    auto h0 = Geometry::DEC::BuildHodgeStar0(mesh);

    EXPECT_EQ(h0.Size, mesh.VerticesSize());
}

TEST(DEC_HodgeStar0, AllEntriesPositive)
{
    auto mesh = MakeTetrahedron();
    auto h0 = Geometry::DEC::BuildHodgeStar0(mesh);

    for (std::size_t i = 0; i < h0.Size; ++i)
    {
        EXPECT_GT(h0.Diagonal[i], 0.0) << "Vertex " << i << " area should be positive";
    }
}

TEST(DEC_HodgeStar0, SumEqualsTotalArea)
{
    // Sum of vertex areas should equal total mesh surface area.
    auto mesh = MakeTetrahedron();
    auto h0 = Geometry::DEC::BuildHodgeStar0(mesh);

    double totalVertexArea = 0.0;
    for (std::size_t i = 0; i < h0.Size; ++i)
    {
        totalVertexArea += h0.Diagonal[i];
    }

    // Tetrahedron edge length = sqrt(8), face area = sqrt(3)/4 * 8 = 2*sqrt(3)
    // 4 faces => total area = 8*sqrt(3)
    double expectedArea = 8.0 * std::sqrt(3.0);
    EXPECT_NEAR(totalVertexArea, expectedArea, 1e-6);
}

TEST(DEC_HodgeStar0, SingleTriangleSumEqualsArea)
{
    auto mesh = MakeSingleTriangle();
    auto h0 = Geometry::DEC::BuildHodgeStar0(mesh);

    double sum = 0.0;
    for (std::size_t i = 0; i < h0.Size; ++i)
    {
        sum += h0.Diagonal[i];
    }

    // Equilateral triangle with side 1: area = sqrt(3)/4
    double expectedArea = std::sqrt(3.0) / 4.0;
    EXPECT_NEAR(sum, expectedArea, 1e-6);
}

TEST(DEC_HodgeStar0, EquilateralTriangleEqualAreas)
{
    // For a single equilateral triangle, all three vertex areas should be equal
    // (each is 1/3 of the total area).
    auto mesh = MakeSingleTriangle();
    auto h0 = Geometry::DEC::BuildHodgeStar0(mesh);

    EXPECT_NEAR(h0.Diagonal[0], h0.Diagonal[1], 1e-10);
    EXPECT_NEAR(h0.Diagonal[1], h0.Diagonal[2], 1e-10);
}

TEST(DEC_HodgeStar0, RegularTetrahedronEqualAreas)
{
    // Regular tetrahedron: all 4 vertices are symmetric => equal areas.
    auto mesh = MakeTetrahedron();
    auto h0 = Geometry::DEC::BuildHodgeStar0(mesh);

    for (std::size_t i = 1; i < 4; ++i)
    {
        EXPECT_NEAR(h0.Diagonal[i], h0.Diagonal[0], 1e-10) << "Vertex " << i;
    }
}

// =============================================================================
// Hodge Star 1 — Cotan weights
// =============================================================================

TEST(DEC_HodgeStar1, DimensionsMatchEdgeCount)
{
    auto mesh = MakeTetrahedron();
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh);

    EXPECT_EQ(h1.Size, mesh.EdgesSize());
}

TEST(DEC_HodgeStar1, InteriorEdgesPositive)
{
    // For a closed mesh (tetrahedron), cotan weights should all be positive
    // when all triangles are non-obtuse (equilateral triangles have cot(60°) > 0).
    auto mesh = MakeTetrahedron();
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh);

    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        EXPECT_GT(h1.Diagonal[i], 0.0) << "Edge " << i;
    }
}

TEST(DEC_HodgeStar1, RegularTetrahedronEqualWeights)
{
    // Regular tetrahedron: all edges are symmetric => equal cotan weights.
    auto mesh = MakeTetrahedron();
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh);

    for (std::size_t i = 1; i < h1.Size; ++i)
    {
        EXPECT_NEAR(h1.Diagonal[i], h1.Diagonal[0], 1e-10) << "Edge " << i;
    }
}

TEST(DEC_HodgeStar1, EquilateralTriangleCotanValue)
{
    // Single equilateral triangle: each edge has one adjacent face.
    // Angle opposite to each edge is 60°, cot(60°) = 1/sqrt(3).
    // Boundary edge: only one face contribution.
    // ⋆1[e] = cot(60°) / 2 = 1/(2*sqrt(3))
    auto mesh = MakeSingleTriangle();
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh);

    double expected = 1.0 / (2.0 * std::sqrt(3.0));
    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        EXPECT_NEAR(h1.Diagonal[i], expected, 1e-6) << "Edge " << i;
    }
}

// =============================================================================
// Hodge Star 2 — Inverse face areas
// =============================================================================

TEST(DEC_HodgeStar2, DimensionsMatchFaceCount)
{
    auto mesh = MakeTetrahedron();
    auto h2 = Geometry::DEC::BuildHodgeStar2(mesh);

    EXPECT_EQ(h2.Size, mesh.FacesSize());
}

TEST(DEC_HodgeStar2, AllEntriesPositive)
{
    auto mesh = MakeTetrahedron();
    auto h2 = Geometry::DEC::BuildHodgeStar2(mesh);

    for (std::size_t i = 0; i < h2.Size; ++i)
    {
        EXPECT_GT(h2.Diagonal[i], 0.0) << "Face " << i;
    }
}

TEST(DEC_HodgeStar2, InverseAreaCorrect)
{
    // Single equilateral triangle with side 1: area = sqrt(3)/4
    // ⋆2 = 1/area = 4/sqrt(3)
    auto mesh = MakeSingleTriangle();
    auto h2 = Geometry::DEC::BuildHodgeStar2(mesh);

    double expectedInvArea = 4.0 / std::sqrt(3.0);
    EXPECT_EQ(h2.Size, 1u);
    EXPECT_NEAR(h2.Diagonal[0], expectedInvArea, 1e-6);
}

TEST(DEC_HodgeStar2, TwoTriangleSquareAreas)
{
    // Two right triangles of area 0.5 each => ⋆2 = 1/0.5 = 2.0 each
    auto mesh = MakeTwoTriangleSquare();
    auto h2 = Geometry::DEC::BuildHodgeStar2(mesh);

    EXPECT_EQ(h2.Size, 2u);
    EXPECT_NEAR(h2.Diagonal[0], 2.0, 1e-10);
    EXPECT_NEAR(h2.Diagonal[1], 2.0, 1e-10);
}

// =============================================================================
// Laplacian — cotan Laplacian
// =============================================================================

TEST(DEC_Laplacian, DimensionsAreVxV)
{
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    EXPECT_EQ(L.Rows, mesh.VerticesSize());
    EXPECT_EQ(L.Cols, mesh.VerticesSize());
}

TEST(DEC_Laplacian, RowSumsAreZero)
{
    // The Laplacian annihilates constant functions: L * 1 = 0.
    // Equivalently, each row sums to zero.
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        double sum = 0.0;
        for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
        {
            sum += L.Values[k];
        }
        EXPECT_NEAR(sum, 0.0, 1e-10) << "Row " << i;
    }
}

TEST(DEC_Laplacian, ConstantFunctionInKernel)
{
    auto mesh = MakeSubdividedTriangle();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    std::vector<double> ones(L.Cols, 1.0);
    std::vector<double> result(L.Rows, 999.0);
    L.Multiply(ones, result);

    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        EXPECT_NEAR(result[i], 0.0, 1e-10) << "Vertex " << i;
    }
}

TEST(DEC_Laplacian, SymmetricMatrix)
{
    // The weak Laplacian L = d0^T * ⋆1 * d0 is symmetric.
    // Check L[i,j] = L[j,i] by extracting entries.
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    // Build dense matrix for comparison (small enough for tetrahedron)
    std::vector<std::vector<double>> dense(L.Rows, std::vector<double>(L.Cols, 0.0));
    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
        {
            dense[i][L.ColIndices[k]] = L.Values[k];
        }
    }

    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        for (std::size_t j = i + 1; j < L.Cols; ++j)
        {
            EXPECT_NEAR(dense[i][j], dense[j][i], 1e-12)
                << "L[" << i << "," << j << "] != L[" << j << "," << i << "]";
        }
    }
}

TEST(DEC_Laplacian, NegativeSemidefinite)
{
    // For any non-constant vector x, x^T L x <= 0.
    // Test with a specific vector.
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    // Use vertex x-coordinates as the test vector
    std::vector<double> x(L.Cols);
    for (std::size_t i = 0; i < L.Cols; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        x[i] = static_cast<double>(mesh.Position(vh).x);
    }

    std::vector<double> Lx(L.Rows);
    L.Multiply(x, Lx);

    double xTLx = 0.0;
    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        xTLx += x[i] * Lx[i];
    }

    // Should be non-positive (we use the convention where L is negative-semidefinite
    // with positive diagonal and negative off-diagonal, so x^T L x >= 0 actually.
    // The Laplacian L = d0^T ⋆1 d0 with our sign convention is positive-semidefinite.)
    EXPECT_GE(xTLx, -1e-10) << "x^T L x should be >= 0 (positive-semidefinite)";
}

TEST(DEC_Laplacian, OffDiagonalNonPositive)
{
    // Off-diagonal entries should be non-positive: L[i,j] = -w_ij <= 0 for i != j.
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
        {
            if (L.ColIndices[k] != i)
            {
                EXPECT_LE(L.Values[k], 1e-12)
                    << "Off-diagonal L[" << i << "," << L.ColIndices[k] << "] should be <= 0";
            }
        }
    }
}

TEST(DEC_Laplacian, DiagonalPositive)
{
    // Diagonal entries should be positive (sum of cotan weights).
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
        {
            if (L.ColIndices[k] == i)
            {
                EXPECT_GT(L.Values[k], 0.0)
                    << "Diagonal L[" << i << "," << i << "] should be positive";
            }
        }
    }
}

TEST(DEC_Laplacian, RegularTetrahedronSymmetricRows)
{
    // Regular tetrahedron: all vertices are equivalent.
    // Each row should have the same diagonal and same off-diagonal values.
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);

    // Extract diagonal values
    std::vector<double> diag(L.Rows, 0.0);
    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
        {
            if (L.ColIndices[k] == i)
            {
                diag[i] = L.Values[k];
            }
        }
    }

    for (std::size_t i = 1; i < L.Rows; ++i)
    {
        EXPECT_NEAR(diag[i], diag[0], 1e-10) << "Vertex " << i;
    }
}

// =============================================================================
// BuildOperators — full assembly
// =============================================================================

TEST(DEC_BuildOperators, AllOperatorsValid)
{
    auto mesh = MakeTetrahedron();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    EXPECT_TRUE(ops.IsValid());
    EXPECT_FALSE(ops.D0.IsEmpty());
    EXPECT_FALSE(ops.D1.IsEmpty());
    EXPECT_FALSE(ops.Hodge0.IsEmpty());
    EXPECT_FALSE(ops.Hodge1.IsEmpty());
    EXPECT_FALSE(ops.Hodge2.IsEmpty());
    EXPECT_FALSE(ops.Laplacian.IsEmpty());
}

TEST(DEC_BuildOperators, ConsistentDimensions)
{
    auto mesh = MakeSubdividedTriangle();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    std::size_t nV = mesh.VerticesSize();
    std::size_t nE = mesh.EdgesSize();
    std::size_t nF = mesh.FacesSize();

    // d0: E × V
    EXPECT_EQ(ops.D0.Rows, nE);
    EXPECT_EQ(ops.D0.Cols, nV);

    // d1: F × E
    EXPECT_EQ(ops.D1.Rows, nF);
    EXPECT_EQ(ops.D1.Cols, nE);

    // Hodge stars
    EXPECT_EQ(ops.Hodge0.Size, nV);
    EXPECT_EQ(ops.Hodge1.Size, nE);
    EXPECT_EQ(ops.Hodge2.Size, nF);

    // Laplacian: V × V
    EXPECT_EQ(ops.Laplacian.Rows, nV);
    EXPECT_EQ(ops.Laplacian.Cols, nV);
}

// =============================================================================
// SparseMatrix operations
// =============================================================================

TEST(DEC_SparseMatrix, MultiplyTransposeConsistent)
{
    // For d0, check that d0^T * y gives the same result as computing
    // the transpose product manually.
    auto mesh = MakeTwoTriangleSquare();
    auto d0 = Geometry::DEC::BuildExteriorDerivative0(mesh);

    // Random-ish 1-form (on edges)
    std::vector<double> omega(d0.Rows);
    for (std::size_t i = 0; i < d0.Rows; ++i)
    {
        omega[i] = static_cast<double>(i) * 0.7 - 1.5;
    }

    std::vector<double> result(d0.Cols, 0.0);
    d0.MultiplyTranspose(omega, result);

    // Verify by computing y^T (d0 * e_j) for each column j
    for (std::size_t j = 0; j < d0.Cols; ++j)
    {
        std::vector<double> ej(d0.Cols, 0.0);
        ej[j] = 1.0;
        std::vector<double> d0ej(d0.Rows, 0.0);
        d0.Multiply(ej, d0ej);

        double dot = 0.0;
        for (std::size_t i = 0; i < d0.Rows; ++i)
        {
            dot += omega[i] * d0ej[i];
        }

        EXPECT_NEAR(result[j], dot, 1e-10) << "Column " << j;
    }
}

// =============================================================================
// DiagonalMatrix operations
// =============================================================================

TEST(DEC_DiagonalMatrix, MultiplyCorrect)
{
    Geometry::DEC::DiagonalMatrix D;
    D.Size = 3;
    D.Diagonal = {2.0, 0.5, 3.0};

    std::vector<double> x = {1.0, 4.0, -1.0};
    std::vector<double> y(3);
    D.Multiply(x, y);

    EXPECT_NEAR(y[0], 2.0, 1e-12);
    EXPECT_NEAR(y[1], 2.0, 1e-12);
    EXPECT_NEAR(y[2], -3.0, 1e-12);
}

TEST(DEC_DiagonalMatrix, MultiplyInverseCorrect)
{
    Geometry::DEC::DiagonalMatrix D;
    D.Size = 3;
    D.Diagonal = {2.0, 0.5, 3.0};

    std::vector<double> x = {1.0, 4.0, -1.0};
    std::vector<double> y(3);
    D.MultiplyInverse(x, y);

    EXPECT_NEAR(y[0], 0.5, 1e-12);
    EXPECT_NEAR(y[1], 8.0, 1e-12);
    EXPECT_NEAR(y[2], -1.0 / 3.0, 1e-12);
}

TEST(DEC_DiagonalMatrix, MultiplyInverseHandlesZero)
{
    Geometry::DEC::DiagonalMatrix D;
    D.Size = 2;
    D.Diagonal = {0.0, 5.0};

    std::vector<double> x = {10.0, 10.0};
    std::vector<double> y(2);
    D.MultiplyInverse(x, y);

    EXPECT_NEAR(y[0], 0.0, 1e-12);   // Zero diagonal => output 0
    EXPECT_NEAR(y[1], 2.0, 1e-12);
}

// =============================================================================
// Integration: Hodge star duality and DEC complex consistency
// =============================================================================

TEST(DEC_Integration, HodgeStarDuality)
{
    // ⋆0 * ⋆2^{-1} relationship: for a closed mesh,
    // sum of ⋆0 diagonals = sum of 1/⋆2 diagonals (both equal total area).
    auto mesh = MakeTetrahedron();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    double sumH0 = 0.0;
    for (std::size_t i = 0; i < ops.Hodge0.Size; ++i)
    {
        sumH0 += ops.Hodge0.Diagonal[i];
    }

    double sumInvH2 = 0.0;
    for (std::size_t i = 0; i < ops.Hodge2.Size; ++i)
    {
        if (ops.Hodge2.Diagonal[i] > 1e-12)
        {
            sumInvH2 += 1.0 / ops.Hodge2.Diagonal[i];
        }
    }

    EXPECT_NEAR(sumH0, sumInvH2, 1e-6);
}

TEST(DEC_Integration, LaplacianMatchesD0TH1D0)
{
    // Verify that the directly-assembled Laplacian matches d0^T * diag(⋆1) * d0.
    auto mesh = MakeTwoTriangleSquare();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    std::size_t nV = ops.Laplacian.Cols;

    // For each standard basis vector e_j, compute:
    //   L_direct * e_j  vs  d0^T * ⋆1 * (d0 * e_j)
    for (std::size_t j = 0; j < nV; ++j)
    {
        std::vector<double> ej(nV, 0.0);
        ej[j] = 1.0;

        // Direct Laplacian
        std::vector<double> Lej(nV, 0.0);
        ops.Laplacian.Multiply(ej, Lej);

        // Manual: d0 * e_j
        std::vector<double> d0ej(ops.D0.Rows, 0.0);
        ops.D0.Multiply(ej, d0ej);

        // ⋆1 * (d0 * e_j)
        std::vector<double> h1d0ej(ops.Hodge1.Size, 0.0);
        ops.Hodge1.Multiply(d0ej, h1d0ej);

        // d0^T * ⋆1 * d0 * e_j
        std::vector<double> d0Th1d0ej(nV, 0.0);
        ops.D0.MultiplyTranspose(h1d0ej, d0Th1d0ej);

        for (std::size_t i = 0; i < nV; ++i)
        {
            EXPECT_NEAR(Lej[i], d0Th1d0ej[i], 1e-10)
                << "L[" << i << "," << j << "] mismatch with d0^T*H1*d0";
        }
    }
}

// =============================================================================
// Edge cases
// =============================================================================

TEST(DEC_EdgeCases, SingleTriangleBoundary)
{
    // Single triangle: all edges are boundary. ⋆1 weights should still be computed
    // (each edge has only one adjacent face).
    auto mesh = MakeSingleTriangle();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    EXPECT_TRUE(ops.IsValid());

    // 3 vertices, 3 edges, 1 face
    EXPECT_EQ(ops.D0.Rows, 3u);
    EXPECT_EQ(ops.D0.Cols, 3u);
    EXPECT_EQ(ops.D1.Rows, 1u);
    EXPECT_EQ(ops.D1.Cols, 3u);
}

TEST(DEC_EdgeCases, SubdividedTriangleMixedBoundary)
{
    // Subdivided triangle: mix of interior and boundary edges/vertices.
    auto mesh = MakeSubdividedTriangle();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    EXPECT_TRUE(ops.IsValid());

    // 6 vertices, 9 edges, 4 faces
    EXPECT_EQ(ops.D0.Rows, mesh.EdgesSize());
    EXPECT_EQ(ops.D0.Cols, 6u);
    EXPECT_EQ(ops.D1.Rows, 4u);

    // Laplacian should still have zero row sums
    std::vector<double> ones(ops.Laplacian.Cols, 1.0);
    std::vector<double> result(ops.Laplacian.Rows, 999.0);
    ops.Laplacian.Multiply(ones, result);

    for (std::size_t i = 0; i < ops.Laplacian.Rows; ++i)
    {
        EXPECT_NEAR(result[i], 0.0, 1e-10) << "Vertex " << i;
    }
}

// =============================================================================
// LaplacianCache — cached derived matrix forms for spectral workflows
// =============================================================================

TEST(DEC_LaplacianCache, BuildFromRegularTetrahedron)
{
    auto mesh = MakeTetrahedron();
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh);

    EXPECT_TRUE(cache.IsValid());
    EXPECT_FALSE(cache.MassInverse.IsEmpty());
    EXPECT_FALSE(cache.MassSqrtInverse.IsEmpty());
    EXPECT_FALSE(cache.SymmetricNormalizedLaplacian.IsEmpty());

    // Dimensions should all be #V × #V
    const std::size_t nV = mesh.VerticesSize();
    EXPECT_EQ(cache.MassInverse.Size, nV);
    EXPECT_EQ(cache.MassSqrtInverse.Size, nV);
    EXPECT_EQ(cache.SymmetricNormalizedLaplacian.Rows, nV);
    EXPECT_EQ(cache.SymmetricNormalizedLaplacian.Cols, nV);
}

TEST(DEC_LaplacianCache, MassInverseIsConsistentWithHodge0)
{
    auto mesh = MakeTetrahedron();
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh);

    const auto& h0 = cache.Operators.Hodge0;
    const auto& mInv = cache.MassInverse;

    for (std::size_t i = 0; i < h0.Size; ++i)
    {
        if (h0.Diagonal[i] > 1e-12)
        {
            EXPECT_NEAR(h0.Diagonal[i] * mInv.Diagonal[i], 1.0, 1e-10) << "Vertex " << i;
        }
    }
}

TEST(DEC_LaplacianCache, MassSqrtInverseIsConsistentWithHodge0)
{
    auto mesh = MakeTetrahedron();
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh);

    const auto& h0 = cache.Operators.Hodge0;
    const auto& mSqrtInv = cache.MassSqrtInverse;

    for (std::size_t i = 0; i < h0.Size; ++i)
    {
        if (h0.Diagonal[i] > 1e-12)
        {
            double product = h0.Diagonal[i] * mSqrtInv.Diagonal[i] * mSqrtInv.Diagonal[i];
            EXPECT_NEAR(product, 1.0, 1e-10) << "Vertex " << i;
        }
    }
}

TEST(DEC_LaplacianCache, SymNormalizedLaplacianIsSymmetric)
{
    auto mesh = MakeTetrahedron();
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh);

    const auto& Lsym = cache.SymmetricNormalizedLaplacian;

    // Verify L_sym[i,j] == L_sym[j,i]
    for (std::size_t i = 0; i < Lsym.Rows; ++i)
    {
        for (std::size_t k = Lsym.RowOffsets[i]; k < Lsym.RowOffsets[i + 1]; ++k)
        {
            std::size_t j = Lsym.ColIndices[k];
            double lij = Lsym.Values[k];

            // Find L_sym[j,i]
            double lji = 0.0;
            for (std::size_t m = Lsym.RowOffsets[j]; m < Lsym.RowOffsets[j + 1]; ++m)
            {
                if (Lsym.ColIndices[m] == i)
                {
                    lji = Lsym.Values[m];
                    break;
                }
            }

            EXPECT_NEAR(lij, lji, 1e-10)
                << "L_sym[" << i << "," << j << "] != L_sym[" << j << "," << i << "]";
        }
    }
}

TEST(DEC_LaplacianCache, SymNormalizedRowSumsAreZero)
{
    auto mesh = MakeSubdividedTriangle();
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh);

    const auto& Lsym = cache.SymmetricNormalizedLaplacian;
    const auto& dSqrt = cache.Operators.Hodge0;

    // L_sym * D^{1/2} * 1 should be zero (since L * 1 = 0 implies
    // D^{-1/2} L D^{-1/2} * D^{1/2} * 1 = D^{-1/2} L * 1 = 0)
    std::vector<double> dSqrt1(Lsym.Cols);
    for (std::size_t i = 0; i < Lsym.Cols; ++i)
        dSqrt1[i] = (dSqrt.Diagonal[i] > 1e-12) ? std::sqrt(dSqrt.Diagonal[i]) : 0.0;

    std::vector<double> result(Lsym.Rows, 999.0);
    Lsym.Multiply(dSqrt1, result);

    for (std::size_t i = 0; i < Lsym.Rows; ++i)
    {
        EXPECT_NEAR(result[i], 0.0, 1e-8) << "Vertex " << i;
    }
}

// =============================================================================
// AnalyzeLaplacian — structural diagnostics
// =============================================================================

TEST(DEC_Diagnostics, RegularTetrahedronPassesAll)
{
    auto mesh = MakeTetrahedron();
    auto L = Geometry::DEC::BuildLaplacian(mesh);
    auto diag = Geometry::DEC::AnalyzeLaplacian(L);

    EXPECT_TRUE(diag.AllPassed());
    EXPECT_TRUE(diag.IsSymmetric);
    EXPECT_TRUE(diag.HasZeroRowSums);
    EXPECT_TRUE(diag.HasNonPositiveOffDiag);
    EXPECT_TRUE(diag.HasPositiveDiagonal);
    EXPECT_TRUE(diag.IsDiagonallyDominant);
    EXPECT_LT(diag.MaxSymmetryError, 1e-10);
    EXPECT_LT(diag.MaxRowSumError, 1e-10);
    EXPECT_DOUBLE_EQ(diag.MaxOffDiagPositive, 0.0);
}

TEST(DEC_Diagnostics, SubdividedTrianglePassesAll)
{
    auto mesh = MakeSubdividedTriangle();
    auto L = Geometry::DEC::BuildLaplacian(mesh);
    auto diag = Geometry::DEC::AnalyzeLaplacian(L);

    EXPECT_TRUE(diag.AllPassed());
}

TEST(DEC_Diagnostics, SingleTrianglePassesAll)
{
    auto mesh = MakeSingleTriangle();
    auto L = Geometry::DEC::BuildLaplacian(mesh);
    auto diag = Geometry::DEC::AnalyzeLaplacian(L);

    EXPECT_TRUE(diag.AllPassed());
}

TEST(DEC_Diagnostics, EmptyMatrixDoesNotCrash)
{
    Geometry::DEC::SparseMatrix empty;
    auto diag = Geometry::DEC::AnalyzeLaplacian(empty);

    // Empty matrix should fail all checks (nothing to validate)
    EXPECT_FALSE(diag.AllPassed());
}

// =============================================================================
// Heat Kernel Weights — EdgeWeightMode::HeatKernel
// =============================================================================

TEST(DEC_HeatKernel, HodgeStar1AllPositive)
{
    // Heat kernel weights are always positive (exp(...) > 0).
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    EXPECT_EQ(h1.Size, mesh.EdgesSize());
    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        EXPECT_GT(h1.Diagonal[i], 0.0) << "Edge " << i;
        EXPECT_LE(h1.Diagonal[i], 1.0) << "Edge " << i << " (exp <= 1 for non-zero dist)";
    }
}

TEST(DEC_HeatKernel, RegularTetrahedronEqualWeights)
{
    // Regular tetrahedron: all edges have the same length => equal heat kernel weights.
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    for (std::size_t i = 1; i < h1.Size; ++i)
    {
        EXPECT_NEAR(h1.Diagonal[i], h1.Diagonal[0], 1e-12) << "Edge " << i;
    }
}

TEST(DEC_HeatKernel, AutoTimeParamMeanEdgeLengthSq)
{
    // With TimeParam=0 (default), time is set to mean squared edge length.
    // For a regular tetrahedron: all edges have the same length L,
    // so t = L^2, and weight = exp(-L^2 / (4*L^2)) = exp(-0.25).
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    double expected = std::exp(-0.25);
    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        EXPECT_NEAR(h1.Diagonal[i], expected, 1e-10) << "Edge " << i;
    }
}

TEST(DEC_HeatKernel, ExplicitTimeParam)
{
    // With explicit t, weight = exp(-d^2 / (4*t)).
    auto mesh = MakeSingleTriangle();  // equilateral, side=1
    double t = 2.0;
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel, t};
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    // Edge length ≈ 1 for equilateral triangle with side 1 (float positions)
    double expected = std::exp(-1.0 / (4.0 * t));  // exp(-0.125)
    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        EXPECT_NEAR(h1.Diagonal[i], expected, 1e-6) << "Edge " << i;
    }
}

TEST(DEC_HeatKernel, LargeTimeLimitUniform)
{
    // As t → ∞, heat kernel weights → 1 (uniform).
    auto mesh = MakeTwoTriangleSquare();
    double largeT = 1e10;
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel, largeT};
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        EXPECT_NEAR(h1.Diagonal[i], 1.0, 1e-6) << "Edge " << i;
    }
}

TEST(DEC_HeatKernel, SmallTimeDiscriminatesEdgeLengths)
{
    // For mixed edge lengths, smaller t makes shorter edges have higher weight.
    auto mesh = MakeTwoTriangleSquare();  // has edges of length 1 and sqrt(2)
    double smallT = 0.1;
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel, smallT};
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    // Find min/max weights
    double minW = 1.0, maxW = 0.0;
    for (std::size_t i = 0; i < h1.Size; ++i)
    {
        minW = std::min(minW, h1.Diagonal[i]);
        maxW = std::max(maxW, h1.Diagonal[i]);
    }
    // Short edges (length 1) should have higher weight than diagonal (length sqrt(2))
    EXPECT_GT(maxW, minW);
}

TEST(DEC_HeatKernel, LaplacianPassesDiagnostics)
{
    // Heat kernel Laplacian should pass ALL structural diagnostics:
    // symmetric, zero row sums, non-positive off-diag, positive diag, diag dominant.
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto L = Geometry::DEC::BuildLaplacian(mesh, config);
    auto diag = Geometry::DEC::AnalyzeLaplacian(L);

    EXPECT_TRUE(diag.AllPassed()) << "Heat kernel Laplacian should be a valid graph Laplacian";
    EXPECT_TRUE(diag.IsSymmetric);
    EXPECT_TRUE(diag.HasZeroRowSums);
    EXPECT_TRUE(diag.HasNonPositiveOffDiag);
    EXPECT_TRUE(diag.HasPositiveDiagonal);
    EXPECT_TRUE(diag.IsDiagonallyDominant);
}

TEST(DEC_HeatKernel, LaplacianSubdividedTrianglePassesDiagnostics)
{
    auto mesh = MakeSubdividedTriangle();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto L = Geometry::DEC::BuildLaplacian(mesh, config);
    auto diag = Geometry::DEC::AnalyzeLaplacian(L);

    EXPECT_TRUE(diag.AllPassed());
}

TEST(DEC_HeatKernel, LaplacianConstantInKernel)
{
    // L * 1 = 0 for heat kernel Laplacian.
    auto mesh = MakeSubdividedTriangle();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto L = Geometry::DEC::BuildLaplacian(mesh, config);

    std::vector<double> ones(L.Cols, 1.0);
    std::vector<double> result(L.Rows, 999.0);
    L.Multiply(ones, result);

    for (std::size_t i = 0; i < L.Rows; ++i)
    {
        EXPECT_NEAR(result[i], 0.0, 1e-10) << "Vertex " << i;
    }
}

TEST(DEC_HeatKernel, LaplacianMatchesHodge1Weights)
{
    // Verify that BuildLaplacian(config) is consistent with BuildHodgeStar1(config):
    // L[i,j] = -w_e for edge (i,j), L[i,i] = Σ_j w_{ij}
    auto mesh = MakeTwoTriangleSquare();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel, 0.5};
    auto L = Geometry::DEC::BuildLaplacian(mesh, config);
    auto h1 = Geometry::DEC::BuildHodgeStar1(mesh, config);

    // For each edge, the off-diagonal entries in L should equal -h1[edge]
    for (std::size_t e = 0; e < mesh.EdgesSize(); ++e)
    {
        Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(e)};
        if (mesh.IsDeleted(eh))
            continue;

        Geometry::HalfedgeHandle hh{static_cast<Geometry::PropertyIndex>(2u * e)};
        std::size_t vi = mesh.FromVertex(hh).Index;
        std::size_t vj = mesh.ToVertex(hh).Index;
        double w = h1.Diagonal[e];

        // Find L[vi, vj]
        bool found = false;
        for (std::size_t k = L.RowOffsets[vi]; k < L.RowOffsets[vi + 1]; ++k)
        {
            if (L.ColIndices[k] == vj)
            {
                EXPECT_NEAR(L.Values[k], -w, 1e-12) << "L[" << vi << "," << vj << "]";
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "L[" << vi << "," << vj << "] not found";
    }
}

TEST(DEC_HeatKernel, BuildOperatorsValid)
{
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto ops = Geometry::DEC::BuildOperators(mesh, config);

    EXPECT_TRUE(ops.IsValid());
    // D0, D1 should match standard (topology-only)
    auto stdOps = Geometry::DEC::BuildOperators(mesh);
    EXPECT_EQ(ops.D0.Rows, stdOps.D0.Rows);
    EXPECT_EQ(ops.D0.Cols, stdOps.D0.Cols);
    EXPECT_EQ(ops.D0.NonZeros(), stdOps.D0.NonZeros());
    // Hodge0, Hodge2 should match (area-based, weight-independent)
    EXPECT_EQ(ops.Hodge0.Size, stdOps.Hodge0.Size);
    for (std::size_t i = 0; i < ops.Hodge0.Size; ++i)
    {
        EXPECT_NEAR(ops.Hodge0.Diagonal[i], stdOps.Hodge0.Diagonal[i], 1e-14);
    }
}

TEST(DEC_HeatKernel, LaplacianCacheValid)
{
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh, config);

    EXPECT_TRUE(cache.IsValid());
    EXPECT_FALSE(cache.MassInverse.IsEmpty());
    EXPECT_FALSE(cache.MassSqrtInverse.IsEmpty());
    EXPECT_FALSE(cache.SymmetricNormalizedLaplacian.IsEmpty());
}

TEST(DEC_HeatKernel, SymNormalizedLaplacianSymmetric)
{
    auto mesh = MakeSubdividedTriangle();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh, config);

    const auto& Lsym = cache.SymmetricNormalizedLaplacian;

    for (std::size_t i = 0; i < Lsym.Rows; ++i)
    {
        for (std::size_t k = Lsym.RowOffsets[i]; k < Lsym.RowOffsets[i + 1]; ++k)
        {
            std::size_t j = Lsym.ColIndices[k];
            double lij = Lsym.Values[k];

            double lji = 0.0;
            for (std::size_t m = Lsym.RowOffsets[j]; m < Lsym.RowOffsets[j + 1]; ++m)
            {
                if (Lsym.ColIndices[m] == i)
                {
                    lji = Lsym.Values[m];
                    break;
                }
            }

            EXPECT_NEAR(lij, lji, 1e-10)
                << "L_sym[" << i << "," << j << "] != L_sym[" << j << "," << i << "]";
        }
    }
}

TEST(DEC_HeatKernel, ObtuseTriangleAlwaysPositive)
{
    // Key motivation: cotan weights can go negative at obtuse angles,
    // but heat kernel weights are always strictly positive.
    // MakeRightTriangle has a 90-degree angle. Make one more obtuse.
    Geometry::HalfedgeMesh::Mesh mesh;
    // Obtuse triangle: angle at v2 > 90 degrees
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({3.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.1f, 0.3f, 0.0f});  // very close to v0-v1 line → obtuse at v2
    (void)mesh.AddTriangle(v0, v1, v2);

    // Cotan weights: at least one should be negative for the obtuse triangle
    auto h1Cotan = Geometry::DEC::BuildHodgeStar1(mesh);
    bool hasNegativeCotan = false;
    for (std::size_t i = 0; i < h1Cotan.Size; ++i)
    {
        if (h1Cotan.Diagonal[i] < 0.0)
            hasNegativeCotan = true;
    }
    EXPECT_TRUE(hasNegativeCotan) << "Obtuse triangle should have at least one negative cotan weight";

    // Heat kernel weights: all must be positive
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto h1Heat = Geometry::DEC::BuildHodgeStar1(mesh, config);
    for (std::size_t i = 0; i < h1Heat.Size; ++i)
    {
        EXPECT_GT(h1Heat.Diagonal[i], 0.0) << "Heat kernel weight must be positive, edge " << i;
    }

    // Heat kernel Laplacian should still pass all diagnostics
    auto L = Geometry::DEC::BuildLaplacian(mesh, config);
    auto diag = Geometry::DEC::AnalyzeLaplacian(L);
    EXPECT_TRUE(diag.AllPassed()) << "Heat kernel Laplacian on obtuse mesh should be valid";
}

TEST(DEC_HeatKernel, CotanModePassthrough)
{
    // EdgeWeightConfig with Cotan mode should produce identical results to default.
    auto mesh = MakeTetrahedron();
    Geometry::DEC::EdgeWeightConfig cotanConfig{Geometry::DEC::EdgeWeightMode::Cotan};

    auto h1Default = Geometry::DEC::BuildHodgeStar1(mesh);
    auto h1Config = Geometry::DEC::BuildHodgeStar1(mesh, cotanConfig);

    EXPECT_EQ(h1Default.Size, h1Config.Size);
    for (std::size_t i = 0; i < h1Default.Size; ++i)
    {
        EXPECT_DOUBLE_EQ(h1Default.Diagonal[i], h1Config.Diagonal[i]) << "Edge " << i;
    }

    auto LDefault = Geometry::DEC::BuildLaplacian(mesh);
    auto LConfig = Geometry::DEC::BuildLaplacian(mesh, cotanConfig);

    EXPECT_EQ(LDefault.Rows, LConfig.Rows);
    EXPECT_EQ(LDefault.NonZeros(), LConfig.NonZeros());
    for (std::size_t i = 0; i < LDefault.Values.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(LDefault.Values[i], LConfig.Values[i]) << "Value index " << i;
    }
}

TEST(DEC_HeatKernel, SolveCGShiftedWithHeatKernel)
{
    // Solve (M + t*L_hk)*x = b where L_hk is the heat kernel Laplacian.
    // This validates that the heat kernel Laplacian works with the existing
    // CG solver infrastructure (e.g., for heat method distance computation).
    auto mesh = MakeSubdividedTriangle();
    Geometry::DEC::EdgeWeightConfig config{Geometry::DEC::EdgeWeightMode::HeatKernel};
    auto cache = Geometry::DEC::BuildLaplacianCache(mesh, config);

    const auto& M = cache.Operators.Hodge0;
    const auto& L = cache.Operators.Laplacian;
    const std::size_t nV = M.Size;

    // RHS: mass-weighted delta at vertex 0
    std::vector<double> b(nV, 0.0);
    b[0] = M.Diagonal[0];

    std::vector<double> x(nV, 0.0);
    Geometry::DEC::CGParams cgParams{1000, 1e-8};
    auto result = Geometry::DEC::SolveCGShifted(M, 1.0, L, 1.0, b, x, cgParams);

    EXPECT_TRUE(result.Converged) << "CG should converge for (M + L_hk)";
    EXPECT_LT(result.ResidualNorm, 1e-6);
    // Solution should be positive at vertex 0 (heat source)
    EXPECT_GT(x[0], 0.0);
}

// =============================================================================
// GEOM-041 — FEM stiffness modes and clamped per-halfedge cotan
// =============================================================================

namespace
{
    using Geometry::DEC::EdgeWeightMode;
    using Geometry::DEC::EdgeWeightConfig;

    // Max |L[i,j] - L[j,i]| and max |row sum| computed directly from CSR.
    void StiffnessStructure(const Geometry::DEC::SparseMatrix& L,
                            double& maxRowSum, double& maxAsym)
    {
        maxRowSum = 0.0;
        maxAsym = 0.0;
        for (std::size_t i = 0; i < L.Rows; ++i)
        {
            double rs = 0.0;
            for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
            {
                rs += L.Values[k];
                const std::size_t j = L.ColIndices[k];
                // Find L[j,i].
                double lji = 0.0;
                for (std::size_t m = L.RowOffsets[j]; m < L.RowOffsets[j + 1]; ++m)
                {
                    if (L.ColIndices[m] == i) { lji = L.Values[m]; break; }
                }
                maxAsym = std::max(maxAsym, std::abs(L.Values[k] - lji));
            }
            maxRowSum = std::max(maxRowSum, std::abs(rs));
        }
    }
}

TEST(DEC_StiffnessModes, RowSumsZeroAndSymmetricForEveryMode)
{
    auto mesh = MakeIcosahedron();
    const EdgeWeightMode modes[] = {
        EdgeWeightMode::Graph, EdgeWeightMode::Cotan,
        EdgeWeightMode::Fujiwara, EdgeWeightMode::ModifiedNormal};

    for (EdgeWeightMode mode : modes)
    {
        EdgeWeightConfig config{mode};
        auto L = Geometry::DEC::BuildLaplacian(mesh, config);
        double maxRowSum = 0.0, maxAsym = 0.0;
        StiffnessStructure(L, maxRowSum, maxAsym);
        EXPECT_LT(maxRowSum, 1e-9) << "mode " << static_cast<int>(mode);
        EXPECT_LT(maxAsym, 1e-9) << "mode " << static_cast<int>(mode);

        auto diag = Geometry::DEC::AnalyzeLaplacian(L, 1e-9);
        EXPECT_TRUE(diag.IsSymmetric) << "mode " << static_cast<int>(mode);
        EXPECT_TRUE(diag.HasZeroRowSums) << "mode " << static_cast<int>(mode);
    }
}

TEST(DEC_StiffnessModes, GraphWeightsAreUnitPerEdge)
{
    auto mesh = MakeTetrahedron();
    auto w = Geometry::DEC::BuildHodgeStar1(mesh, EdgeWeightConfig{EdgeWeightMode::Graph});
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;
        EXPECT_DOUBLE_EQ(w.Diagonal[ei], 1.0);
    }
}

TEST(DEC_StiffnessModes, FujiwaraWeightsMatchInverseLength)
{
    auto mesh = MakeTetrahedron();
    auto w = Geometry::DEC::BuildHodgeStar1(mesh, EdgeWeightConfig{EdgeWeightMode::Fujiwara});
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;
        Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(2u * ei)};
        const glm::dvec3 a = glm::dvec3(mesh.Position(mesh.FromVertex(h0)));
        const glm::dvec3 b = glm::dvec3(mesh.Position(mesh.ToVertex(h0)));
        const double len = glm::length(b - a);
        EXPECT_NEAR(w.Diagonal[ei], 1.0 / len, 1e-12);
    }
}

TEST(DEC_StiffnessModes, ModifiedNormalEqualsCotanTimesNormalDot)
{
    // On a closed convex mesh, ModifiedNormal weight = cotan weight * |n_i·n_j|.
    auto mesh = MakeIcosahedron();
    auto cot = Geometry::DEC::BuildHodgeStar1(mesh); // Cotan Hodge star
    auto mod = Geometry::DEC::BuildHodgeStar1(mesh, EdgeWeightConfig{EdgeWeightMode::ModifiedNormal});

    namespace MU = Geometry::MeshUtils;
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;
        Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(2u * ei)};
        const glm::vec3 ni = glm::normalize(MU::VertexNormal(mesh, mesh.FromVertex(h0)));
        const glm::vec3 nj = glm::normalize(MU::VertexNormal(mesh, mesh.ToVertex(h0)));
        const double expected = cot.Diagonal[ei] * std::abs(glm::dot(glm::dvec3(ni), glm::dvec3(nj)));
        EXPECT_NEAR(mod.Diagonal[ei], expected, 1e-6) << "edge " << ei;
    }
}

TEST(DEC_ClampedHalfedgeCotan, MatchesEdgeCotanWeight)
{
    namespace MU = Geometry::MeshUtils;
    auto mesh = MakeIcosahedron();
    auto cot = MU::ClampedHalfedgeCotan(mesh);
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;
        Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(2u * ei)};
        Geometry::HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);
        const double recovered = 0.5 * (cot[h0] + cot[h1]);
        // Heron/metric form vs the geometric Cotan() agree up to float-position
        // rounding (positions are stored as float).
        EXPECT_NEAR(recovered, MU::EdgeCotanWeight(mesh, e), 1e-6) << "edge " << ei;
    }
}

TEST(DEC_ClampedHalfedgeCotan, ClampEngagesOnSliver)
{
    namespace MU = Geometry::MeshUtils;
    // Needle triangle: a near-zero apex angle gives a huge cotangent.
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, 1.0e-5f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    const double bound = 100.0;
    auto cot = MU::ClampedHalfedgeCotan(mesh, bound);
    double maxAbs = 0.0;
    for (std::size_t hi = 0; hi < mesh.HalfedgesSize(); ++hi)
    {
        Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(hi)};
        maxAbs = std::max(maxAbs, std::abs(cot[h]));
        EXPECT_LE(std::abs(cot[h]), bound + 1e-9);
        EXPECT_TRUE(std::isfinite(cot[h]));
    }
    EXPECT_NEAR(maxAbs, bound, 1e-9) << "sliver apex cotan must hit the clamp";
}

TEST(DEC_StiffnessModes, FailClosedOnEmptyMesh)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    for (EdgeWeightMode mode : {EdgeWeightMode::Graph, EdgeWeightMode::Fujiwara,
                                EdgeWeightMode::ModifiedNormal})
    {
        auto L = Geometry::DEC::BuildLaplacian(mesh, EdgeWeightConfig{mode});
        EXPECT_EQ(L.Rows, 0u);
        for (double v : L.Values) EXPECT_TRUE(std::isfinite(v));
    }
}

TEST(DEC_StiffnessModes, FailClosedOnNonFinitePositions)
{
    // A NaN vertex must not inject NaN into any mode's stiffness operator.
    Geometry::HalfedgeMesh::Mesh mesh;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({nan, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, -1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v3, v1);

    for (EdgeWeightMode mode : {EdgeWeightMode::Graph, EdgeWeightMode::Fujiwara,
                                EdgeWeightMode::ModifiedNormal})
    {
        auto w = Geometry::DEC::BuildHodgeStar1(mesh, EdgeWeightConfig{mode});
        for (double v : w.Diagonal) EXPECT_TRUE(std::isfinite(v)) << "mode " << static_cast<int>(mode);
    }
}

// =============================================================================
// GEOM-041 Slice 3 — mass modes (Sum / Barycentric / Voronoi / Galerkin)
// =============================================================================

namespace
{
    using Geometry::DEC::MassMode;

    double TotalFaceArea(const Geometry::HalfedgeMesh::Mesh& mesh)
    {
        double total = 0.0;
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
            if (mesh.IsDeleted(f)) continue;
            total += Geometry::MeshUtils::FaceArea(mesh, f);
        }
        return total;
    }

    // Quadratic form xᵀ M x for a CSR matrix.
    double QuadForm(const Geometry::DEC::SparseMatrix& M, const std::vector<double>& x)
    {
        double s = 0.0;
        for (std::size_t i = 0; i < M.Rows; ++i)
            for (std::size_t k = M.RowOffsets[i]; k < M.RowOffsets[i + 1]; ++k)
                s += x[i] * M.Values[k] * x[M.ColIndices[k]];
        return s;
    }
}

TEST(DEC_MassModes, VoronoiVariantReproducesDefault)
{
    auto mesh = MakeIcosahedron();
    auto def = Geometry::DEC::BuildHodgeStar0(mesh);
    auto vor = Geometry::DEC::BuildHodgeStar0(mesh, MassMode::Voronoi);
    ASSERT_EQ(def.Size, vor.Size);
    for (std::size_t i = 0; i < def.Size; ++i)
        EXPECT_DOUBLE_EQ(def.Diagonal[i], vor.Diagonal[i]);

    // Default BuildOperators path is also unchanged for {Cotan, Voronoi}.
    auto opsDefault = Geometry::DEC::BuildOperators(mesh);
    auto opsConfig = Geometry::DEC::BuildOperators(
        mesh, EdgeWeightConfig{EdgeWeightMode::Cotan, 0.0, 1.0, MassMode::Voronoi});
    for (std::size_t i = 0; i < opsDefault.Hodge0.Size; ++i)
        EXPECT_DOUBLE_EQ(opsDefault.Hodge0.Diagonal[i], opsConfig.Hodge0.Diagonal[i]);
    EXPECT_TRUE(opsConfig.ConsistentMass.IsEmpty());
}

TEST(DEC_MassModes, AllLumpedModesPartitionTotalArea)
{
    auto mesh = MakeIcosahedron();
    const double total = TotalFaceArea(mesh);
    for (MassMode mode : {MassMode::Sum, MassMode::Barycentric, MassMode::Voronoi})
    {
        auto m = Geometry::DEC::BuildHodgeStar0(mesh, mode);
        double sum = 0.0;
        for (double v : m.Diagonal) sum += v;
        EXPECT_NEAR(sum, total, 1e-5) << "mode " << static_cast<int>(mode);
    }
}

TEST(DEC_MassModes, BarycentricIsOneThirdIncidentArea)
{
    auto mesh = MakeIcosahedron();
    auto bary = Geometry::DEC::BuildHodgeStar0(mesh, MassMode::Barycentric);
    std::vector<double> expected(mesh.VerticesSize(), 0.0);
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(f)) continue;
        const double third = Geometry::MeshUtils::FaceArea(mesh, f) / 3.0;
        for (auto v : mesh.VerticesAroundFace(f)) expected[v.Index] += third;
    }
    for (std::size_t i = 0; i < bary.Size; ++i)
        EXPECT_NEAR(bary.Diagonal[i], expected[i], 1e-6);
}

TEST(DEC_MassModes, GalerkinRowSumsEqualLumpedSum)
{
    auto mesh = MakeIcosahedron();
    auto M = Geometry::DEC::BuildConsistentMass(mesh);
    auto sum = Geometry::DEC::BuildHodgeStar0(mesh, MassMode::Sum);
    ASSERT_EQ(M.Rows, sum.Size);
    for (std::size_t i = 0; i < M.Rows; ++i)
    {
        double rs = 0.0;
        for (std::size_t k = M.RowOffsets[i]; k < M.RowOffsets[i + 1]; ++k)
            rs += M.Values[k];
        EXPECT_NEAR(rs, sum.Diagonal[i], 1e-12) << "row " << i;
    }
    // Total consistent mass equals the surface area.
    double total = 0.0;
    for (double v : M.Values) total += v;
    EXPECT_NEAR(total, TotalFaceArea(mesh), 1e-5);
}

TEST(DEC_MassModes, GalerkinConsistentMassIsSymmetricSPD)
{
    auto mesh = MakeIcosahedron();
    auto M = Geometry::DEC::BuildConsistentMass(mesh);

    // Symmetry + positive diagonal.
    for (std::size_t i = 0; i < M.Rows; ++i)
    {
        for (std::size_t k = M.RowOffsets[i]; k < M.RowOffsets[i + 1]; ++k)
        {
            const std::size_t j = M.ColIndices[k];
            double mji = 0.0;
            for (std::size_t m = M.RowOffsets[j]; m < M.RowOffsets[j + 1]; ++m)
                if (M.ColIndices[m] == i) { mji = M.Values[m]; break; }
            EXPECT_NEAR(M.Values[k], mji, 1e-12);
            if (i == j) EXPECT_GT(M.Values[k], 0.0);
        }
    }

    // Positive-definite evidence: xᵀ M x > 0 for several nonzero vectors.
    const std::size_t n = M.Rows;
    std::vector<double> e0(n, 0.0); e0[0] = 1.0;
    std::vector<double> ones(n, 1.0);
    std::vector<double> alt(n), ramp(n);
    for (std::size_t i = 0; i < n; ++i) { alt[i] = (i % 2 == 0) ? 1.0 : -1.0; ramp[i] = static_cast<double>(i) - 0.5 * n; }
    for (const auto* x : {&e0, &ones, &alt, &ramp})
        EXPECT_GT(QuadForm(M, *x), 0.0);
}

TEST(DEC_MassModes, GalerkinOperatorsPopulateConsistentAndLumped)
{
    auto mesh = MakeIcosahedron();
    auto ops = Geometry::DEC::BuildOperators(
        mesh, EdgeWeightConfig{EdgeWeightMode::Cotan, 0.0, 1.0, MassMode::Galerkin});
    EXPECT_FALSE(ops.ConsistentMass.IsEmpty());
    EXPECT_EQ(ops.ConsistentMass.Rows, mesh.VerticesSize());
    // The diagonal Hodge0 is the row-sum lump of the consistent mass.
    for (std::size_t i = 0; i < ops.ConsistentMass.Rows; ++i)
    {
        double rs = 0.0;
        for (std::size_t k = ops.ConsistentMass.RowOffsets[i]; k < ops.ConsistentMass.RowOffsets[i + 1]; ++k)
            rs += ops.ConsistentMass.Values[k];
        EXPECT_NEAR(ops.Hodge0.Diagonal[i], rs, 1e-12);
    }
}

TEST(DEC_MassModes, FailClosedOnEmptyAndNonFinite)
{
    Geometry::HalfedgeMesh::Mesh empty;
    auto Me = Geometry::DEC::BuildConsistentMass(empty);
    EXPECT_EQ(Me.Rows, 0u);

    Geometry::HalfedgeMesh::Mesh mesh;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({nan, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, -1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2); // poisoned face -> skipped
    (void)mesh.AddTriangle(v0, v3, v1); // finite face -> contributes

    auto M = Geometry::DEC::BuildConsistentMass(mesh);
    for (double v : M.Values) EXPECT_TRUE(std::isfinite(v));
    // The new mass paths fail closed on non-finite faces (the poisoned triangle
    // is skipped). Voronoi is the pre-existing shared mixed-Voronoi path and is
    // out of GEOM-041 scope here.
    for (MassMode mode : {MassMode::Sum, MassMode::Barycentric, MassMode::Galerkin})
    {
        auto m = Geometry::DEC::BuildHodgeStar0(mesh, mode);
        for (double v : m.Diagonal) EXPECT_TRUE(std::isfinite(v)) << "mode " << static_cast<int>(mode);
    }
}
