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
import Geometry.HalfedgeMesh.CurvatureSegmentation.Features;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;

namespace
{
    using Geometry::FaceHandle;
    using Geometry::PropertyIndex;
    using Geometry::VertexHandle;
    namespace Curv = Geometry::Curvature;
    namespace Patches = Geometry::CurvatureSegmentation;
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

    std::filesystem::path TestDataPath(const char* filename)
    {
        return std::filesystem::path(__FILE__)
                   .parent_path()
                   .parent_path()
                   .parent_path()
            / "data" / filename;
    }

    std::optional<Geometry::HalfedgeMesh::Mesh> LoadTriangleFixture(
        const char* filename)
    {
        const auto payload = Geometry::MeshIO::LoadOBJ(
            TestDataPath(filename).string());
        if (!payload)
            return std::nullopt;
        const auto positions = payload->Vertices.Get<glm::vec3>("v:point");
        const auto faces =
            payload->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!positions.IsValid() || !faces.IsValid())
            return std::nullopt;

        std::vector<std::uint32_t> indices;
        indices.reserve(faces.Vector().size() * 3u);
        for (const std::vector<std::uint32_t>& face : faces.Vector())
        {
            if (face.size() != 3u)
                return std::nullopt;
            indices.insert(indices.end(), face.begin(), face.end());
        }
        return MU::BuildHalfedgeMeshFromIndexedTriangles(
            positions.Vector(), indices);
    }

    template <std::size_t N>
    void ExpectFramework24PrincipalReference(
        Geometry::HalfedgeMesh::Mesh& mesh,
        const std::array<double, N>& expectedMinimum,
        const std::array<double, N>& expectedMaximum)
    {
        const auto result = Curv::ComputeCurvatureTensor(mesh);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(mesh.VerticesSize(), N);
        EXPECT_EQ(result->Diagnostics.SupportedVertexCount, N);
        EXPECT_EQ(result->Diagnostics.NonZeroPrincipalVertexCount, N);
        for (std::size_t i = 0u; i < N; ++i)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(i)};
            EXPECT_NEAR(
                result->MinPrincipalCurvatureProperty[vertex],
                expectedMinimum[i],
                2.0e-12)
                << "minimum at vertex " << i;
            EXPECT_NEAR(
                result->MaxPrincipalCurvatureProperty[vertex],
                expectedMaximum[i],
                2.0e-12)
                << "maximum at vertex " << i;
        }
    }

    std::uint64_t QuantizedFramework24FieldHash(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const Curv::CurvatureField& field)
    {
        constexpr double quantization = 100'000.0;
        constexpr std::uint64_t offsetBasis = 14'695'981'039'346'656'037ull;
        constexpr std::uint64_t prime = 1'099'511'628'211ull;
        std::uint64_t hash = offsetBasis;
        for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(i)};
            for (const double value : {
                     field.MinPrincipalCurvatureProperty[vertex],
                     field.MaxPrincipalCurvatureProperty[vertex]})
            {
                std::uint64_t bits = static_cast<std::uint64_t>(
                    std::llround(value * quantization));
                for (int byte = 0; byte < 8; ++byte)
                {
                    hash ^= bits & 0xffu;
                    hash *= prime;
                    bits >>= 8u;
                }
            }
        }
        return hash;
    }
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
// Sphere: the corrected Framework24 mixed area produces approximately 1/R on
// acute triangulations while preserving isotropy and tangent directions.
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
        EXPECT_NEAR(std::abs(k1), 1.0 / R, 0.08);
        EXPECT_NEAR(std::abs(k2), 1.0 / R, 0.08);
        EXPECT_LT(std::abs(std::abs(k1) - std::abs(k2)), 0.08);

        // Orthonormal + tangent.
        const glm::vec3 n = MU::VertexNormal(mesh, vh);
        EXPECT_NEAR(glm::length(d1), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::length(d2), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(d1, d2), 0.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(d1, glm::normalize(n)), 0.0f, 1e-2f);
        EXPECT_NEAR(glm::dot(d2, glm::normalize(n)), 0.0f, 1e-2f);
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

TEST(CurvatureTensor, ReversingOrientationSwapsAnisotropicScalarPairs)
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
}

TEST(CurvatureTensor, MatchesFramework24SequentialAcuteClosedReference)
{
    auto mesh = LoadTriangleFixture("framework24-acute-tetrahedron.obj");
    ASSERT_TRUE(mesh.has_value());
    constexpr std::array expectedMinimum{
        0.78001275240210033, 0.78001275240210033,
        0.78001275240210033, 0.78001275240210033};
    constexpr std::array expectedMaximum{
        3.1200510096084009, 3.1200510096084009,
        3.1200510096084009, 3.1200510096084009};
    ExpectFramework24PrincipalReference(
        *mesh, expectedMinimum, expectedMaximum);
}

TEST(CurvatureTensor, MatchesFramework24SequentialObtuseClosedReference)
{
    auto mesh = LoadTriangleFixture("framework24-obtuse-tetrahedron.obj");
    ASSERT_TRUE(mesh.has_value());
    constexpr std::array expectedMinimum{
        -6.1810779792105457, -10.012749350060913,
        -2.3846744578378214, -10.6600725275584};
    constexpr std::array expectedMaximum{
        -3.4382492274809735, -2.4416027945661374,
        -1.0771084777901436, -2.0751173149857816};
    ExpectFramework24PrincipalReference(
        *mesh, expectedMinimum, expectedMaximum);
}

TEST(CurvatureTensor, MatchesFramework24SequentialOpenBoundaryReference)
{
    auto mesh = LoadTriangleFixture("framework24-open-patch.obj");
    ASSERT_TRUE(mesh.has_value());
    constexpr std::array expectedMinimum{
        1.4548394143473505e-19, -0.49363388794616025,
        -0.48175306589211192, 0.0,
        -0.55741139968573661, 0.0,
        -0.48175306589211192, -0.49363388794616025,
        1.4548394143473505e-19};
    constexpr std::array expectedMaximum{
        0.48185460157763876, 0.0, 0.0,
        0.24775096602326011, 0.31158967040402408,
        0.24775096602326011, 0.0, 0.0,
        0.48185460157763876};
    ExpectFramework24PrincipalReference(
        *mesh, expectedMinimum, expectedMaximum);

    const auto result = Curv::ComputeCurvatureTensor(*mesh);
    ASSERT_TRUE(result.has_value());
    const VertexHandle center{4u};
    const glm::vec3 frameworkMaximumDirection{
        0.95751961172477684f, 0.28836815559702916f, 0.0f};
    const glm::vec3 frameworkMinimumDirection{
        -0.28836815559702916f, 0.95751961172477684f, 0.0f};
    EXPECT_GT(std::abs(glm::dot(
        result->PrincipalDir1Property[center],
        frameworkMaximumDirection)), 0.999999f);
    EXPECT_GT(std::abs(glm::dot(
        result->PrincipalDir2Property[center],
        frameworkMinimumDirection)), 0.999999f);
}

// =============================================================================
// Framework24 publishes tensor eigenvectors without complementary pairing. On
// a cylinder its nonzero edge-tangent mode follows the axis while the scalar
// has the corrected approximately 1/R normalization.
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
            const bool firstIsZero = std::abs(k1) < std::abs(k2);
            const glm::vec3 zeroDirection = firstIsZero ? d1 : d2;
            const glm::vec3 bendingDirection = firstIsZero ? d2 : d1;
            const double zeroValue = firstIsZero ? k1 : k2;
            const double bendingValue = firstIsZero ? k2 : k1;

            EXPECT_NEAR(std::abs(zeroValue), 0.0, 0.15);
            EXPECT_NEAR(std::abs(bendingValue), 1.0 / R, 0.2);
            EXPECT_LT(std::abs(glm::dot(zeroDirection, axis)), 0.1f);
            EXPECT_GT(std::abs(glm::dot(bendingDirection, axis)), 0.9f);
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
// Saddle z = x² − y²: Framework24's corrected signed tensor follows the graph
// normal convention and publishes stable axis-aligned directions at the origin.
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
// Framework24's eigensolver publishes a finite orthonormal basis even when a
// flat tensor has three equal zero eigenvalues.
// =============================================================================

TEST(CurvatureTensor, FlatRegionPublishesFiniteFramework24Basis)
{
    const double a = 1.0;
    const int cells = 10;
    auto mesh = MakeHeightGrid(a, cells, [](double, double) { return 0.0; }); // planar
    auto result = Curv::ComputeCurvatureTensor(mesh);
    ASSERT_TRUE(result.has_value());

    const int n = cells + 1;
    // Interior and boundary vertices both take the direct tensor path.
    VertexHandle interior{static_cast<PropertyIndex>((cells / 2) * n + (cells / 2))};
    VertexHandle corner{static_cast<PropertyIndex>(0)};
    ASSERT_TRUE(mesh.IsBoundary(corner));
    for (const VertexHandle vertex : {interior, corner})
    {
        const glm::vec3 direction1 = result->PrincipalDir1Property[vertex];
        const glm::vec3 direction2 = result->PrincipalDir2Property[vertex];
        EXPECT_NEAR(glm::length(direction1), 1.0f, 1.0e-6f);
        EXPECT_NEAR(glm::length(direction2), 1.0f, 1.0e-6f);
        EXPECT_NEAR(glm::dot(direction1, direction2), 0.0f, 1.0e-6f);
        EXPECT_DOUBLE_EQ(
            result->MinPrincipalCurvatureProperty[vertex], 0.0);
        EXPECT_DOUBLE_EQ(
            result->MaxPrincipalCurvatureProperty[vertex], 0.0);
    }

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

TEST(CurvatureTensor, CreaseFlanksKeepGenuineCurvature)
{
    // A tent ridge with parabolic flanks. The independent Meyer cotan operator
    // catches artificial cancellation introduced by wider support or scalar
    // post-smoothing.
    constexpr int cells = 20;
    auto mesh = MakeHeightGrid(
        1.0,
        cells,
        [](double x, double) { return -0.6 * std::abs(x) + 0.55 * x * x; });

    auto meyer = Curv::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(meyer.has_value());
    const std::vector<double> meyerMean = meyer->Property.Vector();
    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);

    constexpr int rowWidth = cells + 1;
    int probed = 0;
    for (int y = 7; y <= 13; ++y)
    {
        for (const int x : {12, 13})
        {
            const VertexHandle vertex{
                static_cast<PropertyIndex>(y * rowWidth + x)};
            ASSERT_FALSE(mesh.IsBoundary(vertex));
            const double tensorMean = field.MeanCurvatureProperty[vertex];
            const double referenceMean = meyerMean[vertex.Index];
            ASSERT_GT(std::abs(referenceMean), 0.2);
            EXPECT_GT(tensorMean * referenceMean, 0.0)
                << "sign flip at (" << x << ", " << y << ")";
            EXPECT_GE(std::abs(tensorMean), 0.3 * std::abs(referenceMean))
                << "curvature lost at (" << x << ", " << y << ")";
            ++probed;
        }
    }
    EXPECT_EQ(probed, 14);
}

// The full sculpt field is checked through a quantized hash plus readable
// anchors. Both were generated by Framework24 revision 6dd50a82 using
// CurvatureTaubin(mesh, 0, false, Policy::Sequential) on the identical OBJ.
TEST(CurvatureTensor, SculptAssetMatchesFramework24SequentialReference)
{
    auto built = LoadTriangleFixture("sculpt.obj");
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

    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);
    EXPECT_EQ(field.Diagnostics.SupportedVertexCount, 3669u);
    EXPECT_EQ(field.Diagnostics.NonZeroPrincipalVertexCount, 3669u);
    EXPECT_EQ(
        QuantizedFramework24FieldHash(mesh, field),
        0xfc090818c136a6e2ull);

    struct Anchor
    {
        std::size_t Index;
        double Minimum;
        double Maximum;
    };
    constexpr std::array anchors{
        Anchor{0u, -1.1309506518419563, 47.629353972839404},
        Anchor{4u, -1.2745543828717363, 75.265942565283126},
        Anchor{511u, 1.9095566040154699, 2.0814124836958148},
        Anchor{1000u, 1.9913375118628998, 1.9971206015746612},
        Anchor{2048u, -4.0273646171654249, 0.080819169051565512},
        Anchor{2901u, -3.3026408414958044, -3.0920399983311264},
        Anchor{3668u, -4.0106676790063247, 0.025921025178834916}};
    for (const Anchor& anchor : anchors)
    {
        const VertexHandle vertex{
            static_cast<PropertyIndex>(anchor.Index)};
        EXPECT_NEAR(
            field.MinPrincipalCurvatureProperty[vertex],
            anchor.Minimum,
            2.0e-12);
        EXPECT_NEAR(
            field.MaxPrincipalCurvatureProperty[vertex],
            anchor.Maximum,
            2.0e-12);
    }
}

TEST(CurvatureTensor, SculptAssetHasNoZeroCurvatureBands)
{
    auto built = LoadTriangleFixture("sculpt.obj");
    ASSERT_TRUE(built.has_value());
    Geometry::HalfedgeMesh::Mesh& mesh = *built;

    auto meyer = Curv::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(meyer.has_value());
    const std::vector<double> meyerMean = meyer->Property.Vector();
    const Curv::CurvatureField field = Curv::ComputeCurvature(mesh);

    const auto medianAbsolute = [](std::vector<double> values)
    {
        for (double& value : values)
            value = std::abs(value);
        const std::size_t middle = values.size() / 2u;
        std::nth_element(values.begin(), values.begin() + middle, values.end());
        return values[middle];
    };
    const std::vector<double> tensorMean =
        field.MeanCurvatureProperty.Vector();
    const double tensorScale = medianAbsolute(tensorMean);
    const double meyerScale = medianAbsolute(meyerMean);
    ASSERT_GT(tensorScale, 0.0);
    ASSERT_GT(meyerScale, 0.0);

    std::size_t zeroBand = 0u;
    std::size_t signFlips = 0u;
    for (std::size_t i = 0u; i < tensorMean.size(); ++i)
    {
        const bool referenceCurved =
            std::abs(meyerMean[i]) > 0.5 * meyerScale;
        if (referenceCurved
            && std::abs(tensorMean[i]) < 0.05 * tensorScale)
        {
            ++zeroBand;
        }
        if (referenceCurved
            && std::abs(tensorMean[i]) > 0.1 * tensorScale
            && tensorMean[i] * meyerMean[i] < 0.0)
        {
            ++signFlips;
        }
    }
    EXPECT_EQ(zeroBand, 0u);
    EXPECT_EQ(signFlips, 0u);
}

TEST(CurvatureTensor, SculptAssetProducesStableFeatureAlignedParts)
{
    auto built = LoadTriangleFixture("sculpt.obj");
    ASSERT_TRUE(built.has_value());
    Geometry::HalfedgeMesh::Mesh& mesh = *built;
    const Curv::CurvatureField curvature = Curv::ComputeCurvature(mesh);
    const Patches::FeatureEvidenceResult evidence =
        Patches::DetectFeatureEvidence(
            mesh,
            curvature.MaxPrincipalCurvatureProperty.Vector(),
            curvature.MinPrincipalCurvatureProperty.Vector());
    ASSERT_TRUE(evidence.Succeeded())
        << Patches::ToString(evidence.Diagnostics.Status);

    EXPECT_EQ(evidence.Diagnostics.HardFeatureEdgeCount, 384u);
    EXPECT_EQ(evidence.Diagnostics.RetainedSoftEdgeCount, 808u);

    Patches::CurvaturePatchParams params{};
    params.Mixture.SelectionMode =
        Patches::ComponentSelectionMode::FixedCount;
    params.Mixture.FixedComponentCount = 6u;
    params.PatchComplexityCost = 0.5;
    const Patches::CurvaturePatchResult result =
        Patches::SegmentFeatureAlignedPatches(
            mesh,
            curvature.MaxPrincipalCurvatureProperty.Vector(),
            curvature.MinPrincipalCurvatureProperty.Vector(),
            evidence.View(),
            params);
    ASSERT_TRUE(result.Succeeded())
        << Patches::ToString(result.Diagnostics.Status);
    EXPECT_EQ(result.Diagnostics.SelectedComponentCount, 6u);
    EXPECT_EQ(result.Diagnostics.FinalRegionCount, 8u);
    EXPECT_EQ(result.Diagnostics.FinalBoundaryEdgeCount, 620u);
    EXPECT_EQ(result.Diagnostics.HardBoundaryEdgeCount, 384u);
    EXPECT_EQ(result.Diagnostics.SoftBoundaryEdgeCount, 236u);
    EXPECT_EQ(result.Diagnostics.ClosureBoundaryEdgeCount, 0u);
    EXPECT_EQ(result.Diagnostics.FinalNegativeMergeCount, 0u);

    std::vector<std::size_t> regionSizes;
    for (const Patches::CurvaturePatchRegionDiagnostics& region :
         result.Regions)
    {
        regionSizes.push_back(region.FaceCount);
    }
    std::sort(regionSizes.begin(), regionSizes.end());
    EXPECT_EQ(
        regionSizes,
        (std::vector<std::size_t>{
            160u, 160u, 168u, 628u, 632u, 758u, 1488u, 3348u}));

    for (const Geometry::EdgeHandle edge : mesh.LiveEdges())
    {
        if (evidence.HardEdgeMask[edge.Index] != 0u)
        {
            EXPECT_EQ(result.EdgeBoundaries[edge.Index], 1u);
            EXPECT_EQ(
                result.EdgeBoundaryRoles[edge.Index],
                Patches::PatchBoundaryRole::HardFeature);
        }
        if (result.EdgeBoundaries[edge.Index] == 0u)
            continue;
        EXPECT_TRUE(evidence.HardEdgeMask[edge.Index] != 0u
                    || evidence.SoftEdgeConfidence[edge.Index] > 0.0);
        EXPECT_NE(
            result.EdgeBoundaryRoles[edge.Index],
            Patches::PatchBoundaryRole::CurvatureClosure);
    }

    std::vector<std::uint32_t> perturbedSeeds;
    perturbedSeeds.reserve(result.SeedFaceSlots.size());
    for (const std::uint32_t seedSlot : result.SeedFaceSlots)
    {
        const Geometry::FaceHandle seed{seedSlot};
        std::uint32_t replacement = seedSlot;
        for (const Geometry::HalfedgeHandle halfedge :
             mesh.HalfedgesAroundFace(seed))
        {
            const Geometry::FaceHandle neighbor =
                mesh.Face(mesh.OppositeHalfedge(halfedge));
            if (neighbor.IsValid())
            {
                replacement = neighbor.Index;
                break;
            }
        }
        perturbedSeeds.push_back(replacement);
    }
    std::sort(perturbedSeeds.begin(), perturbedSeeds.end());
    perturbedSeeds.erase(
        std::unique(perturbedSeeds.begin(), perturbedSeeds.end()),
        perturbedSeeds.end());
    const Patches::CurvaturePatchResult perturbed =
        Patches::SegmentFeatureAlignedPatches(
            mesh,
            curvature.MaxPrincipalCurvatureProperty.Vector(),
            curvature.MinPrincipalCurvatureProperty.Vector(),
            evidence.View(),
            params,
            {perturbedSeeds, true});
    ASSERT_TRUE(perturbed.Succeeded())
        << Patches::ToString(perturbed.Diagnostics.Status);
    EXPECT_EQ(perturbed.Diagnostics.FinalRegionCount, 8u);
    EXPECT_EQ(result.EdgeBoundaries, perturbed.EdgeBoundaries);
    EXPECT_EQ(result.EdgeBoundaryRoles, perturbed.EdgeBoundaryRoles);
}

// =============================================================================
// Published H and K equal the principal invariants at every vertex, including
// Framework24's directly evaluated open-boundary tensors.
// =============================================================================

TEST(CurvatureTensor, OpenMeshFullFieldKeepsPrincipalInvariants)
{
    // Uniform diagonals exercise corners that the former interpolation path
    // left unsupported. Framework24 evaluates them directly.
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
    }
    EXPECT_EQ(nonZero, field.Diagnostics.NonZeroPrincipalVertexCount);
    EXPECT_EQ(field.Diagnostics.SupportedVertexCount, mesh.VertexCount());
    EXPECT_EQ(nonZero, 23u);
}
