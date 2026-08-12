#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
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

    Geometry::HalfedgeMesh::Mesh MakeTetrahedron(bool reversed)
    {
        const std::vector<glm::vec3> positions{
            {1.0f, 1.0f, 1.0f},
            {1.0f, -1.0f, -1.0f},
            {-1.0f, 1.0f, -1.0f},
            {-1.0f, -1.0f, 1.0f}};
        std::vector<std::uint32_t> indices{
            0u, 1u, 2u,
            0u, 2u, 3u,
            0u, 3u, 1u,
            1u, 3u, 2u};
        if (reversed)
        {
            for (std::size_t i = 0; i < indices.size(); i += 3u)
                std::swap(indices[i + 1u], indices[i + 2u]);
        }
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(positions, indices);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    // Open cylindrical tube of radius R, length L along +z, closed in the
    // angular direction and open at the two end rings. Alternating quad
    // diagonals exercise the estimator on an ordinary triangle tessellation.
    Geometry::HalfedgeMesh::Mesh MakeCylinderTube(
        double R,
        double L,
        int nu,
        int nv,
        bool reversed = false)
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
        std::vector<std::uint32_t> idx;
        for (int j = 0; j < nv; ++j)
        {
            for (int i = 0; i < nu; ++i)
            {
                const std::uint32_t a = static_cast<std::uint32_t>(j * nu + i);
                const std::uint32_t b = static_cast<std::uint32_t>(j * nu + (i + 1) % nu);
                const std::uint32_t c = static_cast<std::uint32_t>((j + 1) * nu + i);
                const std::uint32_t d = static_cast<std::uint32_t>((j + 1) * nu + (i + 1) % nu);
                if ((i + j) % 2 == 0)
                    idx.insert(idx.end(), {a, b, d, a, d, c});
                else
                    idx.insert(idx.end(), {a, b, c, b, d, c});
            }
        }
        if (reversed)
        {
            for (std::size_t i = 0; i < idx.size(); i += 3u)
                std::swap(idx[i + 1u], idx[i + 2u]);
        }
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    // Height-field grid z = f(x, y) over [-a, a]^2 with `cells` cells per side.
    // Alternating diagonals avoid a preferred mesh direction without adding
    // artificial centroid vertices. Open boundary.
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
                if ((ix + iy) % 2 == 0)
                    idx.insert(idx.end(), {v00, v10, v11, v00, v11, v01});
                else
                    idx.insert(idx.end(), {v00, v10, v01, v10, v11, v01});
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

TEST(CurvatureTensor, PrincipalCurvaturesScaleInverselyWithGeometry)
{
    auto unit = MakeIcosphere(1.0f, 3);
    auto scaled = MakeIcosphere(3.0f, 3);
    auto unitResult = Curv::ComputeCurvatureTensor(unit);
    auto scaledResult = Curv::ComputeCurvatureTensor(scaled);
    ASSERT_TRUE(unitResult.has_value());
    ASSERT_TRUE(scaledResult.has_value());
    ASSERT_EQ(unit.VerticesSize(), scaled.VerticesSize());

    for (std::size_t i = 0; i < unit.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        EXPECT_NEAR(
            unitResult->MaxPrincipalCurvatureProperty[vertex],
            3.0 * scaledResult->MaxPrincipalCurvatureProperty[vertex],
            2.0e-5);
        EXPECT_NEAR(
            unitResult->MinPrincipalCurvatureProperty[vertex],
            3.0 * scaledResult->MinPrincipalCurvatureProperty[vertex],
            2.0e-5);
    }
}

TEST(CurvatureTensor, PrincipalCurvaturesRemainScaleInvariantAtExtremeScales)
{
    auto unit = MakeIcosphere(1.0f, 2);
    auto tiny = MakeIcosphere(1.0f, 2);
    auto huge = MakeIcosphere(1.0f, 2);
    for (std::size_t i = 0u; i < unit.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        tiny.Position(vertex) *= 1.0e-6f;
        huge.Position(vertex) *= 1.0e6f;
    }
    auto unitResult = Curv::ComputeCurvatureTensor(unit);
    auto tinyResult = Curv::ComputeCurvatureTensor(tiny);
    auto hugeResult = Curv::ComputeCurvatureTensor(huge);
    ASSERT_TRUE(unitResult.has_value());
    ASSERT_TRUE(tinyResult.has_value());
    ASSERT_TRUE(hugeResult.has_value());
    ASSERT_EQ(unit.VerticesSize(), tiny.VerticesSize());
    ASSERT_EQ(unit.VerticesSize(), huge.VerticesSize());
    EXPECT_EQ(
        unitResult->Diagnostics.SupportedVertexCount,
        tinyResult->Diagnostics.SupportedVertexCount);
    EXPECT_EQ(
        unitResult->Diagnostics.SupportedVertexCount,
        hugeResult->Diagnostics.SupportedVertexCount);
    EXPECT_EQ(tinyResult->Diagnostics.IllConditionedFaceCount, 0u);
    EXPECT_EQ(hugeResult->Diagnostics.IllConditionedFaceCount, 0u);

    for (std::size_t i = 0; i < unit.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        for (const auto values : {
                 std::array{
                     unitResult->MaxPrincipalCurvatureProperty[vertex],
                     tinyResult->MaxPrincipalCurvatureProperty[vertex],
                     hugeResult->MaxPrincipalCurvatureProperty[vertex]},
                 std::array{
                     unitResult->MinPrincipalCurvatureProperty[vertex],
                     tinyResult->MinPrincipalCurvatureProperty[vertex],
                     hugeResult->MinPrincipalCurvatureProperty[vertex]}})
        {
            EXPECT_NEAR(values[0], 1.0e-6 * values[1], 3.0e-5);
            EXPECT_NEAR(values[0], 1.0e6 * values[2], 3.0e-5);
        }
    }
}

TEST(CurvatureTensor, ReversingOrientationFlipsSignedCurvature)
{
    auto outward = MakeTetrahedron(false);
    auto inward = MakeTetrahedron(true);
    const Curv::CurvatureField outwardField = Curv::ComputeCurvature(outward);
    const Curv::CurvatureField inwardField = Curv::ComputeCurvature(inward);

    for (std::size_t i = 0; i < outward.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        EXPECT_GT(outwardField.MeanCurvatureProperty[vertex], 0.0);
        EXPECT_LT(inwardField.MeanCurvatureProperty[vertex], 0.0);
        EXPECT_NEAR(
            outwardField.MeanCurvatureProperty[vertex],
            -inwardField.MeanCurvatureProperty[vertex],
            1.0e-12);
        EXPECT_NEAR(
            outwardField.GaussianCurvatureProperty[vertex],
            inwardField.GaussianCurvatureProperty[vertex],
            1.0e-12);
    }
}

TEST(CurvatureTensor, ReversingOrientationSwapsAnisotropicOrderedPairs)
{
    auto outward = MakeCylinderTube(1.0, 4.0, 48, 24);
    auto inward = MakeCylinderTube(1.0, 4.0, 48, 24, true);
    auto outwardResult = Curv::ComputeCurvatureTensor(outward);
    auto inwardResult = Curv::ComputeCurvatureTensor(inward);
    ASSERT_TRUE(outwardResult.has_value());
    ASSERT_TRUE(inwardResult.has_value());

    const VertexHandle vertex{static_cast<PropertyIndex>(12 * 48)};
    EXPECT_NEAR(
        outwardResult->MaxPrincipalCurvatureProperty[vertex],
        -inwardResult->MinPrincipalCurvatureProperty[vertex],
        1.0e-12);
    EXPECT_NEAR(
        outwardResult->MinPrincipalCurvatureProperty[vertex],
        -inwardResult->MaxPrincipalCurvatureProperty[vertex],
        1.0e-12);
    EXPECT_GT(std::abs(glm::dot(
        outwardResult->PrincipalDir1Property[vertex],
        inwardResult->PrincipalDir2Property[vertex])), 0.999f);
    EXPECT_GT(std::abs(glm::dot(
        outwardResult->PrincipalDir2Property[vertex],
        inwardResult->PrincipalDir1Property[vertex])), 0.999f);
}

TEST(CurvatureTensor, MatchesEdgeDihedralReferenceOracle)
{
    auto mesh = MakeHeightGrid(
        1.0,
        4,
        [](double x, double y)
        {
            return 0.15 * x * x - 0.08 * y * y + 0.05 * x * y + 0.03 * x;
        });
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Diagnostics.SupportedVertexCount, 25u);
    EXPECT_EQ(result->Diagnostics.NonZeroPrincipalVertexCount, 25u);

    // Generated from an independent NumPy replica of the one-ring signed
    // edge-dihedral formulation (no post-smoothing, uniform boundary
    // interpolation) on this exact float-quantized mesh. Distinguishes the
    // superseded two-ring/triple-smoothing pipeline, whose values differ in
    // the second decimal.
    constexpr std::array<double, 25> expectedMinimum{
        -0.30763604818085394, -0.30763604818085394, -0.30203228463579473,
        -0.30107879285620004, -0.30107879285620004, -0.30763604818085394,
        -0.30763604818085394, -0.29738201287033017, -0.30107879285620004,
        -0.30107879285620004, -0.30284621585438321, -0.29181010591874701,
        -0.31685407473095040, -0.28462753927691398, -0.29478966003858620,
        -0.30909249346354861, -0.30909249346354861, -0.29606906482768247,
        -0.29866264798264458, -0.29866264798264458, -0.30909249346354861,
        -0.30909249346354861, -0.30127473542462524, -0.29866264798264458,
        -0.29866264798264458};
    constexpr std::array<double, 25> expectedMaximum{
        0.17454539053529400, 0.17454539053529400, 0.16833233952847096,
        0.17326714295448911, 0.17326714295448911, 0.17454539053529400,
        0.17454539053529400, 0.15718448509562977, 0.17326714295448911,
        0.17326714295448911, 0.16884770609030286, 0.15734881250747568,
        0.17811381091552106, 0.15619334591518119, 0.16722405894622683,
        0.17464891522813897, 0.17464891522813897, 0.15695800622893832,
        0.17221168796901024, 0.17221168796901024, 0.17464891522813897,
        0.17464891522813897, 0.16793953647536250, 0.17221168796901024,
        0.17221168796901024};

    ASSERT_EQ(mesh.VerticesSize(), expectedMinimum.size());
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        EXPECT_NEAR(
            result->MinPrincipalCurvatureProperty[vertex],
            expectedMinimum[i],
            1.0e-12)
            << "vertex " << i;
        EXPECT_NEAR(
            result->MaxPrincipalCurvatureProperty[vertex],
            expectedMaximum[i],
            1.0e-12)
            << "vertex " << i;
    }
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

TEST(CurvatureTensor, OpenCylinderBoundaryUsesSupportedNeighbourhood)
{
    auto mesh = MakeCylinderTube(1.0, 4.0, 48, 24);
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

    int estimated = 0;
    for (int i = 0; i < 48; i += 6)
    {
        const VertexHandle boundary{static_cast<PropertyIndex>(i)};
        ASSERT_TRUE(mesh.IsBoundary(boundary));

        const glm::vec3 d1 = result->PrincipalDir1Property[boundary];
        const glm::vec3 d2 = result->PrincipalDir2Property[boundary];
        EXPECT_FALSE(IsZeroVec(d1));
        EXPECT_FALSE(IsZeroVec(d2));
        EXPECT_GT(std::max(
            std::abs(result->MaxPrincipalCurvatureProperty[boundary]),
            std::abs(result->MinPrincipalCurvatureProperty[boundary])), 0.25);
        estimated += !IsZeroVec(d1) && !IsZeroVec(d2);
    }
    EXPECT_EQ(estimated, 8);
}

// =============================================================================
// Saddle z = x² − y²: the engine's positive-outward-convex convention is
// the negative of the graph-normal shape operator, so κ_max follows y and
// κ_min follows x at the origin.
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

    // Orthogonal, and aligned with the y (positive κ) / x (negative κ) axes.
    EXPECT_NEAR(glm::dot(d1, d2), 0.0f, 1e-4f);
    EXPECT_GT(std::abs(d1.y), 0.9f) << "max-curvature direction ∥ y axis";
    EXPECT_GT(std::abs(d2.x), 0.9f) << "min-curvature direction ∥ x axis";
}

// =============================================================================
// Fail-closed: a single non-finite corner on a CLOSED mesh must not poison the
// finite interior neighbours via a NaN vertex normal.
// =============================================================================

TEST(CurvatureTensor, NonFiniteCornerOnClosedMeshFailsClosed)
{
    // Regular tetrahedron with one NaN vertex. Every other vertex is incident
    // to a face containing the bad vertex, so each gets a NaN VertexNormal; the
    // tensor path must fail closed rather than publish NaN directions/curvatures.
    Geometry::HalfedgeMesh::Mesh mesh;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto v0 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
    const auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
    const auto v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
    const auto v3 = mesh.AddVertex({nan, -1.0f, 1.0f}); // poisoned corner
    (void)mesh.AddTriangle(v0, v2, v1);
    (void)mesh.AddTriangle(v0, v3, v2);
    (void)mesh.AddTriangle(v0, v1, v3);
    (void)mesh.AddTriangle(v1, v2, v3);

    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

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

TEST(CurvatureTensor, IllConditionedFacesAreDiagnosedAndFailClosedLocally)
{
    constexpr int cells = 12;
    auto mesh = MakeHeightGrid(
        1.0,
        cells,
        [](double x, double y) { return 0.2 * (x * x + y * y); });
    const int rowWidth = cells + 1;
    const VertexHandle center{
        static_cast<PropertyIndex>((cells / 2) * rowWidth + cells / 2)};
    const VertexHandle right{center.Index + 1u};
    mesh.Position(center) = mesh.Position(right) + glm::vec3(1.0e-6f, 0.0f, 0.0f);

    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Diagnostics.DegenerateFaceCount, 0u);
    EXPECT_GT(result->Diagnostics.IllConditionedFaceCount, 0u);
    EXPECT_LT(
        result->Diagnostics.MinimumTriangleQuality,
        Curv::kMinimumReliableTriangleQuality);
    EXPECT_LT(result->Diagnostics.SupportedVertexCount, mesh.VertexCount());
    EXPECT_GT(result->Diagnostics.SupportedVertexCount, 0u);

    for (const VertexHandle invalid : {center, right})
    {
        EXPECT_DOUBLE_EQ(
            result->MinPrincipalCurvatureProperty[invalid], 0.0);
        EXPECT_DOUBLE_EQ(
            result->MaxPrincipalCurvatureProperty[invalid], 0.0);
        EXPECT_TRUE(IsZeroVec(result->PrincipalDir1Property[invalid]));
        EXPECT_TRUE(IsZeroVec(result->PrincipalDir2Property[invalid]));
    }
    const VertexHandle far{
        static_cast<PropertyIndex>(2 * rowWidth + 2)};
    EXPECT_GT(
        std::abs(result->MinPrincipalCurvatureProperty[far])
            + std::abs(result->MaxPrincipalCurvatureProperty[far]),
        0.0);

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        EXPECT_TRUE(std::isfinite(
            result->MaxPrincipalCurvatureProperty[vertex]));
        EXPECT_TRUE(std::isfinite(
            result->MinPrincipalCurvatureProperty[vertex]));
        const glm::vec3 direction1 = result->PrincipalDir1Property[vertex];
        const glm::vec3 direction2 = result->PrincipalDir2Property[vertex];
        EXPECT_TRUE(std::isfinite(direction1.x));
        EXPECT_TRUE(std::isfinite(direction1.y));
        EXPECT_TRUE(std::isfinite(direction1.z));
        EXPECT_TRUE(std::isfinite(direction2.x));
        EXPECT_TRUE(std::isfinite(direction2.y));
        EXPECT_TRUE(std::isfinite(direction2.z));
    }
}

// =============================================================================
// Determinism: identical output across repeated runs.
// =============================================================================

TEST(CurvatureTensor, Deterministic)
{
    auto firstMesh = MakeIcosphere(1.0f, 3);
    auto secondMesh = MakeIcosphere(1.0f, 3);
    auto first = Curv::ComputeCurvatureTensor(firstMesh);
    auto second = Curv::ComputeCurvatureTensor(secondMesh);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(firstMesh.VerticesSize(), secondMesh.VerticesSize());

    for (std::size_t i = 0; i < firstMesh.VerticesSize(); ++i)
    {
        VertexHandle vh{static_cast<PropertyIndex>(i)};
        if (firstMesh.IsDeleted(vh) || secondMesh.IsDeleted(vh)) continue;
        EXPECT_EQ(first->PrincipalDir1Property[vh], second->PrincipalDir1Property[vh]);
        EXPECT_EQ(first->PrincipalDir2Property[vh], second->PrincipalDir2Property[vh]);
        EXPECT_EQ(first->MaxPrincipalCurvatureProperty[vh], second->MaxPrincipalCurvatureProperty[vh]);
        EXPECT_EQ(first->MinPrincipalCurvatureProperty[vh], second->MinPrincipalCurvatureProperty[vh]);
    }
}

// =============================================================================
// The standalone Meyer scalar operators remain available independently of the
// coherent edge-dihedral full field.
// =============================================================================

TEST(CurvatureTensor, StandaloneScalarOperatorsRemainAvailable)
{
    auto scalarMesh = MakeIcosphere(1.0f, 3);
    auto fieldMesh = MakeIcosphere(1.0f, 3);

    auto meanOnly = Curv::ComputeMeanCurvature(scalarMesh);
    auto gaussOnly = Curv::ComputeGaussianCurvature(scalarMesh);
    ASSERT_TRUE(meanOnly.has_value());
    ASSERT_TRUE(gaussOnly.has_value());

    const Curv::CurvatureField field = Curv::ComputeCurvature(fieldMesh);

    int published = 0;
    for (std::size_t i = 0; i < scalarMesh.VerticesSize(); ++i)
    {
        VertexHandle vh{static_cast<PropertyIndex>(i)};
        if (scalarMesh.IsDeleted(vh) || scalarMesh.IsIsolated(vh)) continue;
        EXPECT_NEAR(std::abs(meanOnly->Property[vh]), 1.0, 0.15);
        EXPECT_NEAR(gaussOnly->Property[vh], 1.0, 0.25);
        if (!IsZeroVec(field.PrincipalDir1Property[vh])) ++published;
    }
    EXPECT_GT(published, 100) << "ComputeCurvature should publish principal directions";
}

TEST(CurvatureTensor, FullFieldUsesTheTensorPrincipalSystem)
{
    auto mesh = MakeCylinderTube(1.0, 4.0, 48, 24);
    auto tensor = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(tensor.has_value());

    std::vector<double> maxPrincipal(mesh.VerticesSize(), 0.0);
    std::vector<double> minPrincipal(mesh.VerticesSize(), 0.0);
    std::vector<glm::vec3> maxDirections(mesh.VerticesSize(), glm::vec3(0.0f));
    std::vector<glm::vec3> minDirections(mesh.VerticesSize(), glm::vec3(0.0f));
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        maxPrincipal[i] = tensor->MaxPrincipalCurvatureProperty[vertex];
        minPrincipal[i] = tensor->MinPrincipalCurvatureProperty[vertex];
        maxDirections[i] = tensor->PrincipalDir1Property[vertex];
        minDirections[i] = tensor->PrincipalDir2Property[vertex];
    }

    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);
    int compared = 0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        if (mesh.IsDeleted(vertex) || IsZeroVec(maxDirections[i]) || IsZeroVec(minDirections[i]))
            continue;

        EXPECT_DOUBLE_EQ(field.MaxPrincipalCurvatureProperty[vertex], maxPrincipal[i]);
        EXPECT_DOUBLE_EQ(field.MinPrincipalCurvatureProperty[vertex], minPrincipal[i]);
        EXPECT_DOUBLE_EQ(
            field.MeanCurvatureProperty[vertex],
            0.5 * (maxPrincipal[i] + minPrincipal[i]));
        EXPECT_DOUBLE_EQ(
            field.GaussianCurvatureProperty[vertex],
            maxPrincipal[i] * minPrincipal[i]);
        EXPECT_EQ(field.PrincipalDir1Property[vertex], maxDirections[i]);
        EXPECT_EQ(field.PrincipalDir2Property[vertex], minDirections[i]);
        ++compared;
    }
    EXPECT_GT(compared, 1000);
}

// =============================================================================
// A supported flat region, including interpolated boundaries, has zero hinge
// curvature and publishes finite zero direction sentinels.
// =============================================================================

TEST(CurvatureTensor, FlatRegionPublishesFiniteZeroDirections)
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

    // A boundary vertex on the same flat grid inherits supported zero scalars,
    // but a flat tensor has no unique principal direction.
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

// =============================================================================
// BUG-156 — sharp-crease flanks must keep their genuine curvature. The
// superseded two-ring support integrated crease bending into flanking smooth
// vertices, and the three damped eigenvalue-smoothing passes then cancelled
// their mean curvature toward zero (up to 79% loss on this fixture).
// =============================================================================

TEST(CurvatureTensor, CreaseFlanksKeepGenuineCurvature)
{
    // Tent ridge along x = 0 with parabolic flanks. The independent Meyer
    // cotan operator supplies the per-vertex cross-check magnitude.
    constexpr int cells = 20;
    auto mesh = MakeHeightGrid(
        1.0,
        cells,
        [](double x, double) { return -0.6 * std::abs(x) + 0.55 * x * x; });

    auto meyer = Curv::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(meyer.has_value());
    // ComputeCurvature republishes the same property names, so copy first.
    const std::vector<double> meyerMean = meyer->Property.Vector();

    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);

    constexpr int n = cells + 1;
    int probed = 0;
    for (int iy = 7; iy <= 13; ++iy)
    {
        for (const int ix : {12, 13})
        {
            const VertexHandle vertex{
                static_cast<PropertyIndex>(iy * n + ix)};
            ASSERT_FALSE(mesh.IsBoundary(vertex));
            const double meanTensor = field.MeanCurvatureProperty[vertex];
            const double meanMeyer = meyerMean[vertex.Index];
            ASSERT_GT(std::abs(meanMeyer), 0.2);
            EXPECT_GT(meanTensor * meanMeyer, 0.0)
                << "sign flip at (" << ix << ", " << iy << ")";
            EXPECT_GE(std::abs(meanTensor), 0.3 * std::abs(meanMeyer))
                << "curvature lost at (" << ix << ", " << iy << ")";
            ++probed;
        }
    }
    EXPECT_EQ(probed, 14);
}

// =============================================================================
// BUG-156 acceptance — the closed genus-2 sculpt asset that exposed the
// defect: every vertex is interior with clean support, and the independent
// Meyer field shows |H| >= ~1.9 everywhere, yet the superseded pipeline
// published 65 near-zero vertices and 917 sign flips.
// =============================================================================

TEST(CurvatureTensor, SculptAssetHasNoZeroCurvatureBands)
{
    const std::filesystem::path fixture =
        std::filesystem::path(__FILE__)
            .parent_path()
            .parent_path()
            .parent_path()
        / "data" / "sculpt.obj";
    ASSERT_TRUE(std::filesystem::exists(fixture)) << fixture.string();

    auto payload = Geometry::MeshIO::LoadOBJ(fixture.string());
    ASSERT_TRUE(payload.has_value());
    const auto positions = payload->Vertices.Get<glm::vec3>("v:point");
    const auto faces =
        payload->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(positions.IsValid());
    ASSERT_TRUE(faces.IsValid());
    std::vector<std::uint32_t> indices;
    indices.reserve(faces.Vector().size() * 3u);
    for (const std::vector<std::uint32_t>& face : faces.Vector())
    {
        ASSERT_EQ(face.size(), 3u);
        indices.insert(indices.end(), face.begin(), face.end());
    }
    auto built = MU::BuildHalfedgeMeshFromIndexedTriangles(
        positions.Vector(), indices);
    ASSERT_TRUE(built.has_value());
    Geometry::HalfedgeMesh::Mesh& mesh = *built;
    ASSERT_EQ(mesh.VertexCount(), 3669u);
    ASSERT_EQ(mesh.EdgeCount(), 11013u);
    ASSERT_EQ(mesh.FaceCount(), 7342u);
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        ASSERT_FALSE(mesh.IsBoundary(
            VertexHandle{static_cast<PropertyIndex>(i)}));
    }

    auto meyer = Curv::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(meyer.has_value());
    const std::vector<double> meyerMean = meyer->Property.Vector();

    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);
    EXPECT_EQ(field.Diagnostics.SupportedVertexCount, 3669u);
    EXPECT_EQ(field.Diagnostics.NonZeroPrincipalVertexCount, 3669u);

    const auto medianAbs = [](std::vector<double> values)
    {
        for (double& value : values)
            value = std::abs(value);
        const std::size_t middle = values.size() / 2u;
        std::nth_element(values.begin(), values.begin() + middle, values.end());
        return values[middle];
    };
    const std::vector<double> tensorMean =
        field.MeanCurvatureProperty.Vector();
    const double tensorScale = medianAbs(tensorMean);
    const double meyerScale = medianAbs(meyerMean);
    ASSERT_GT(tensorScale, 0.0);
    ASSERT_GT(meyerScale, 0.0);

    std::size_t zeroBand = 0;
    std::size_t signFlips = 0;
    for (std::size_t i = 0; i < tensorMean.size(); ++i)
    {
        const bool meyerCurved = std::abs(meyerMean[i]) > 0.5 * meyerScale;
        if (meyerCurved && std::abs(tensorMean[i]) < 0.05 * tensorScale)
            ++zeroBand;
        if (meyerCurved && std::abs(tensorMean[i]) > 0.1 * tensorScale
            && tensorMean[i] * meyerMean[i] < 0.0)
        {
            ++signFlips;
        }
    }
    // Superseded pipeline: 65 zero-band vertices and 917 sign flips.
    EXPECT_EQ(zeroBand, 0u);
    EXPECT_EQ(signFlips, 0u);
}

// =============================================================================
// Published H and K equal the principal invariants at every vertex, including
// unsupported vertices holding zero sentinels; diagnostics extrema bound every
// nonzero published value. The superseded smoothing violated both at boundary
// vertices with no interior one-ring neighbour (BUG-154 review finding).
// =============================================================================

TEST(CurvatureTensor, OpenMeshFullFieldKeepsPrincipalInvariants)
{
    // Uniform (non-alternating) diagonals leave two grid corners without an
    // interior one-ring neighbour, so those vertices stay unsupported while
    // their boundary neighbours carry interpolated values.
    constexpr int cells = 4;
    constexpr int n = cells + 1;
    std::vector<glm::vec3> pos;
    for (int iy = 0; iy < n; ++iy)
    {
        for (int ix = 0; ix < n; ++ix)
        {
            const double x = -1.0 + 2.0 * ix / cells;
            const double y = -1.0 + 2.0 * iy / cells;
            pos.push_back(glm::vec3(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(0.2 * (x * x + y * y))));
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
            idx.insert(idx.end(), {v00, v10, v11, v00, v11, v01});
        }
    }
    auto built = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
    ASSERT_TRUE(built.has_value());
    Geometry::HalfedgeMesh::Mesh& mesh = *built;

    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);

    std::size_t nonZero = 0;
    bool sawUnsupported = false;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vertex{static_cast<PropertyIndex>(i)};
        const double maxPrincipal =
            field.MaxPrincipalCurvatureProperty[vertex];
        const double minPrincipal =
            field.MinPrincipalCurvatureProperty[vertex];
        EXPECT_DOUBLE_EQ(
            field.MeanCurvatureProperty[vertex],
            0.5 * (maxPrincipal + minPrincipal));
        EXPECT_DOUBLE_EQ(
            field.GaussianCurvatureProperty[vertex],
            maxPrincipal * minPrincipal);
        if (maxPrincipal != 0.0 || minPrincipal != 0.0)
        {
            ++nonZero;
            EXPECT_GE(
                maxPrincipal, field.Diagnostics.MinimumPrincipalValue);
            EXPECT_LE(
                maxPrincipal, field.Diagnostics.MaximumPrincipalValue);
            EXPECT_GE(
                minPrincipal, field.Diagnostics.MinimumPrincipalValue);
            EXPECT_LE(
                minPrincipal, field.Diagnostics.MaximumPrincipalValue);
        }
        else
        {
            sawUnsupported = true;
        }
    }
    EXPECT_EQ(nonZero, field.Diagnostics.NonZeroPrincipalVertexCount);
    EXPECT_TRUE(sawUnsupported)
        << "fixture should contain an unsupported corner vertex";
}
