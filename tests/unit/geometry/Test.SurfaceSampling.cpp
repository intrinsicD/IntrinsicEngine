#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.PointCloud.SurfaceSampling;
import Geometry.Properties;

namespace
{
    namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeSeparatedAreaRatioMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;

        const auto s0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto s1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto s2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddTriangle(s0, s1, s2);

        const auto b0 = mesh.AddVertex({10.0f, 0.0f, 0.0f});
        const auto b1 = mesh.AddVertex({12.0f, 0.0f, 0.0f});
        const auto b2 = mesh.AddVertex({10.0f, 2.0f, 0.0f});
        (void)mesh.AddTriangle(b0, b1, b2);

        return mesh;
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeSingleTriangleMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);
        return mesh;
    }

    void ExpectFiniteUnit(const glm::vec3 normal)
    {
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
        EXPECT_NEAR(glm::length(normal), 1.0f, 1.0e-5f);
    }
}

TEST(SurfaceSampling, SamplesFacesInProportionToSurfaceArea)
{
    const auto mesh = MakeSeparatedAreaRatioMesh();

    SurfaceSampling::Params params;
    params.SampleCount = 5000;
    params.Seed = 12345;

    const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    ASSERT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::Success);
    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Info.AcceptedTriangleCount, 2u);
    EXPECT_EQ(result.Info.WrittenSampleCount, 5000u);
    EXPECT_NEAR(result.Info.TotalSurfaceArea, 2.5, 1.0e-9);

    std::size_t smallTriangleSamples = 0;
    std::size_t largeTriangleSamples = 0;
    for (const auto point : result.Cloud.LivePoints())
    {
        const glm::vec3 p = result.Cloud.Position(point);
        if (p.x < 5.0f)
        {
            ++smallTriangleSamples;
        }
        else
        {
            ++largeTriangleSamples;
        }
    }

    const double smallFraction =
        static_cast<double>(smallTriangleSamples) / static_cast<double>(result.Cloud.VertexCount());
    const double largeFraction =
        static_cast<double>(largeTriangleSamples) / static_cast<double>(result.Cloud.VertexCount());

    EXPECT_NEAR(smallFraction, 0.2, 0.035);
    EXPECT_NEAR(largeFraction, 0.8, 0.035);
}

TEST(SurfaceSampling, DeterministicForFixedSeedAndVariesForDifferentSeed)
{
    const auto mesh = MakeSeparatedAreaRatioMesh();

    SurfaceSampling::Params params;
    params.SampleCount = 64;
    params.Seed = 7;

    const auto a = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    const auto b = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    ASSERT_EQ(a.Status, SurfaceSampling::SurfaceSamplingStatus::Success);
    ASSERT_EQ(b.Status, SurfaceSampling::SurfaceSamplingStatus::Success);
    ASSERT_EQ(a.Cloud.VertexCount(), b.Cloud.VertexCount());

    for (std::size_t i = 0; i < a.Cloud.VertexCount(); ++i)
    {
        const auto handle = Geometry::PointCloud::Cloud::Handle(i);
        EXPECT_EQ(a.Cloud.Position(handle), b.Cloud.Position(handle));
        EXPECT_EQ(a.Cloud.Normal(handle), b.Cloud.Normal(handle));
    }

    params.Seed = 8;
    const auto c = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    ASSERT_EQ(c.Status, SurfaceSampling::SurfaceSamplingStatus::Success);

    bool sawDifferentPosition = false;
    for (std::size_t i = 0; i < a.Cloud.VertexCount(); ++i)
    {
        const auto handle = Geometry::PointCloud::Cloud::Handle(i);
        if (glm::distance(a.Cloud.Position(handle), c.Cloud.Position(handle)) > 1.0e-6f)
        {
            sawDifferentPosition = true;
            break;
        }
    }
    EXPECT_TRUE(sawDifferentPosition);
}

TEST(SurfaceSampling, InterpolatesSourceVertexNormalsIntoPointCloudNormals)
{
    auto mesh = MakeSingleTriangleMesh();
    auto normals = mesh.VertexProperties().GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f});
    normals[0] = {0.0f, 0.0f, 1.0f};
    normals[1] = {0.0f, 0.0f, 1.0f};
    normals[2] = {0.0f, 0.0f, 1.0f};

    SurfaceSampling::Params params;
    params.SampleCount = 16;
    params.Seed = 99;

    const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    ASSERT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::Success);
    EXPECT_TRUE(result.Cloud.HasNormals());
    EXPECT_EQ(result.Info.InterpolatedNormalCount, 16u);
    EXPECT_EQ(result.Info.GeometricNormalCount, 0u);

    for (const auto point : result.Cloud.LivePoints())
    {
        ExpectFiniteUnit(result.Cloud.Normal(point));
        EXPECT_NEAR(result.Cloud.Normal(point).z, 1.0f, 1.0e-5f);
    }
}

TEST(SurfaceSampling, FallsBackToGeometricFaceNormalsWhenNoSourceNormalsExist)
{
    const auto mesh = MakeSingleTriangleMesh();

    SurfaceSampling::Params params;
    params.SampleCount = 8;
    params.Seed = 17;

    const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    ASSERT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::Success);
    EXPECT_TRUE(result.Cloud.HasNormals());
    EXPECT_EQ(result.Info.InterpolatedNormalCount, 0u);
    EXPECT_EQ(result.Info.GeometricNormalCount, 8u);

    for (const auto point : result.Cloud.LivePoints())
    {
        ExpectFiniteUnit(result.Cloud.Normal(point));
        EXPECT_NEAR(result.Cloud.Normal(point).z, 1.0f, 1.0e-5f);
    }
}

TEST(SurfaceSampling, FailsClosedForInvalidSampleCounts)
{
    const auto mesh = MakeSingleTriangleMesh();

    SurfaceSampling::Params params;
    params.SampleCount = 0;
    EXPECT_EQ(SurfaceSampling::SampleTriangleMeshSurface(mesh, params).Status,
              SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount);

    params.SampleCount = -4;
    EXPECT_EQ(SurfaceSampling::SampleTriangleMeshSurface(mesh, params).Status,
              SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount);
}

TEST(SurfaceSampling, FailsClosedForEmptyMesh)
{
    const Geometry::HalfedgeMesh::Mesh mesh;

    SurfaceSampling::Params params;
    params.SampleCount = 4;

    const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
    EXPECT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::EmptyMesh);
    EXPECT_EQ(result.Cloud.VertexCount(), 0u);
}

TEST(SurfaceSampling, ReportsDegenerateNonTriangleAndNonFiniteFaces)
{
    SurfaceSampling::Params params;
    params.SampleCount = 4;

    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);

        const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
        EXPECT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::NoValidTriangles);
        EXPECT_EQ(result.Info.RejectedDegenerateTriangleCount, 1u);
    }

    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
        const auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddQuad(v0, v1, v2, v3);

        const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
        EXPECT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::NoValidTriangles);
        EXPECT_EQ(result.Info.RejectedNonTriangleFaceCount, 1u);
    }

    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({std::numeric_limits<float>::infinity(), 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);

        const auto result = SurfaceSampling::SampleTriangleMeshSurface(mesh, params);
        EXPECT_EQ(result.Status, SurfaceSampling::SurfaceSamplingStatus::NoValidTriangles);
        EXPECT_EQ(result.Info.RejectedNonFiniteTriangleCount, 1u);
    }
}
