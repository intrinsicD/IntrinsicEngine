#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numeric>
#include <vector>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

namespace
{
    using Geometry::FaceHandle;
    using Geometry::PropertyIndex;
    using Geometry::VertexHandle;
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

    void ExpectVecNear(const glm::dvec3 actual, const glm::dvec3 expected, const double epsilon = 1.0e-9)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon);
        EXPECT_NEAR(actual.y, expected.y, epsilon);
        EXPECT_NEAR(actual.z, expected.z, epsilon);
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

TEST(GeometryMeshQuantities, EquilateralTriangleAreaIsAnalytic)
{
    const float height = static_cast<float>(std::sqrt(3.0) * 0.5);
    const std::vector<glm::vec3> pos{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.5f, height, 0.0f},
    };
    const std::vector<std::uint32_t> idx{0, 1, 2};
    const auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
    ASSERT_TRUE(mesh.has_value());

    EXPECT_NEAR(MU::FaceArea(*mesh, FaceHandle{0}), std::sqrt(3.0) * 0.25, 1.0e-8);
}

TEST(GeometryMeshQuantities, ClosedMeshAreaVectorsSumToZero)
{
    const auto mesh = Geometry::HalfedgeMesh::MakeMeshTetrahedron();

    glm::dvec3 sum{0.0};
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        sum += MU::FaceAreaVector(mesh, FaceHandle{static_cast<PropertyIndex>(fi)});
    }

    ExpectVecNear(sum, glm::dvec3{0.0}, 1.0e-9);
}

TEST(GeometryMeshQuantities, PublishCanonicalQuantityProperties)
{
    auto mesh = MakeUnitSquare();

    const auto areas = MU::PublishFaceAreas(mesh);
    const auto areaVectors = MU::PublishFaceAreaVectors(mesh);
    const auto centroids = MU::PublishFaceCentroids(mesh);
    const auto barycentricAreas = MU::PublishBarycentricVertexAreas(mesh);

    ASSERT_TRUE(areas.IsValid());
    ASSERT_TRUE(areaVectors.IsValid());
    ASSERT_TRUE(centroids.IsValid());
    ASSERT_TRUE(barycentricAreas.IsValid());
    EXPECT_TRUE(mesh.FaceProperties().Exists(MU::kFaceAreaPropertyName));
    EXPECT_TRUE(mesh.FaceProperties().Exists(MU::kFaceAreaVectorPropertyName));
    EXPECT_TRUE(mesh.FaceProperties().Exists(MU::kFaceCentroidPropertyName));
    EXPECT_TRUE(mesh.VertexProperties().Exists(MU::kBarycentricVertexAreaPropertyName));

    EXPECT_NEAR(areas[FaceHandle{0}], MU::FaceArea(mesh, FaceHandle{0}), 1.0e-9);
    ExpectVecNear(areaVectors[FaceHandle{0}], MU::FaceAreaVector(mesh, FaceHandle{0}));
    ExpectVecNear(centroids[FaceHandle{0}], MU::FaceCentroid(mesh, FaceHandle{0}));

    const double barySum = std::accumulate(
        barycentricAreas.Vector().begin(), barycentricAreas.Vector().end(), 0.0);
    EXPECT_NEAR(barySum, 1.0, 1.0e-9);
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

TEST(GeometryMeshQuantities, FaceScalarGradientMatchesLinearField)
{
    const auto mesh = MakeSingleTriangle();
    const std::vector<double> u{
        5.0,
        7.0,
        8.0,
    };

    const glm::dvec3 gradient = MU::FaceScalarGradient(mesh, FaceHandle{0}, u);
    ExpectVecNear(gradient, glm::dvec3{2.0, 3.0, 0.0}, 1.0e-7);

    const auto gradients = MU::ComputeFaceScalarGradients(mesh, u);
    ASSERT_EQ(gradients.size(), mesh.FacesSize());
    ExpectVecNear(gradients[0], gradient, 1.0e-12);

    auto mutableMesh = mesh;
    const auto published = MU::PublishFaceScalarGradients(mutableMesh, u);
    ASSERT_TRUE(published.IsValid());
    ExpectVecNear(published[FaceHandle{0}], gradient, 1.0e-12);
}

TEST(GeometryMeshQuantities, FaceScalarGradientFailsClosed)
{
    const auto mesh = MakeSingleTriangle();
    const std::vector<double> tooShort{1.0, 2.0};
    const std::vector<double> nonFinite{0.0, std::numeric_limits<double>::infinity(), 1.0};
    const std::vector<double> valid{0.0, 1.0, 2.0};

    ExpectVecNear(MU::FaceScalarGradient(mesh, FaceHandle{0}, tooShort), glm::dvec3{0.0});
    ExpectVecNear(MU::FaceScalarGradient(mesh, FaceHandle{0}, nonFinite), glm::dvec3{0.0});
    ExpectVecNear(MU::FaceScalarGradient(mesh, FaceHandle{9999}, valid), glm::dvec3{0.0});
}

TEST(GeometryMeshQuantities, ProjectToUnitSphereLeavesOriginAndNormalizesFiniteVertices)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const VertexHandle origin = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const VertexHandle x = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    const VertexHandle diagonal = mesh.AddVertex({1.0f, 2.0f, 2.0f});

    Geometry::HalfedgeMesh::ProjectToUnitSphere(mesh);

    EXPECT_EQ(mesh.Position(origin), (glm::vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_NEAR(glm::length(mesh.Position(x)), 1.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(mesh.Position(diagonal)), 1.0f, 1.0e-6f);
    EXPECT_TRUE(std::isfinite(mesh.Position(origin).x));
    EXPECT_TRUE(std::isfinite(mesh.Position(diagonal).z));
}

TEST(GeometryMeshQuantities, VertexOneRingPcaAlignsWithSphereNormal)
{
    auto mesh = Geometry::HalfedgeMesh::MakeMeshIcosahedron();
    Geometry::HalfedgeMesh::ProjectToUnitSphere(mesh);

    const VertexHandle vertex{0u};
    const Geometry::PCAResult pca = MU::VertexOneRingPCA(mesh, vertex);
    ASSERT_TRUE(pca.Valid);
    const glm::vec3 radial = glm::normalize(mesh.Position(vertex));
    const glm::vec3 smallestEigenvector = glm::normalize(pca.Eigenvectors[2]);
    EXPECT_GT(std::abs(glm::dot(radial, smallestEigenvector)), 0.95f);

    const auto pcaProperty = MU::PublishVertexOneRingPCA(mesh);
    ASSERT_TRUE(pcaProperty.IsValid());
    EXPECT_TRUE(mesh.VertexProperties().Exists(MU::kVertexPcaPropertyName));
    EXPECT_TRUE(pcaProperty[vertex].Valid);
}

TEST(GeometryMeshQuantities, VertexOneRingPcaFailsClosedWhenUnderdetermined)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const VertexHandle v = mesh.AddVertex({0.0f, 0.0f, 0.0f});

    const Geometry::PCAResult pca = MU::VertexOneRingPCA(mesh, v);
    EXPECT_FALSE(pca.Valid);
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

TEST(GeometryMeshQuantities, PlanarConcavePolygonUsesShoelaceArea)
{
    // Concave (reflex at D) planar quad. Its true (shoelace) area is 1.5, but a
    // naive absolute triangle-fan from A overcounts (2.5) because fan triangles
    // span the concavity. FaceArea must report the exact planar area.
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto vA = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto vB = mesh.AddVertex({2.0f, 1.0f, 0.0f});
    const auto vC = mesh.AddVertex({0.0f, 2.0f, 0.0f});
    const auto vD = mesh.AddVertex({0.5f, 1.0f, 0.0f});
    const Geometry::VertexHandle loop[] = {vA, vB, vC, vD};
    const auto f = mesh.AddFace(loop);
    ASSERT_TRUE(f.has_value());

    // Shoelace area of the loop (planar, z = 0).
    const glm::dvec2 p[] = {{0, 0}, {2, 1}, {0, 2}, {0.5, 1}};
    double twice = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const glm::dvec2& a = p[i];
        const glm::dvec2& b = p[(i + 1) % 4];
        twice += a.x * b.y - b.x * a.y;
    }
    const double shoelace = std::abs(twice) * 0.5;
    ASSERT_NEAR(shoelace, 1.5, 1e-12);

    EXPECT_NEAR(MU::FaceArea(mesh, *f), shoelace, 1e-9);
    // Planar area must equal the Newell magnitude exactly (no fan overcount).
    EXPECT_NEAR(MU::FaceArea(mesh, *f), glm::length(MU::FaceAreaVector(mesh, *f)), 1e-9);
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
