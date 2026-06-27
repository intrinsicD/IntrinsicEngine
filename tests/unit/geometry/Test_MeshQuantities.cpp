#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numeric>
#include <vector>
#include <glm/glm.hpp>

import Geometry;

namespace
{
    using Geometry::FaceHandle;
    using Geometry::PropertyIndex;
    namespace MU = Geometry::MeshUtils;

    Geometry::HalfedgeMesh::Mesh MakeSingleTriangle()
    {
        std::vector<glm::vec3> pos{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        std::vector<std::uint32_t> idx{0, 1, 2};
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    Geometry::HalfedgeMesh::Mesh MakeUnitSquare()
    {
        std::vector<glm::vec3> pos{
            {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 3};
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }
}

TEST(GeometryMeshQuantities, SingleTriangleAreaVectorCentroid)
{
    const auto mesh = MakeSingleTriangle();
    const FaceHandle f{0};

    EXPECT_NEAR(MU::FaceArea(mesh, f), 0.5, 1e-9);

    const glm::dvec3 av = MU::FaceAreaVector(mesh, f);
    EXPECT_NEAR(av.x, 0.0, 1e-9);
    EXPECT_NEAR(av.y, 0.0, 1e-9);
    EXPECT_NEAR(std::abs(av.z), 0.5, 1e-9); // +z winding expected, but magnitude is the area
    EXPECT_NEAR(glm::length(av), 0.5, 1e-9);

    const glm::dvec3 c = MU::FaceCentroid(mesh, f);
    EXPECT_NEAR(c.x, 1.0 / 3.0, 1e-9);
    EXPECT_NEAR(c.y, 1.0 / 3.0, 1e-9);
    EXPECT_NEAR(c.z, 0.0, 1e-9);
}

TEST(GeometryMeshQuantities, BarycentricAreasSumToSurfaceArea)
{
    const auto mesh = MakeUnitSquare();

    double totalFaceArea = 0.0;
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        const FaceHandle f{static_cast<PropertyIndex>(fi)};
        if (mesh.IsDeleted(f)) continue;
        totalFaceArea += MU::FaceArea(mesh, f);
    }
    EXPECT_NEAR(totalFaceArea, 1.0, 1e-9);

    const std::vector<double> bary = MU::ComputeBarycentricVertexAreas(mesh);
    const double barySum = std::accumulate(bary.begin(), bary.end(), 0.0);
    // Lumped vertex areas partition the surface: their sum equals total area.
    EXPECT_NEAR(barySum, totalFaceArea, 1e-9);
    EXPECT_EQ(bary.size(), mesh.VerticesSize());
}

TEST(GeometryMeshQuantities, FaceCentroidIsCornerAverageNotOneRing)
{
    const auto mesh = MakeUnitSquare();
    // Face 0 = (0,0,0),(1,0,0),(1,1,0): corner average = (2/3, 1/3, 0).
    const glm::dvec3 c = MU::FaceCentroid(mesh, FaceHandle{0});
    EXPECT_NEAR(c.x, 2.0 / 3.0, 1e-9);
    EXPECT_NEAR(c.y, 1.0 / 3.0, 1e-9);
}

TEST(GeometryMeshQuantities, FailClosedOnInvalidFace)
{
    const auto mesh = MakeSingleTriangle();
    const FaceHandle bogus{static_cast<PropertyIndex>(9999)};
    EXPECT_EQ(MU::FaceArea(mesh, bogus), 0.0);
    EXPECT_EQ(glm::length(MU::FaceAreaVector(mesh, bogus)), 0.0);
    EXPECT_EQ(glm::length(MU::FaceCentroid(mesh, bogus)), 0.0);
}

TEST(GeometryMeshQuantities, NonPlanarQuadAreaUsesTriangleFan)
{
    // A folded quad: corners (0,0,0),(1,0,0),(1,1,1),(0,1,0). The two fan
    // triangles each have a known area; the oriented Newell vector would
    // partially cancel and underreport, so FaceArea must use the fan sum.
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
    const auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    const auto f = mesh.AddQuad(v0, v1, v2, v3);
    ASSERT_TRUE(f.has_value());

    // Fan = tri(v0,v1,v2) + tri(v0,v2,v3).
    const glm::dvec3 p0{0.0, 0.0, 0.0}, p1{1.0, 0.0, 0.0}, p2{1.0, 1.0, 1.0}, p3{0.0, 1.0, 0.0};
    const double expected =
        0.5 * glm::length(glm::cross(p1 - p0, p2 - p0)) +
        0.5 * glm::length(glm::cross(p2 - p0, p3 - p0));

    const double area = MU::FaceArea(mesh, *f);
    EXPECT_NEAR(area, expected, 1e-9);
    // The folded quad's true area must strictly exceed the cancelled Newell magnitude.
    EXPECT_GT(area, glm::length(MU::FaceAreaVector(mesh, *f)) + 1e-6);
}

TEST(GeometryMeshQuantities, FailClosedOnNonFiniteCorner)
{
    // A triangle with a NaN corner must not propagate non-finite values into
    // area / area-vector / centroid / barycentric mass.
    Geometry::HalfedgeMesh::Mesh mesh;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({nan, 1.0f, 0.0f});
    const auto f = mesh.AddTriangle(v0, v1, v2);
    ASSERT_TRUE(f.has_value());

    EXPECT_EQ(MU::FaceArea(mesh, *f), 0.0);
    EXPECT_EQ(glm::length(MU::FaceAreaVector(mesh, *f)), 0.0);
    EXPECT_EQ(glm::length(MU::FaceCentroid(mesh, *f)), 0.0);

    const std::vector<double> bary = MU::ComputeBarycentricVertexAreas(mesh);
    for (const double a : bary)
    {
        EXPECT_TRUE(std::isfinite(a));
    }
}
