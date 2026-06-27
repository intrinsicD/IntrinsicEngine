#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    using Geometry::FaceHandle;
    using Geometry::PropertyIndex;
    using Geometry::VertexHandle;
    namespace Curv = Geometry::Curvature;
    namespace MU = Geometry::MeshUtils;

    constexpr double kPi = std::numbers::pi;

    // Closed icosphere of the given radius (vertices exactly on the sphere).
    Geometry::HalfedgeMesh::Mesh MakeIcosphere(float radius, std::uint8_t level)
    {
        Geometry::Sphere sphere{glm::vec3(0.0f), radius};
        auto mesh = Geometry::HalfedgeMesh::MakeMesh(sphere, level);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    // Open cylindrical tube of radius R, length L along +z, closed in the
    // angular direction (welded seam) and open at the two end rings. `nu` is the
    // angular sample count, `nv` the number of axial segments. Each quad is split
    // into four triangles around its on-surface centroid so interior vertices get
    // an isotropic 1-ring (a single-diagonal split injects a scale-invariant
    // helical anisotropy that rotates the principal directions).
    Geometry::HalfedgeMesh::Mesh MakeCylinderTube(double R, double L, int nu, int nv)
    {
        auto onCyl = [&](double th, double z)
        {
            return glm::vec3(static_cast<float>(R * std::cos(th)),
                             static_cast<float>(R * std::sin(th)),
                             static_cast<float>(z));
        };
        std::vector<glm::vec3> pos;
        for (int j = 0; j <= nv; ++j)
        {
            const double z = (static_cast<double>(j) / nv - 0.5) * L;
            for (int i = 0; i < nu; ++i)
                pos.push_back(onCyl(2.0 * kPi * static_cast<double>(i) / nu, z));
        }
        const std::uint32_t gridCount = static_cast<std::uint32_t>(pos.size());
        std::vector<std::uint32_t> idx;
        for (int j = 0; j < nv; ++j)
        {
            const double z0 = (static_cast<double>(j) / nv - 0.5) * L;
            const double z1 = (static_cast<double>(j + 1) / nv - 0.5) * L;
            for (int i = 0; i < nu; ++i)
            {
                const std::uint32_t a = static_cast<std::uint32_t>(j * nu + i);
                const std::uint32_t b = static_cast<std::uint32_t>(j * nu + (i + 1) % nu);
                const std::uint32_t c = static_cast<std::uint32_t>((j + 1) * nu + i);
                const std::uint32_t d = static_cast<std::uint32_t>((j + 1) * nu + (i + 1) % nu);
                const double th0 = 2.0 * kPi * static_cast<double>(i) / nu;
                const double th1 = 2.0 * kPi * static_cast<double>(i + 1) / nu;
                const std::uint32_t e = static_cast<std::uint32_t>(pos.size());
                pos.push_back(onCyl(0.5 * (th0 + th1), 0.5 * (z0 + z1)));
                // Outward-facing fan around the centroid e.
                idx.insert(idx.end(), {a, b, e, b, d, e, d, c, e, c, a, e});
            }
        }
        (void)gridCount;
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    // Height-field grid z = f(x, y) over [-a, a]^2 with `cells` cells per side (so
    // `cells` even => the origin is a corner vertex). Each quad is split into four
    // triangles around its on-surface centroid for an isotropic interior 1-ring.
    // Open boundary.
    template <class F>
    Geometry::HalfedgeMesh::Mesh MakeHeightGrid(double a, int cells, F&& f)
    {
        const int n = cells + 1;
        auto coord = [&](int i) { return -a + 2.0 * a * i / cells; };
        std::vector<glm::vec3> pos;
        for (int iy = 0; iy < n; ++iy)
        {
            for (int ix = 0; ix < n; ++ix)
            {
                const double x = coord(ix);
                const double y = coord(iy);
                pos.push_back(glm::vec3(static_cast<float>(x), static_cast<float>(y),
                                        static_cast<float>(f(x, y))));
            }
        }
        std::vector<std::uint32_t> idx;
        for (int iy = 0; iy < cells; ++iy)
        {
            for (int ix = 0; ix < cells; ++ix)
            {
                const std::uint32_t v00 = static_cast<std::uint32_t>(iy * n + ix);
                const std::uint32_t v10 = v00 + 1;
                const std::uint32_t v01 = static_cast<std::uint32_t>((iy + 1) * n + ix);
                const std::uint32_t v11 = v01 + 1;
                const double cx = 0.5 * (coord(ix) + coord(ix + 1));
                const double cy = 0.5 * (coord(iy) + coord(iy + 1));
                const std::uint32_t e = static_cast<std::uint32_t>(pos.size());
                pos.push_back(glm::vec3(static_cast<float>(cx), static_cast<float>(cy),
                                        static_cast<float>(f(cx, cy))));
                idx.insert(idx.end(), {v00, v10, e, v10, v11, e, v11, v01, e, v01, v00, e});
            }
        }
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    bool IsZeroVec(const glm::vec3& v) { return glm::length(v) == 0.0f; }
}

// =============================================================================
// Empty / no-face meshes -> nullopt (matches the scalar-curvature contract).
// =============================================================================

TEST(CurvatureTensor, EmptyMesh_ReturnsNullopt)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    EXPECT_FALSE(Curv::ComputeCurvatureTensor(mesh).has_value());
}

TEST(CurvatureTensor, NoFaceMesh_ReturnsNullopt)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    (void)mesh.AddVertex({0.0f, 0.0f, 0.0f});
    (void)mesh.AddVertex({1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(Curv::ComputeCurvatureTensor(mesh).has_value());
}

// =============================================================================
// Sphere: isotropic principal curvatures (κ₁ ≈ κ₂ ≈ 1/R), orthonormal tangents.
// =============================================================================

TEST(CurvatureTensor, Sphere_IsotropicAndTangent)
{
    const float R = 1.0f;
    auto mesh = MakeIcosphere(R, 3);
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

    int interior = 0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        VertexHandle vh{static_cast<PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        const glm::vec3 d1 = result->PrincipalDir1Property[vh];
        const glm::vec3 d2 = result->PrincipalDir2Property[vh];
        if (IsZeroVec(d1) || IsZeroVec(d2)) continue; // skip any fail-closed vertex
        ++interior;

        const double k1 = result->MaxPrincipalCurvatureProperty[vh];
        const double k2 = result->MinPrincipalCurvatureProperty[vh];
        // Isotropy: principal curvatures within tolerance of each other and ≈ 1/R.
        EXPECT_NEAR(std::abs(k1), 1.0 / R, 0.25);
        EXPECT_NEAR(std::abs(k2), 1.0 / R, 0.25);
        EXPECT_LT(std::abs(std::abs(k1) - std::abs(k2)), 0.25);

        // Orthonormal + tangent.
        const glm::vec3 n = MU::VertexNormal(mesh, vh);
        EXPECT_NEAR(glm::length(d1), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::length(d2), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(d1, d2), 0.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(d1, glm::normalize(n)), 0.0f, 1e-3f);
        EXPECT_NEAR(glm::dot(d2, glm::normalize(n)), 0.0f, 1e-3f);
    }
    EXPECT_GT(interior, 100);
}

// =============================================================================
// Cylinder: one principal curvature ≈ 0 (axial), one ≈ 1/R (circumferential).
// =============================================================================

TEST(CurvatureTensor, Cylinder_AxisAligned)
{
    const double R = 1.0;
    const double L = 4.0;
    const int nu = 48;
    const int nv = 24;
    auto mesh = MakeCylinderTube(R, L, nu, nv);
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

    const glm::vec3 axis(0.0f, 0.0f, 1.0f);
    int tested = 0;
    // Interior axial rings: j in [4, nv-4] keeps a margin from the open ends.
    for (int j = 6; j <= nv - 6; ++j)
    {
        for (int i = 0; i < nu; i += 12)
        {
            VertexHandle vh{static_cast<PropertyIndex>(j * nu + i)};
            const glm::vec3 d1 = result->PrincipalDir1Property[vh];
            const glm::vec3 d2 = result->PrincipalDir2Property[vh];
            if (IsZeroVec(d1) || IsZeroVec(d2)) continue;

            const double k1 = result->MaxPrincipalCurvatureProperty[vh];
            const double k2 = result->MinPrincipalCurvatureProperty[vh];
            // Identify the near-zero-curvature direction (axial) by magnitude.
            const bool firstIsAxial = std::abs(k1) < std::abs(k2);
            const glm::vec3 axialDir = firstIsAxial ? d1 : d2;
            const glm::vec3 circDir = firstIsAxial ? d2 : d1;
            const double axialK = firstIsAxial ? k1 : k2;
            const double circK = firstIsAxial ? k2 : k1;

            EXPECT_NEAR(std::abs(axialK), 0.0, 0.15) << "axial curvature should vanish";
            EXPECT_NEAR(std::abs(circK), 1.0 / R, 0.2) << "circumferential curvature ≈ 1/R";
            EXPECT_GT(std::abs(glm::dot(axialDir, axis)), 0.9f) << "zero-curvature dir ∥ axis";
            EXPECT_LT(std::abs(glm::dot(circDir, axis)), 0.1f) << "1/R dir ⟂ axis";
            ++tested;
        }
    }
    EXPECT_GT(tested, 0);
}

// =============================================================================
// Saddle z = x² − y²: opposite-sign principal curvatures along the x / y axes.
// =============================================================================

TEST(CurvatureTensor, Saddle_OppositeSignsAxisAligned)
{
    const double a = 0.3;
    const int cells = 30; // even -> origin is a vertex
    auto mesh = MakeHeightGrid(a, cells, [](double x, double y) { return x * x - y * y; });
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

    const int n = cells + 1;
    const int centerIdx = (cells / 2) * n + (cells / 2);
    VertexHandle center{static_cast<PropertyIndex>(centerIdx)};
    // Confirm we picked the origin vertex.
    ASSERT_NEAR(glm::length(mesh.Position(center)), 0.0f, 1e-5f);

    const glm::vec3 d1 = result->PrincipalDir1Property[center];
    const glm::vec3 d2 = result->PrincipalDir2Property[center];
    ASSERT_FALSE(IsZeroVec(d1));
    ASSERT_FALSE(IsZeroVec(d2));

    const double k1 = result->MaxPrincipalCurvatureProperty[center]; // max (positive)
    const double k2 = result->MinPrincipalCurvatureProperty[center]; // min (negative)
    EXPECT_GT(k1, 0.0);
    EXPECT_LT(k2, 0.0);
    EXPECT_LT(k1 * k2, 0.0) << "principal curvatures must have opposite signs";
    EXPECT_NEAR(k1, 2.0, 0.4);
    EXPECT_NEAR(k2, -2.0, 0.4);

    // Orthogonal, and aligned with the x (positive κ) / y (negative κ) axes.
    EXPECT_NEAR(glm::dot(d1, d2), 0.0f, 1e-4f);
    EXPECT_GT(std::abs(d1.x), 0.9f) << "max-curvature direction ∥ x axis";
    EXPECT_GT(std::abs(d2.y), 0.9f) << "min-curvature direction ∥ y axis";
}

// =============================================================================
// Determinism: identical output across repeated runs.
// =============================================================================

TEST(CurvatureTensor, Deterministic)
{
    auto mesh = MakeIcosphere(1.0f, 3);
    auto first = Curv::ComputeCurvatureTensor(mesh);
    auto second = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        VertexHandle vh{static_cast<PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;
        EXPECT_EQ(first->PrincipalDir1Property[vh], second->PrincipalDir1Property[vh]);
        EXPECT_EQ(first->PrincipalDir2Property[vh], second->PrincipalDir2Property[vh]);
        EXPECT_EQ(first->MaxPrincipalCurvatureProperty[vh], second->MaxPrincipalCurvatureProperty[vh]);
        EXPECT_EQ(first->MinPrincipalCurvatureProperty[vh], second->MinPrincipalCurvatureProperty[vh]);
    }
}

// =============================================================================
// Scalar preservation: ComputeCurvature's H/K are unchanged (still equal to the
// independent ComputeMeanCurvature / ComputeGaussianCurvature outputs), and it
// now additionally publishes the principal-direction fields.
// =============================================================================

TEST(CurvatureTensor, ComputeCurvaturePreservesScalarsAndPublishesDirections)
{
    auto mesh = MakeIcosphere(1.0f, 3);

    auto meanOnly = Curv::ComputeMeanCurvature(mesh);
    auto gaussOnly = Curv::ComputeGaussianCurvature(mesh);
    ASSERT_TRUE(meanOnly.has_value());
    ASSERT_TRUE(gaussOnly.has_value());

    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);

    int published = 0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        VertexHandle vh{static_cast<PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        EXPECT_DOUBLE_EQ(field.MeanCurvatureProperty[vh], meanOnly->Property[vh]);
        EXPECT_DOUBLE_EQ(field.GaussianCurvatureProperty[vh], gaussOnly->Property[vh]);
        if (!IsZeroVec(field.PrincipalDir1Property[vh])) ++published;
    }
    EXPECT_GT(published, 100) << "ComputeCurvature should publish principal directions";
}

// =============================================================================
// Fail-closed: flat region, boundary vertex -> zero-sentinel directions, no NaN.
// =============================================================================

TEST(CurvatureTensor, FlatRegionAndBoundaryFailClosed)
{
    const double a = 1.0;
    const int cells = 10;
    auto mesh = MakeHeightGrid(a, cells, [](double, double) { return 0.0; }); // planar
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

    const int n = cells + 1;
    // Interior vertex of a flat patch: tensor is numerically zero -> sentinel.
    VertexHandle interior{static_cast<PropertyIndex>((cells / 2) * n + (cells / 2))};
    EXPECT_TRUE(IsZeroVec(result->PrincipalDir1Property[interior]));
    EXPECT_TRUE(IsZeroVec(result->PrincipalDir2Property[interior]));

    // Boundary vertex (corner of the grid) -> sentinel.
    VertexHandle corner{static_cast<PropertyIndex>(0)};
    ASSERT_TRUE(mesh.IsBoundary(corner));
    EXPECT_TRUE(IsZeroVec(result->PrincipalDir1Property[corner]));
    EXPECT_TRUE(IsZeroVec(result->PrincipalDir2Property[corner]));

    // No NaN/Inf anywhere.
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        VertexHandle vh{static_cast<PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;
        const glm::vec3 d1 = result->PrincipalDir1Property[vh];
        const glm::vec3 d2 = result->PrincipalDir2Property[vh];
        EXPECT_TRUE(std::isfinite(d1.x) && std::isfinite(d1.y) && std::isfinite(d1.z));
        EXPECT_TRUE(std::isfinite(d2.x) && std::isfinite(d2.y) && std::isfinite(d2.z));
        EXPECT_TRUE(std::isfinite(result->MaxPrincipalCurvatureProperty[vh]));
        EXPECT_TRUE(std::isfinite(result->MinPrincipalCurvatureProperty[vh]));
    }
}
