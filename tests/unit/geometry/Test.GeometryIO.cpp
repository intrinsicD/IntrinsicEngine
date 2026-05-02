#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    [[nodiscard]] std::string WriteTempFile(const std::string& extension, const std::string& contents)
    {
        static int counter = 0;
        const char* tmpDir = std::getenv("TEST_TMPDIR");
        if (tmpDir == nullptr || tmpDir[0] == '\0')
        {
            tmpDir = "/tmp";
        }

        std::string path = std::string(tmpDir) + "/intrinsic_geometry_io_" +
                           std::to_string(static_cast<long long>(getpid())) + "_" +
                           std::to_string(counter++) + extension;
        std::ofstream out(path, std::ios::binary);
        out << contents;
        return path;
    }

    struct TempFile
    {
        std::string Path;

        TempFile(std::string extension, std::string contents)
            : Path(WriteTempFile(extension, contents))
        {
        }

        ~TempFile()
        {
            if (!Path.empty())
            {
                std::remove(Path.c_str());
            }
        }
    };

    void ExpectTriangleMeshProperties(const Geometry::MeshIO::MeshIOResult& mesh)
    {
        EXPECT_EQ(mesh.Vertices.Size(), 3u);
        EXPECT_EQ(mesh.Faces.Size(), 1u);

        auto positions = mesh.Vertices.Get<glm::vec3>("v:point");
        ASSERT_TRUE(positions.IsValid());
        ASSERT_EQ(positions.Vector().size(), 3u);
        EXPECT_EQ(positions[0], glm::vec3(0.0f, 0.0f, 0.0f));

        auto faceVertices = mesh.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        ASSERT_TRUE(faceVertices.IsValid());
        ASSERT_EQ(faceVertices.Vector().size(), 1u);
        ASSERT_EQ(faceVertices[0].size(), 3u);
        EXPECT_EQ(faceVertices[0][0], 0u);
        EXPECT_EQ(faceVertices[0][1], 1u);
        EXPECT_EQ(faceVertices[0][2], 2u);
    }
}

TEST(GeometryIO_MeshIO, LoadsOFFTriangle)
{
    TempFile file(".off", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->SourcePath, file.Path);
    EXPECT_FALSE(result->BasePath.empty());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadsOBJTriangle)
{
    TempFile file(".obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    const auto result = Geometry::MeshIO::LoadOBJ(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadsASCIIPLYTriangle)
{
    TempFile file(".ply",
                  "ply\n"
                  "format ascii 1.0\n"
                  "element vertex 3\n"
                  "property float x\n"
                  "property float y\n"
                  "property float z\n"
                  "element face 1\n"
                  "property list uchar int vertex_indices\n"
                  "end_header\n"
                  "0 0 0\n"
                  "1 0 0\n"
                  "0 1 0\n"
                  "3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadsASCIISTLTriangle)
{
    TempFile file(".stl",
                  "solid tri\n"
                  "facet normal 0 0 1\n"
                  "outer loop\n"
                  "vertex 0 0 0\n"
                  "vertex 1 0 0\n"
                  "vertex 0 1 0\n"
                  "endloop\n"
                  "endfacet\n"
                  "endsolid tri\n");

    const auto result = Geometry::MeshIO::LoadSTL(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_PointCloudIO, LoadsXYZWithColor)
{
    TempFile file(".xyz", "2\n0 0 0 255 0 0\n1 0 0 0 255 0\n");

    const auto result = Geometry::PointCloudIO::LoadXYZ(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Color(Geometry::VertexHandle{0}), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsASCIIPCDWithNormalsAndColor)
{
    TempFile file(".pcd",
                  "# .PCD v0.7\n"
                  "FIELDS x y z normal_x normal_y normal_z r g b\n"
                  "SIZE 4 4 4 4 4 4 4 4 4\n"
                  "TYPE F F F F F F F F F\n"
                  "COUNT 1 1 1 1 1 1 1 1 1\n"
                  "WIDTH 1\n"
                  "HEIGHT 1\n"
                  "POINTS 1\n"
                  "DATA ascii\n"
                  "0 1 2 0 0 1 0 0 255\n");

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 1u);
    EXPECT_TRUE(result->Cloud.HasNormals());
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(0.0f, 1.0f, 2.0f));
    EXPECT_EQ(result->Cloud.Normal(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(result->Cloud.Color(Geometry::VertexHandle{0}), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsVertexOnlyASCIIPLY)
{
    TempFile file(".ply",
                  "ply\n"
                  "format ascii 1.0\n"
                  "element vertex 1\n"
                  "property float x\n"
                  "property float y\n"
                  "property float z\n"
                  "property uchar red\n"
                  "property uchar green\n"
                  "property uchar blue\n"
                  "end_header\n"
                  "1 2 3 255 128 0\n");

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 1u);
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).y, 128.0f / 255.0f, 1.0e-6f);
}

TEST(GeometryIO_GraphIO, LoadsTGFWithLabelsAndWeight)
{
    TempFile file(".tgf", "1 0 0 0 first\n2 1 0 0 second\n#\n1 2 2.5 edge label\n");

    const auto result = Geometry::GraphIO::LoadTGF(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Graph.VertexCount(), 2u);
    EXPECT_EQ(result->Graph.EdgeCount(), 1u);
    EXPECT_EQ(result->Graph.VertexPosition(Geometry::VertexHandle{1}), glm::vec3(1.0f, 0.0f, 0.0f));

    auto vertexLabels = result->Graph.VertexProperties().Get<std::string>("v:label");
    ASSERT_TRUE(vertexLabels.IsValid());
    EXPECT_EQ(vertexLabels[0], "first");

    auto edgeWeights = result->Graph.EdgeProperties().Get<float>("e:weight");
    ASSERT_TRUE(edgeWeights.IsValid());
    EXPECT_FLOAT_EQ(edgeWeights[0], 2.5f);

    auto edgeLabels = result->Graph.EdgeProperties().Get<std::string>("e:label");
    ASSERT_TRUE(edgeLabels.IsValid());
    EXPECT_EQ(edgeLabels[0], "edge label");
}

TEST(GeometryIO_GraphIO, LoadsEdgeListWithImplicitVertices)
{
    TempFile file(".edges", "A B 1.5 first\nB C 2.0 second\n");

    const auto result = Geometry::GraphIO::LoadEdgeList(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Graph.VertexCount(), 3u);
    EXPECT_EQ(result->Graph.EdgeCount(), 2u);

    auto vertexIds = result->Graph.VertexProperties().Get<std::string>("v:id");
    ASSERT_TRUE(vertexIds.IsValid());
    EXPECT_EQ(vertexIds[0], "A");
    EXPECT_EQ(vertexIds[2], "C");

    auto edgeWeights = result->Graph.EdgeProperties().Get<float>("e:weight");
    ASSERT_TRUE(edgeWeights.IsValid());
    EXPECT_FLOAT_EQ(edgeWeights[1], 2.0f);
}


