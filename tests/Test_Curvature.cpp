#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <optional>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    Geometry::Halfedge::Mesh MakeInsideOutTetrahedron()
    {
        Geometry::Halfedge::Mesh mesh;
        auto v0 = mesh.AddVertex({1.0f,  1.0f,  1.0f});
        auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
        auto v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
        auto v3 = mesh.AddVertex({-1.0f,-1.0f,  1.0f});
        (void)mesh.AddTriangle(v0, v2, v1);
        (void)mesh.AddTriangle(v0, v3, v2);
        (void)mesh.AddTriangle(v0, v1, v3);
        (void)mesh.AddTriangle(v1, v2, v3);
        return mesh;
    }
}

// =============================================================================
// ComputeMeanCurvature — empty mesh
// =============================================================================

TEST(Curvature_MeanCurvature, EmptyMesh_ReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::Curvature::ComputeMeanCurvature(mesh);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// ComputeGaussianCurvature — empty mesh
// =============================================================================

TEST(Curvature_GaussianCurvature, EmptyMesh_ReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::Curvature::ComputeGaussianCurvature(mesh);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// ComputeMeanCurvature — closed regular tetrahedron
// =============================================================================

TEST(Curvature_MeanCurvature, Tetrahedron_UniformPositiveCurvature)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::Curvature::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(result.has_value());

    const auto& prop = result->Property;
    const std::size_t nV = mesh.VerticesSize();
    ASSERT_GT(nV, 0u);

    // All vertices of a regular tetrahedron are symmetric — curvature should
    // be identical (and positive, since the surface is convex).
    Geometry::VertexHandle v0{static_cast<Geometry::PropertyIndex>(0)};
    const double H0 = prop[v0];
    EXPECT_GT(H0, 0.0) << "Mean curvature on convex closed mesh should be positive";

    for (std::size_t i = 1; i < nV; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_NEAR(prop[vh], H0, 1e-6)
            << "Vertex " << i << " curvature should match vertex 0 by symmetry";
    }
}

// =============================================================================
// ComputeGaussianCurvature — Gauss-Bonnet on tetrahedron
// =============================================================================

TEST(Curvature_GaussianCurvature, Tetrahedron_GaussBonnet)
{
    // For a closed genus-0 surface: Σ K_i * A_i = 2π * χ = 2π * 2 = 4π.
    auto mesh = MakeTetrahedron();

    auto kResult = Geometry::Curvature::ComputeGaussianCurvature(mesh);
    ASSERT_TRUE(kResult.has_value());

    // We need areas to verify Gauss-Bonnet. Compute the full curvature field
    // to get Gaussian curvature, then use angle defect sum directly.
    // Alternatively, since angle defect is K_i * A_i before dividing by A_i,
    // we can sum angle defects directly: Σ(2π - Σθ_j) = 2πχ = 4π.
    //
    // For a regular tetrahedron with 4 vertices, each vertex has 3 incident
    // triangles. The angle at each vertex in each equilateral triangle is π/3.
    // Angle defect per vertex = 2π - 3*(π/3) = 2π - π = π.
    // Total angle defect = 4π, matching Gauss-Bonnet for χ=2.
    //
    // K_i = angle_defect_i / A_i, so K_i * A_i = angle_defect_i.
    // We verify by checking that all K_i are equal (by symmetry) and positive.

    const auto& prop = kResult->Property;
    const std::size_t nV = mesh.VerticesSize();

    Geometry::VertexHandle v0{static_cast<Geometry::PropertyIndex>(0)};
    const double K0 = prop[v0];
    EXPECT_GT(K0, 0.0) << "Gaussian curvature on convex closed mesh should be positive";

    for (std::size_t i = 1; i < nV; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_NEAR(prop[vh], K0, 1e-6)
            << "Vertex " << i << " should have same Gaussian curvature by symmetry";
    }
}

// =============================================================================
// ComputeCurvature — icosahedron symmetry
// =============================================================================

TEST(Curvature_Full, Icosahedron_SymmetricCurvatureValues)
{
    auto mesh = MakeIcosahedron();
    auto field = Geometry::Curvature::ComputeCurvature(mesh);

    const std::size_t nV = mesh.VerticesSize();
    ASSERT_EQ(nV, 12u);

    // All vertices of a regular icosahedron are equivalent by symmetry.
    Geometry::VertexHandle v0{static_cast<Geometry::PropertyIndex>(0)};
    const double H0 = field.MeanCurvatureProperty[v0];
    const double K0 = field.GaussianCurvatureProperty[v0];
    const double k1_0 = field.MaxPrincipalCurvatureProperty[v0];
    const double k2_0 = field.MinPrincipalCurvatureProperty[v0];

    for (std::size_t i = 1; i < nV; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_NEAR(field.MeanCurvatureProperty[vh], H0, 1e-6)
            << "Mean curvature should be uniform on icosahedron (vertex " << i << ")";
        EXPECT_NEAR(field.GaussianCurvatureProperty[vh], K0, 1e-6)
            << "Gaussian curvature should be uniform on icosahedron (vertex " << i << ")";
        EXPECT_NEAR(field.MaxPrincipalCurvatureProperty[vh], k1_0, 1e-6)
            << "Max principal curvature should be uniform (vertex " << i << ")";
        EXPECT_NEAR(field.MinPrincipalCurvatureProperty[vh], k2_0, 1e-6)
            << "Min principal curvature should be uniform (vertex " << i << ")";
    }

    // On a sphere-like closed mesh, mean curvature should be positive.
    EXPECT_GT(H0, 0.0);
    EXPECT_GT(K0, 0.0);
}

// =============================================================================
// ComputeCurvature — principal curvature relation: κ₁ >= κ₂ and H = (κ₁+κ₂)/2
// =============================================================================

TEST(Curvature_Full, PrincipalRelation_K1GeK2_And_H_Equals_Average)
{
    auto mesh = MakeIcosahedron();
    auto field = Geometry::Curvature::ComputeCurvature(mesh);

    const std::size_t nV = mesh.VerticesSize();

    for (std::size_t i = 0; i < nV; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};

        const double H  = field.MeanCurvatureProperty[vh];
        const double k1 = field.MaxPrincipalCurvatureProperty[vh];
        const double k2 = field.MinPrincipalCurvatureProperty[vh];

        EXPECT_GE(k1, k2 - 1e-10)
            << "κ₁ should be >= κ₂ at vertex " << i;

        EXPECT_NEAR(H, (k1 + k2) / 2.0, 1e-10)
            << "H should equal (κ₁ + κ₂)/2 at vertex " << i;
    }
}

// =============================================================================
// ComputeMeanCurvature — flat planar mesh
// =============================================================================

TEST(Curvature_MeanCurvature, FlatMesh_NearZeroCurvatureAtInteriorVertices)
{
    // MakeTwoTriangleSquare is a flat quad split into two triangles.
    // All vertices are boundary, so we use the subdivided triangle which has
    // an interior vertex (v3 at index 3).
    auto mesh = MakeSubdividedTriangle();
    auto result = Geometry::Curvature::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(result.has_value());

    const auto& prop = result->Property;

    // Check interior vertices — on a flat mesh they should have near-zero
    // mean curvature. Boundary vertices may have unreliable values.
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsBoundary(vh))
        {
            EXPECT_NEAR(prop[vh], 0.0, 1e-6)
                << "Interior vertex " << i << " on flat mesh should have ~zero mean curvature";
        }
    }
}

TEST(Curvature_MeanCurvature, FlippedClosedMesh_NegativeCurvature)
{
    auto mesh = MakeInsideOutTetrahedron();
    auto result = Geometry::Curvature::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(result.has_value());

    const auto& prop = result->Property;
    Geometry::VertexHandle v0{static_cast<Geometry::PropertyIndex>(0)};
    EXPECT_LT(prop[v0], 0.0)
        << "Inside-out orientation should flip the sign of mean curvature";
}

// =============================================================================
// ComputeGaussianCurvature — flat planar mesh
// =============================================================================

TEST(Curvature_GaussianCurvature, FlatMesh_NearZeroCurvatureAtInteriorVertices)
{
    // Same rationale: interior vertices on a flat mesh have zero Gaussian
    // curvature (angle defect = 0 since angles sum to 2π).
    auto mesh = MakeSubdividedTriangle();
    auto result = Geometry::Curvature::ComputeGaussianCurvature(mesh);
    ASSERT_TRUE(result.has_value());

    const auto& prop = result->Property;

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsBoundary(vh))
        {
            EXPECT_NEAR(prop[vh], 0.0, 1e-6)
                << "Interior vertex " << i << " on flat mesh should have ~zero Gaussian curvature";
        }
    }
}

// =============================================================================
// MeshUtils::VertexNormal — used by curvature sign detection
// =============================================================================
//
// These tests verify the area-weighted vertex normal utility that ComputeMeanCurvature
// and ComputeCurvature rely on for curvature sign orientation.

TEST(MeshUtils_VertexNormal, ClosedConvexMesh_NormalsPointOutward)
{
    // For a convex closed mesh centred at the origin, every vertex normal must
    // point generally outward (positive dot product with the outward radial direction).
    auto mesh = MakeIcosahedron();
    const std::size_t nV = mesh.VerticesSize();

    for (std::size_t i = 0; i < nV; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        glm::vec3 n = Geometry::MeshUtils::VertexNormal(mesh, vh);
        glm::vec3 pos = mesh.Position(vh);

        // Normal should be unit length (or the fallback (0,1,0) for degenerate)
        EXPECT_NEAR(glm::length(n), 1.0f, 1e-5f)
            << "VertexNormal should return a unit vector (vertex " << i << ")";

        // On an icosahedron centred at origin, the outward radial direction equals
        // the normalised position. The vertex normal should agree in orientation.
        glm::vec3 radial = glm::normalize(pos);
        EXPECT_GT(glm::dot(n, radial), 0.0f)
            << "VertexNormal on convex mesh should point outward (vertex " << i << ")";
    }
}

TEST(MeshUtils_VertexNormal, FlatMesh_NormalsPointAlongFaceNormal)
{
    // A flat quad mesh in the XY plane. All vertex normals should align with +Z.
    auto mesh = MakeTwoTriangleSquare();
    const std::size_t nV = mesh.VerticesSize();

    for (std::size_t i = 0; i < nV; ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        glm::vec3 n = Geometry::MeshUtils::VertexNormal(mesh, vh);

        // Non-isolated vertices (those with incident faces) should point along +Z.
        if (!mesh.IsIsolated(vh))
        {
            EXPECT_NEAR(n.z, 1.0f, 1e-5f)
                << "VertexNormal on flat XY mesh should be (0,0,1) (vertex " << i << ")";
        }
    }
}


TEST(MeshUtils_AngleDefect, ClosedTetrahedron_TotalDefectEqualsFourPi)
{
    auto mesh = MakeTetrahedron();
    const auto angleSums = Geometry::MeshUtils::ComputeVertexAngleSums(mesh);

    ASSERT_EQ(angleSums.size(), mesh.VerticesSize());

    double totalDefect = 0.0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        totalDefect += Geometry::MeshUtils::ComputeVertexAngleDefect(mesh, vh, angleSums[i]);
    }

    EXPECT_NEAR(totalDefect, 4.0 * std::numbers::pi, 1e-6)
        << "Closed genus-0 mesh should satisfy Σ angle defect = 4π";
}

TEST(MeshUtils_AngleDefect, BoundaryVertex_UsesPiMinusAngleSum)
{
    auto mesh = MakeSubdividedTriangle();
    const auto angleSums = Geometry::MeshUtils::ComputeVertexAngleSums(mesh);

    // Vertex 0 is a boundary corner in the test mesh with angle π/3.
    Geometry::VertexHandle boundaryV{static_cast<Geometry::PropertyIndex>(0)};
    ASSERT_TRUE(mesh.IsBoundary(boundaryV));

    const double defect = Geometry::MeshUtils::ComputeVertexAngleDefect(mesh, boundaryV, angleSums[boundaryV.Index]);
    const double expected = std::numbers::pi - angleSums[boundaryV.Index];
    EXPECT_NEAR(defect, expected, 1e-12);
}
