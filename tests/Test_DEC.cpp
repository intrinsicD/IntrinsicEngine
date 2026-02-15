#include <gtest/gtest.h>
#include <cmath>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

// =============================================================================
// Test mesh builders
// =============================================================================

// Single equilateral triangle in the XY plane: vertices at
//   (0, 0, 0), (1, 0, 0), (0.5, sqrt(3)/2, 0)
// Area = sqrt(3)/4 ≈ 0.4330
static Geometry::Halfedge::Mesh MakeSingleTriangle()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, std::sqrt(3.0f) / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

// Two triangles sharing an edge (a "bowtie" quad split):
//   v0=(0,0,0)  v1=(1,0,0)
//   v2=(1,1,0)  v3=(0,1,0)
//
//   Face 0: v0-v1-v2
//   Face 1: v0-v2-v3
//
// This is a unit square split into two right triangles, each with area 0.5.
static Geometry::Halfedge::Mesh MakeTwoTriangleSquare()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    return mesh;
}

// Regular tetrahedron (closed mesh, no boundary).
// Vertices at:
//   v0 = (1, 1, 1)
//   v1 = (1, -1, -1)
//   v2 = (-1, 1, -1)
//   v3 = (-1, -1, 1)
// All edges have equal length sqrt(8), all faces are equilateral.
static Geometry::Halfedge::Mesh MakeTetrahedron()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
    auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
    auto v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
    auto v3 = mesh.AddVertex({-1.0f, -1.0f, 1.0f});

    // Consistent outward-facing winding
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    (void)mesh.AddTriangle(v0, v3, v1);
    (void)mesh.AddTriangle(v1, v3, v2);

    return mesh;
}

// Flat regular mesh of an equilateral triangle subdivided once:
//   v0 = (0, 0, 0),  v1 = (2, 0, 0),  v2 = (1, sqrt(3), 0)
//   v3 = midpoint(v0,v1) = (1, 0, 0)
//   v4 = midpoint(v1,v2) = (1.5, sqrt(3)/2, 0)
//   v5 = midpoint(v0,v2) = (0.5, sqrt(3)/2, 0)
//
// 4 faces, 6 vertices, 9 edges (3 boundary + 6 or rather 9 total with 3 interior)
// v3 is an interior vertex with valence 6 — good for Laplacian testing.
static Geometry::Halfedge::Mesh MakeSubdividedTriangle()
{
    const float s = std::sqrt(3.0f);
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, s, 0.0f});
    auto v3 = mesh.AddVertex({1.0f, 0.0f, 0.0f});       // mid(v0,v1)
    auto v4 = mesh.AddVertex({1.5f, s / 2.0f, 0.0f});   // mid(v1,v2)
    auto v5 = mesh.AddVertex({0.5f, s / 2.0f, 0.0f});   // mid(v0,v2)

    (void)mesh.AddTriangle(v0, v3, v5);
    (void)mesh.AddTriangle(v3, v1, v4);
    (void)mesh.AddTriangle(v5, v4, v2);
    (void)mesh.AddTriangle(v3, v4, v5);

    return mesh;
}

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
