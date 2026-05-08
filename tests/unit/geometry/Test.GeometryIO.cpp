#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include <glm/glm.hpp>

import Core;
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

TEST(GeometryIO_MeshIO, LoadsCOFFTriangleWithVertexColors)
{
    TempFile file(".off",
                  "COFF\n"
                  "3 1 0\n"
                  "0 0 0 255 0 0\n"
                  "1 0 0 0 255 0\n"
                  "0 1 0 0 0 255\n"
                  "3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);

    auto colors = result->Vertices.Get<glm::vec4>("v:color");
    ASSERT_TRUE(colors.IsValid());
    ASSERT_EQ(colors.Vector().size(), 3u);
    EXPECT_EQ(colors[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(colors[1], glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(colors[2], glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

TEST(GeometryIO_MeshIO, LoadsCOFFAlphaTokenIsConsumedNotStored)
{
    TempFile file(".off",
                  "COFF\n"
                  "3 1 0\n"
                  "0 0 0 255 0 0 128\n"
                  "1 0 0 0 255 0 64\n"
                  "0 1 0 0 0 255 32\n"
                  "3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);

    auto colors = result->Vertices.Get<glm::vec4>("v:color");
    ASSERT_TRUE(colors.IsValid());
    ASSERT_EQ(colors.Vector().size(), 3u);
    EXPECT_EQ(colors[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(colors[1], glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(colors[2], glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

TEST(GeometryIO_MeshIO, LoadsNOFFTriangleWithVertexNormals)
{
    TempFile file(".off",
                  "NOFF\n"
                  "3 1 0\n"
                  "0 0 0 0 0 1\n"
                  "1 0 0 0 0 1\n"
                  "0 1 0 0 0 1\n"
                  "3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);

    auto normals = result->Vertices.Get<glm::vec3>("v:normal");
    ASSERT_TRUE(normals.IsValid());
    ASSERT_EQ(normals.Vector().size(), 3u);
    EXPECT_EQ(normals[0], glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(normals[1], glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(normals[2], glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(GeometryIO_MeshIO, LoadsCNOFFTriangleWithNormalsAndColors)
{
    TempFile file(".off",
                  "CNOFF\n"
                  "3 1 0\n"
                  "0 0 0 0 0 1 255 0 0\n"
                  "1 0 0 0 0 1 0 255 0\n"
                  "0 1 0 0 0 1 0 0 255\n"
                  "3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);

    auto normals = result->Vertices.Get<glm::vec3>("v:normal");
    ASSERT_TRUE(normals.IsValid());
    ASSERT_EQ(normals.Vector().size(), 3u);
    EXPECT_EQ(normals[0], glm::vec3(0.0f, 0.0f, 1.0f));

    auto colors = result->Vertices.Get<glm::vec4>("v:color");
    ASSERT_TRUE(colors.IsValid());
    ASSERT_EQ(colors.Vector().size(), 3u);
    EXPECT_EQ(colors[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(colors[1], glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(colors[2], glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

TEST(GeometryIO_MeshIO, LoadOFFSkipsDegenerateFaceRows)
{
    // The face counts header declares two face rows so the loop
    // iterates both; the first is degenerate (count < 3) and must be
    // soft-skipped without aborting the load.
    TempFile file(".off",
                  "OFF\n"
                  "3 2 0\n"
                  "0 0 0\n"
                  "1 0 0\n"
                  "0 1 0\n"
                  "2 0 1\n"
                  "3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Vertices.Size(), 3u);
    auto faceVertices = result->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(faceVertices.IsValid());
    ASSERT_EQ(faceVertices.Vector().size(), 1u);
    ASSERT_EQ(faceVertices[0].size(), 3u);
    EXPECT_EQ(faceVertices[0][0], 0u);
    EXPECT_EQ(faceVertices[0][1], 1u);
    EXPECT_EQ(faceVertices[0][2], 2u);
}

TEST(GeometryIO_MeshIO, LoadOFFRejectsAllDegenerateFaces)
{
    // Every declared face row is degenerate (count < 3); the loader
    // soft-skips each row but must still reject the load because no
    // usable face topology was populated.
    TempFile file(".off",
                  "OFF\n"
                  "3 1 0\n"
                  "0 0 0\n"
                  "1 0 0\n"
                  "0 1 0\n"
                  "2 0 1\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_MeshIO, LoadOFFRejectsUnknownMagic)
{
    TempFile file(".off", "FOO\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_MeshIO, LoadOFFRejectsOutOfRangeVertexIndex)
{
    TempFile file(".off", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 5\n");

    const auto result = Geometry::MeshIO::LoadOFF(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
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

TEST(GeometryIO_PointCloudIO, LoadsXYZRGBTrailingColor)
{
    // .xyzrgb: 7+ tokens with RGB carried in the trailing three positions
    // (here: x y z nx ny nz r g b -> 9 tokens).
    TempFile file(".xyzrgb",
                  "0 0 0 0 1 0 255 0 0\n"
                  "1 0 0 0 1 0 0 255 0\n");

    const auto result = Geometry::PointCloudIO::LoadXYZ(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Color(Geometry::VertexHandle{0}), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(result->Cloud.Color(Geometry::VertexHandle{1}), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsXYZSemicolonDelimited)
{
    TempFile file(".xyz",
                  "1.0;2.0;3.0;255;0;0\n"
                  "4.0;5.0;6.0;0;255;0\n");

    const auto result = Geometry::PointCloudIO::LoadXYZ(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(result->Cloud.Color(Geometry::VertexHandle{0}), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(result->Cloud.Color(Geometry::VertexHandle{1}), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsXYZSkipsScanLineMarkers)
{
    TempFile file(".xyz",
                  "LH001\n"
                  "0 0 0\n"
                  "LH42\n"
                  "1 2 3\n"
                  "LH9\n");

    const auto result = Geometry::PointCloudIO::LoadXYZ(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_FALSE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsXYZSoftSkipsMalformedRows)
{
    TempFile file(".xyz",
                  "0 0 0\n"
                  "garbage row\n"
                  "1 nan-token 2\n"
                  "1 2 3\n");

    const auto result = Geometry::PointCloudIO::LoadXYZ(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(GeometryIO_PointCloudIO, LoadXYZRejectsAllMalformedInput)
{
    TempFile file(".xyz",
                  "# only comments and scan-line markers\n"
                  "LH001\n"
                  "LH42\n");

    const auto result = Geometry::PointCloudIO::LoadXYZ(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
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

namespace
{
    void PopulateTriangleMesh(Geometry::MeshIO::MeshIOResult& mesh,
                              std::span<const glm::vec3> positions,
                              std::span<const std::vector<std::uint32_t>> faces,
                              std::span<const glm::vec3> normals = {})
    {
        mesh.Vertices.Resize(positions.size());
        auto pointProperty = mesh.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3(0.0f));
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            pointProperty[i] = positions[i];
        }
        if (!normals.empty())
        {
            auto normalProperty = mesh.Vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3(0.0f, 1.0f, 0.0f));
            for (std::size_t i = 0; i < normals.size(); ++i)
            {
                normalProperty[i] = normals[i];
            }
        }

        mesh.Faces.Resize(faces.size());
        auto faceProperty = mesh.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices", {});
        for (std::size_t i = 0; i < faces.size(); ++i)
        {
            faceProperty[i] = faces[i];
        }
    }

    [[nodiscard]] std::string ReadFileContents(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
}

TEST(GeometryIO_MeshIO, WritesOBJTriangle)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".obj", "");
    const auto status = Geometry::MeshIO::WriteOBJ(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const auto loaded = Geometry::MeshIO::LoadOBJ(file.Path);
    ASSERT_TRUE(loaded.has_value());
    ExpectTriangleMeshProperties(*loaded);
}

TEST(GeometryIO_MeshIO, WritesOBJTriangleWithNormals)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<glm::vec3, 3> normals{
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces, normals);

    TempFile file(".obj", "");
    const auto status = Geometry::MeshIO::WriteOBJ(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const std::string contents = ReadFileContents(file.Path);
    EXPECT_NE(contents.find("vn 0.000000 0.000000 1.000000\n"), std::string::npos);
    EXPECT_NE(contents.find("f 1//1 2//2 3//3\n"), std::string::npos);

    const auto loaded = Geometry::MeshIO::LoadOBJ(file.Path);
    ASSERT_TRUE(loaded.has_value());
    ExpectTriangleMeshProperties(*loaded);
}

TEST(GeometryIO_MeshIO, WritesOBJQuadRoundTripsFaceArity)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 4> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u, 3u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".obj", "");
    const auto status = Geometry::MeshIO::WriteOBJ(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const auto loaded = Geometry::MeshIO::LoadOBJ(file.Path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->Vertices.Size(), 4u);
    EXPECT_EQ(loaded->Faces.Size(), 1u);

    auto faceVertices = loaded->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(faceVertices.IsValid());
    ASSERT_EQ(faceVertices.Vector().size(), 1u);
    EXPECT_EQ(faceVertices.Vector()[0].size(), 4u);
    EXPECT_EQ(faceVertices.Vector()[0][3], 3u);
}

TEST(GeometryIO_MeshIO, WriteOBJRejectsEmptyMesh)
{
    Geometry::MeshIO::MeshIOResult mesh;
    TempFile file(".obj", "");
    EXPECT_EQ(Geometry::MeshIO::WriteOBJ(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::EmptyMesh);
}

TEST(GeometryIO_MeshIO, WriteOBJRejectsOutOfRangeIndex)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 99u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".obj", "");
    EXPECT_EQ(Geometry::MeshIO::WriteOBJ(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidFace);
}

TEST(GeometryIO_MeshIO, WriteOBJRejectsBadPath)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    const std::string path =
        std::string("/this/directory/does/not/exist/intrinsic_geometry_io_") +
        std::to_string(static_cast<long long>(getpid())) + ".obj";

    EXPECT_EQ(Geometry::MeshIO::WriteOBJ(path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidPath);
}

TEST(GeometryIO_MeshIO, WritesPLYTriangle)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".ply", "");
    const auto status = Geometry::MeshIO::WritePLY(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const auto loaded = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(loaded.has_value());
    ExpectTriangleMeshProperties(*loaded);
}

TEST(GeometryIO_MeshIO, WritesPLYTriangleWithNormals)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<glm::vec3, 3> normals{
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces, normals);

    TempFile file(".ply", "");
    const auto status = Geometry::MeshIO::WritePLY(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const std::string contents = ReadFileContents(file.Path);
    EXPECT_NE(contents.find("property float nx\n"), std::string::npos);
    EXPECT_NE(contents.find("property float ny\n"), std::string::npos);
    EXPECT_NE(contents.find("property float nz\n"), std::string::npos);
    EXPECT_NE(contents.find("0.000000 0.000000 0.000000 0.000000 0.000000 1.000000\n"),
              std::string::npos);

    const auto loaded = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(loaded.has_value());
    ExpectTriangleMeshProperties(*loaded);
}

TEST(GeometryIO_MeshIO, WritesPLYQuadRoundTripsFaceArity)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 4> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u, 3u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".ply", "");
    const auto status = Geometry::MeshIO::WritePLY(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const auto loaded = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->Vertices.Size(), 4u);
    EXPECT_EQ(loaded->Faces.Size(), 1u);

    auto faceVertices = loaded->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(faceVertices.IsValid());
    ASSERT_EQ(faceVertices.Vector().size(), 1u);
    EXPECT_EQ(faceVertices.Vector()[0].size(), 4u);
    EXPECT_EQ(faceVertices.Vector()[0][3], 3u);
}

TEST(GeometryIO_MeshIO, WritePLYRejectsEmptyMesh)
{
    Geometry::MeshIO::MeshIOResult mesh;
    TempFile file(".ply", "");
    EXPECT_EQ(Geometry::MeshIO::WritePLY(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::EmptyMesh);
}

TEST(GeometryIO_MeshIO, WritePLYRejectsOutOfRangeIndex)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 99u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".ply", "");
    EXPECT_EQ(Geometry::MeshIO::WritePLY(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidFace);
}

TEST(GeometryIO_MeshIO, WritePLYRejectsBadPath)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    const std::string path =
        std::string("/this/directory/does/not/exist/intrinsic_geometry_io_") +
        std::to_string(static_cast<long long>(getpid())) + ".ply";

    EXPECT_EQ(Geometry::MeshIO::WritePLY(path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidPath);
}

namespace
{
    [[nodiscard]] std::string WriteBinarySTLFixture(std::span<const std::array<glm::vec3, 3>> triangles,
                                                   std::uint32_t advertisedTriCount)
    {
        static int counter = 0;
        const char* tmpDir = std::getenv("TEST_TMPDIR");
        if (tmpDir == nullptr || tmpDir[0] == '\0')
        {
            tmpDir = "/tmp";
        }
        std::string path = std::string(tmpDir) + "/intrinsic_geometry_io_binstl_" +
                           std::to_string(static_cast<long long>(getpid())) + "_" +
                           std::to_string(counter++) + ".stl";
        std::ofstream out(path, std::ios::binary);

        std::array<char, 80> header{};
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        out.write(reinterpret_cast<const char*>(&advertisedTriCount), sizeof(advertisedTriCount));

        for (const auto& tri : triangles)
        {
            const float zeroNormal[3] = {0.0f, 0.0f, 0.0f};
            out.write(reinterpret_cast<const char*>(zeroNormal), sizeof(zeroNormal));
            for (const auto& v : tri)
            {
                const float xyz[3] = {v.x, v.y, v.z};
                out.write(reinterpret_cast<const char*>(xyz), sizeof(xyz));
            }
            const std::uint16_t attributeByteCount = 0;
            out.write(reinterpret_cast<const char*>(&attributeByteCount), sizeof(attributeByteCount));
        }

        return path;
    }

    struct TempBinarySTL
    {
        std::string Path;

        TempBinarySTL(std::span<const std::array<glm::vec3, 3>> triangles,
                      std::uint32_t advertisedTriCount)
            : Path(WriteBinarySTLFixture(triangles, advertisedTriCount))
        {
        }

        ~TempBinarySTL()
        {
            if (!Path.empty())
            {
                std::remove(Path.c_str());
            }
        }
    };
}

TEST(GeometryIO_MeshIO, LoadsBinarySTLSingleTriangle)
{
    const std::array<std::array<glm::vec3, 3>, 1> triangles{{
        {glm::vec3{0.0f, 0.0f, 0.0f},
         glm::vec3{1.0f, 0.0f, 0.0f},
         glm::vec3{0.0f, 1.0f, 0.0f}},
    }};
    TempBinarySTL file(triangles, 1u);

    const auto result = Geometry::MeshIO::LoadSTL(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadsBinarySTLTwoTriangles)
{
    const std::array<std::array<glm::vec3, 3>, 2> triangles{{
        {glm::vec3{0.0f, 0.0f, 0.0f},
         glm::vec3{1.0f, 0.0f, 0.0f},
         glm::vec3{0.0f, 1.0f, 0.0f}},
        {glm::vec3{1.0f, 0.0f, 0.0f},
         glm::vec3{1.0f, 1.0f, 0.0f},
         glm::vec3{0.0f, 1.0f, 0.0f}},
    }};
    TempBinarySTL file(triangles, 2u);

    const auto result = Geometry::MeshIO::LoadSTL(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Vertices.Size(), 6u);
    EXPECT_EQ(result->Faces.Size(), 2u);

    auto positions = result->Vertices.Get<glm::vec3>("v:point");
    ASSERT_TRUE(positions.IsValid());
    ASSERT_EQ(positions.Vector().size(), 6u);
    EXPECT_EQ(positions[3], glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(positions[5], glm::vec3(0.0f, 1.0f, 0.0f));

    auto faceVertices = result->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(faceVertices.IsValid());
    ASSERT_EQ(faceVertices.Vector().size(), 2u);
    ASSERT_EQ(faceVertices[1].size(), 3u);
    EXPECT_EQ(faceVertices[1][0], 3u);
    EXPECT_EQ(faceVertices[1][1], 4u);
    EXPECT_EQ(faceVertices[1][2], 5u);
}

TEST(GeometryIO_MeshIO, LoadSTLRejectsTruncatedBinaryPayload)
{
    const std::array<std::array<glm::vec3, 3>, 1> triangles{{
        {glm::vec3{0.0f, 0.0f, 0.0f},
         glm::vec3{1.0f, 0.0f, 0.0f},
         glm::vec3{0.0f, 1.0f, 0.0f}},
    }};
    // Write only one triangle but advertise two: 80 + 4 + 50 = 134 bytes,
    // size-match for triCount=2 would require 184 bytes, so this disqualifies
    // the size-match branch and the ASCII fallback (no "facet" / "solid"
    // tokens in zeroed header), forcing ParseBinarySTL to detect the
    // shortage.
    TempBinarySTL file(triangles, 2u);

    const auto result = Geometry::MeshIO::LoadSTL(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_MeshIO, LoadsASCIISTLAfterBinaryDispatch)
{
    // Regression: the ASCII fallback inside IsBinarySTL must classify a
    // canonical ASCII STL as ASCII even after the binary code path was
    // introduced, so the existing ASCII parser still runs.
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

TEST(GeometryIO_MeshIO, WritesSTLTriangle)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".stl", "");
    const auto status = Geometry::MeshIO::WriteSTL(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const auto loaded = Geometry::MeshIO::LoadSTL(file.Path);
    ASSERT_TRUE(loaded.has_value());
    ExpectTriangleMeshProperties(*loaded);
}

TEST(GeometryIO_MeshIO, WritesSTLTriangleEmitsFacetNormal)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".stl", "");
    const auto status = Geometry::MeshIO::WriteSTL(file.Path, mesh);
    EXPECT_EQ(status, Geometry::MeshIO::MeshIOWriteStatus::Success);

    const std::string contents = ReadFileContents(file.Path);
    EXPECT_NE(contents.find("solid IntrinsicEngine\n"), std::string::npos);
    EXPECT_NE(contents.find("endsolid IntrinsicEngine\n"), std::string::npos);
    EXPECT_NE(contents.find("    outer loop\n"), std::string::npos);
    EXPECT_NE(contents.find("    endloop\n"), std::string::npos);
    EXPECT_NE(contents.find("  facet normal 0.000000e+00 0.000000e+00 1.000000e+00\n"),
              std::string::npos);
}

TEST(GeometryIO_MeshIO, WriteSTLRejectsQuadFace)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 4> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u, 3u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".stl", "");
    EXPECT_EQ(Geometry::MeshIO::WriteSTL(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidFace);
}

TEST(GeometryIO_MeshIO, WriteSTLRejectsEmptyMesh)
{
    Geometry::MeshIO::MeshIOResult mesh;
    TempFile file(".stl", "");
    EXPECT_EQ(Geometry::MeshIO::WriteSTL(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::EmptyMesh);
}

TEST(GeometryIO_MeshIO, WriteSTLRejectsOutOfRangeIndex)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 99u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    TempFile file(".stl", "");
    EXPECT_EQ(Geometry::MeshIO::WriteSTL(file.Path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidFace);
}

TEST(GeometryIO_MeshIO, WriteSTLRejectsBadPath)
{
    Geometry::MeshIO::MeshIOResult mesh;
    const std::array<glm::vec3, 3> positions{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    PopulateTriangleMesh(mesh, positions, faces);

    const std::string path =
        std::string("/this/directory/does/not/exist/intrinsic_geometry_io_") +
        std::to_string(static_cast<long long>(getpid())) + ".stl";

    EXPECT_EQ(Geometry::MeshIO::WriteSTL(path, mesh),
              Geometry::MeshIO::MeshIOWriteStatus::InvalidPath);
}

namespace
{
    enum class BinaryPlyEndian
    {
        Little,
        Big,
    };

    [[nodiscard]] std::array<std::byte, 4> EncodeFloat(float value, BinaryPlyEndian endian)
    {
        std::array<std::byte, 4> bytes{};
        std::memcpy(bytes.data(), &value, 4);
        if (endian == BinaryPlyEndian::Big)
        {
            std::swap(bytes[0], bytes[3]);
            std::swap(bytes[1], bytes[2]);
        }
        return bytes;
    }

    [[nodiscard]] std::array<std::byte, 4> EncodeUint32(std::uint32_t value, BinaryPlyEndian endian)
    {
        std::array<std::byte, 4> bytes{};
        std::memcpy(bytes.data(), &value, 4);
        if (endian == BinaryPlyEndian::Big)
        {
            std::swap(bytes[0], bytes[3]);
            std::swap(bytes[1], bytes[2]);
        }
        return bytes;
    }

    struct BinaryPlyVertex
    {
        glm::vec3 Position{0.0f};
        bool HasColor = false;
        std::uint8_t R = 0;
        std::uint8_t G = 0;
        std::uint8_t B = 0;
    };

    [[nodiscard]] std::string WriteBinaryPLYFixture(std::span<const BinaryPlyVertex> vertices,
                                                    std::span<const std::vector<std::uint32_t>> faces,
                                                    BinaryPlyEndian endian,
                                                    std::uint32_t advertisedFaceListCountOverride = 0,
                                                    bool truncateBody = false)
    {
        static int counter = 0;
        const char* tmpDir = std::getenv("TEST_TMPDIR");
        if (tmpDir == nullptr || tmpDir[0] == '\0')
        {
            tmpDir = "/tmp";
        }
        std::string path = std::string(tmpDir) + "/intrinsic_geometry_io_binply_" +
                           std::to_string(static_cast<long long>(getpid())) + "_" +
                           std::to_string(counter++) + ".ply";
        std::ofstream out(path, std::ios::binary);

        const bool hasColor = !vertices.empty() && vertices.front().HasColor;
        const char* formatToken =
            endian == BinaryPlyEndian::Little ? "binary_little_endian" : "binary_big_endian";

        out << "ply\n";
        out << "format " << formatToken << " 1.0\n";
        out << "comment Test fixture\n";
        out << "element vertex " << vertices.size() << "\n";
        out << "property float x\n";
        out << "property float y\n";
        out << "property float z\n";
        if (hasColor)
        {
            out << "property uchar red\n";
            out << "property uchar green\n";
            out << "property uchar blue\n";
        }
        out << "element face " << faces.size() << "\n";
        out << "property list uchar int vertex_indices\n";
        out << "end_header\n";

        std::size_t vertexBytesWritten = 0;
        for (const auto& v : vertices)
        {
            const auto x = EncodeFloat(v.Position.x, endian);
            const auto y = EncodeFloat(v.Position.y, endian);
            const auto z = EncodeFloat(v.Position.z, endian);
            out.write(reinterpret_cast<const char*>(x.data()), 4);
            out.write(reinterpret_cast<const char*>(y.data()), 4);
            out.write(reinterpret_cast<const char*>(z.data()), 4);
            vertexBytesWritten += 12;
            if (hasColor)
            {
                const std::uint8_t rgb[3] = {v.R, v.G, v.B};
                out.write(reinterpret_cast<const char*>(rgb), 3);
                vertexBytesWritten += 3;
            }
            if (truncateBody)
            {
                return path;
            }
        }
        (void)vertexBytesWritten;

        for (const auto& face : faces)
        {
            const std::uint32_t reportedCount =
                advertisedFaceListCountOverride > 0 ? advertisedFaceListCountOverride
                                                    : static_cast<std::uint32_t>(face.size());
            const std::uint8_t countByte = static_cast<std::uint8_t>(reportedCount);
            out.write(reinterpret_cast<const char*>(&countByte), 1);
            for (const auto idx : face)
            {
                const auto bytes = EncodeUint32(idx, endian);
                out.write(reinterpret_cast<const char*>(bytes.data()), 4);
            }
        }

        return path;
    }

    struct TempBinaryPLY
    {
        std::string Path;

        TempBinaryPLY(std::span<const BinaryPlyVertex> vertices,
                      std::span<const std::vector<std::uint32_t>> faces,
                      BinaryPlyEndian endian,
                      std::uint32_t advertisedFaceListCountOverride = 0,
                      bool truncateBody = false)
            : Path(WriteBinaryPLYFixture(vertices, faces, endian, advertisedFaceListCountOverride, truncateBody))
        {
        }

        ~TempBinaryPLY()
        {
            if (!Path.empty())
            {
                std::remove(Path.c_str());
            }
        }
    };
}

TEST(GeometryIO_MeshIO, LoadsBinaryLittleEndianPLYTriangle)
{
    const std::array<BinaryPlyVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{0.0f, 1.0f, 0.0f}, false, 0, 0, 0},
    }};
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    TempBinaryPLY file(vertices, faces, BinaryPlyEndian::Little);

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadsBinaryBigEndianPLYTriangle)
{
    const std::array<BinaryPlyVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{0.0f, 1.0f, 0.0f}, false, 0, 0, 0},
    }};
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    TempBinaryPLY file(vertices, faces, BinaryPlyEndian::Big);

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadsBinaryLittleEndianPLYQuad)
{
    const std::array<BinaryPlyVertex, 4> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{1.0f, 1.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{0.0f, 1.0f, 0.0f}, false, 0, 0, 0},
    }};
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u, 3u}}};
    TempBinaryPLY file(vertices, faces, BinaryPlyEndian::Little);

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Vertices.Size(), 4u);
    EXPECT_EQ(result->Faces.Size(), 1u);

    auto faceVertices = result->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(faceVertices.IsValid());
    ASSERT_EQ(faceVertices.Vector().size(), 1u);
    ASSERT_EQ(faceVertices[0].size(), 4u);
    EXPECT_EQ(faceVertices[0][3], 3u);
}

TEST(GeometryIO_MeshIO, LoadsBinaryLittleEndianPLYWithExtraVertexProperties)
{
    const std::array<BinaryPlyVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, true, 255, 0, 0},
        {glm::vec3{1.0f, 0.0f, 0.0f}, true, 0, 255, 0},
        {glm::vec3{0.0f, 1.0f, 0.0f}, true, 0, 0, 255},
    }};
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    TempBinaryPLY file(vertices, faces, BinaryPlyEndian::Little);

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    ExpectTriangleMeshProperties(*result);
}

TEST(GeometryIO_MeshIO, LoadPLYRejectsTruncatedBinaryBody)
{
    const std::array<BinaryPlyVertex, 2> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, 0, 0, 0},
    }};
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 0u}}};
    // Header advertises vertex 2 / face 1, but the writer stops after the
    // first vertex's bytes -> remaining body is too small for the second
    // vertex and the face list.
    TempBinaryPLY file(vertices, faces, BinaryPlyEndian::Little, 0, /*truncateBody=*/true);

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_MeshIO, LoadPLYRejectsBinaryFaceListBelowThree)
{
    const std::array<BinaryPlyVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, 0, 0, 0},
        {glm::vec3{0.0f, 1.0f, 0.0f}, false, 0, 0, 0},
    }};
    // Real face list has three indices, but advertise count==2 so the
    // reader sees a degenerate list. The trailing index byte is consumed
    // as padding by the size check; the importer must reject the count.
    const std::array<std::vector<std::uint32_t>, 1> faces{{{0u, 1u, 2u}}};
    TempBinaryPLY file(vertices, faces, BinaryPlyEndian::Little, /*advertisedFaceListCountOverride=*/2u);

    const auto result = Geometry::MeshIO::LoadPLY(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_MeshIO, LoadsAsciiPLYAfterBinaryDispatch)
{
    // Regression: the ASCII path must continue to parse the canonical
    // ASCII fixture after LoadPLY was refactored into a header-driven
    // dispatch supporting binary little/big-endian formats.
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

namespace
{
    struct BinaryPlyPointCloudVertex
    {
        glm::vec3 Position{0.0f};
        bool HasNormal = false;
        glm::vec3 Normal{0.0f};
        bool HasColor = false;
        std::uint8_t R = 0;
        std::uint8_t G = 0;
        std::uint8_t B = 0;
        bool HasIntensity = false;
        float Intensity = 0.0f;
    };

    [[nodiscard]] std::string WriteBinaryPLYPointCloudFixture(
        std::span<const BinaryPlyPointCloudVertex> vertices,
        std::size_t advertisedVertexCountOverride,
        BinaryPlyEndian endian,
        bool injectListPropertyInVertex = false,
        bool truncateBodyAfterFirst = false)
    {
        static int counter = 0;
        const char* tmpDir = std::getenv("TEST_TMPDIR");
        if (tmpDir == nullptr || tmpDir[0] == '\0')
        {
            tmpDir = "/tmp";
        }
        std::string path = std::string(tmpDir) + "/intrinsic_geometry_io_binplypc_" +
                           std::to_string(static_cast<long long>(getpid())) + "_" +
                           std::to_string(counter++) + ".ply";
        std::ofstream out(path, std::ios::binary);

        const bool hasNormals = !vertices.empty() && vertices.front().HasNormal;
        const bool hasIntensity = !vertices.empty() && vertices.front().HasIntensity;
        const bool hasColors = !vertices.empty() && vertices.front().HasColor;
        const char* formatToken =
            endian == BinaryPlyEndian::Little ? "binary_little_endian" : "binary_big_endian";

        const std::size_t advertisedCount =
            advertisedVertexCountOverride > 0 ? advertisedVertexCountOverride : vertices.size();

        out << "ply\n";
        out << "format " << formatToken << " 1.0\n";
        out << "comment Test fixture\n";
        out << "element vertex " << advertisedCount << "\n";
        out << "property float x\n";
        out << "property float y\n";
        out << "property float z\n";
        if (hasNormals)
        {
            out << "property float nx\n";
            out << "property float ny\n";
            out << "property float nz\n";
        }
        if (hasIntensity)
        {
            out << "property float intensity\n";
        }
        if (hasColors)
        {
            out << "property uchar red\n";
            out << "property uchar green\n";
            out << "property uchar blue\n";
        }
        if (injectListPropertyInVertex)
        {
            out << "property list uchar int unsupported\n";
        }
        out << "end_header\n";

        for (const auto& v : vertices)
        {
            const auto x = EncodeFloat(v.Position.x, endian);
            const auto y = EncodeFloat(v.Position.y, endian);
            const auto z = EncodeFloat(v.Position.z, endian);
            out.write(reinterpret_cast<const char*>(x.data()), 4);
            out.write(reinterpret_cast<const char*>(y.data()), 4);
            out.write(reinterpret_cast<const char*>(z.data()), 4);
            if (hasNormals)
            {
                const auto nx = EncodeFloat(v.Normal.x, endian);
                const auto ny = EncodeFloat(v.Normal.y, endian);
                const auto nz = EncodeFloat(v.Normal.z, endian);
                out.write(reinterpret_cast<const char*>(nx.data()), 4);
                out.write(reinterpret_cast<const char*>(ny.data()), 4);
                out.write(reinterpret_cast<const char*>(nz.data()), 4);
            }
            if (hasIntensity)
            {
                const auto intensity = EncodeFloat(v.Intensity, endian);
                out.write(reinterpret_cast<const char*>(intensity.data()), 4);
            }
            if (hasColors)
            {
                const std::uint8_t rgb[3] = {v.R, v.G, v.B};
                out.write(reinterpret_cast<const char*>(rgb), 3);
            }
            if (truncateBodyAfterFirst)
            {
                return path;
            }
        }

        return path;
    }

    struct TempBinaryPLYPointCloud
    {
        std::string Path;

        TempBinaryPLYPointCloud(std::span<const BinaryPlyPointCloudVertex> vertices,
                                BinaryPlyEndian endian,
                                std::size_t advertisedVertexCountOverride = 0,
                                bool injectListPropertyInVertex = false,
                                bool truncateBodyAfterFirst = false)
            : Path(WriteBinaryPLYPointCloudFixture(vertices,
                                                   advertisedVertexCountOverride,
                                                   endian,
                                                   injectListPropertyInVertex,
                                                   truncateBodyAfterFirst))
        {
        }

        ~TempBinaryPLYPointCloud()
        {
            if (!Path.empty())
            {
                std::remove(Path.c_str());
            }
        }
    };
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryLittleEndianPLYPointCloud)
{
    const std::array<BinaryPlyPointCloudVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 1.0f, 2.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    TempBinaryPLYPointCloud file(vertices, BinaryPlyEndian::Little);

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 3u);
    EXPECT_FALSE(result->Cloud.HasNormals());
    EXPECT_FALSE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{2}), glm::vec3(0.0f, 1.0f, 2.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryBigEndianPLYPointCloud)
{
    const std::array<BinaryPlyPointCloudVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 1.0f, 2.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    TempBinaryPLYPointCloud file(vertices, BinaryPlyEndian::Big);

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 3u);
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{2}), glm::vec3(0.0f, 1.0f, 2.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryPLYPointCloudWithNormalsAndColor)
{
    const std::array<BinaryPlyPointCloudVertex, 1> vertices{{
        {glm::vec3{1.0f, 2.0f, 3.0f}, true, glm::vec3{0.0f, 0.0f, 1.0f}, true, 255, 128, 0, false, 0.0f},
    }};
    TempBinaryPLYPointCloud file(vertices, BinaryPlyEndian::Little);

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 1u);
    EXPECT_TRUE(result->Cloud.HasNormals());
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(result->Cloud.Normal(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).x, 1.0f, 1.0e-6f);
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).y, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).z, 0.0f, 1.0e-6f);
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryPLYPointCloudSkipsExtraScalars)
{
    const std::array<BinaryPlyPointCloudVertex, 2> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, true, 255, 0, 0, true, 0.5f},
        {glm::vec3{1.0f, 2.0f, 3.0f}, false, glm::vec3{0.0f}, true, 0, 255, 0, true, 0.25f},
    }};
    TempBinaryPLYPointCloud file(vertices, BinaryPlyEndian::Little);

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{1}).y, 1.0f, 1.0e-6f);
}

TEST(GeometryIO_PointCloudIO, LoadPLYPointCloudRejectsTruncatedBinaryBody)
{
    const std::array<BinaryPlyPointCloudVertex, 4> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 1.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 0.0f, 1.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    TempBinaryPLYPointCloud file(vertices,
                                 BinaryPlyEndian::Little,
                                 /*advertisedVertexCountOverride=*/0,
                                 /*injectListPropertyInVertex=*/false,
                                 /*truncateBodyAfterFirst=*/true);

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_PointCloudIO, LoadPLYPointCloudRejectsListPropertyInVertex)
{
    const std::array<BinaryPlyPointCloudVertex, 1> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    TempBinaryPLYPointCloud file(vertices,
                                 BinaryPlyEndian::Little,
                                 /*advertisedVertexCountOverride=*/0,
                                 /*injectListPropertyInVertex=*/true);

    const auto result = Geometry::PointCloudIO::LoadPLY(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_PointCloudIO, LoadsAsciiPLYPointCloudAfterBinaryDispatch)
{
    // Regression: the ASCII path must continue to parse the canonical
    // ASCII point-cloud fixture after LoadPLY was refactored into a
    // header-driven dispatch supporting binary little/big-endian formats.
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

namespace
{
    struct BinaryPcdPointCloudVertex
    {
        glm::vec3 Position{0.0f};
        bool HasNormal = false;
        glm::vec3 Normal{0.0f};
        bool HasColor = false;
        std::uint8_t R = 0;
        std::uint8_t G = 0;
        std::uint8_t B = 0;
        bool HasIntensity = false;
        float Intensity = 0.0f;
    };

    struct BinaryPcdFixtureOptions
    {
        std::size_t AdvertisedPointCountOverride = 0;
        bool OmitPointsLine = false;
        std::string DataEncoding = "binary";
        bool TruncateBodyAfterFirst = false;
    };

    [[nodiscard]] std::string WriteBinaryPCDFixture(
        std::span<const BinaryPcdPointCloudVertex> vertices,
        const BinaryPcdFixtureOptions& opts)
    {
        static int counter = 0;
        const char* tmpDir = std::getenv("TEST_TMPDIR");
        if (tmpDir == nullptr || tmpDir[0] == '\0')
        {
            tmpDir = "/tmp";
        }
        std::string path = std::string(tmpDir) + "/intrinsic_geometry_io_binpcd_" +
                           std::to_string(static_cast<long long>(getpid())) + "_" +
                           std::to_string(counter++) + ".pcd";
        std::ofstream out(path, std::ios::binary);

        const bool hasNormals = !vertices.empty() && vertices.front().HasNormal;
        const bool hasIntensity = !vertices.empty() && vertices.front().HasIntensity;
        const bool hasColors = !vertices.empty() && vertices.front().HasColor;

        const std::size_t advertisedCount =
            opts.AdvertisedPointCountOverride > 0 ? opts.AdvertisedPointCountOverride : vertices.size();

        out << "# .PCD v0.7\n";
        out << "FIELDS x y z";
        if (hasNormals) out << " normal_x normal_y normal_z";
        if (hasIntensity) out << " intensity";
        if (hasColors) out << " r g b";
        out << "\n";

        out << "SIZE 4 4 4";
        if (hasNormals) out << " 4 4 4";
        if (hasIntensity) out << " 4";
        if (hasColors) out << " 1 1 1";
        out << "\n";

        out << "TYPE F F F";
        if (hasNormals) out << " F F F";
        if (hasIntensity) out << " F";
        if (hasColors) out << " U U U";
        out << "\n";

        out << "COUNT 1 1 1";
        if (hasNormals) out << " 1 1 1";
        if (hasIntensity) out << " 1";
        if (hasColors) out << " 1 1 1";
        out << "\n";

        out << "WIDTH " << advertisedCount << "\n";
        out << "HEIGHT 1\n";
        if (!opts.OmitPointsLine)
        {
            out << "POINTS " << advertisedCount << "\n";
        }
        out << "DATA " << opts.DataEncoding << "\n";

        auto writeFloat = [&](float value)
        {
            std::array<std::byte, 4> bytes{};
            std::memcpy(bytes.data(), &value, 4);
            out.write(reinterpret_cast<const char*>(bytes.data()), 4);
        };

        for (const auto& v : vertices)
        {
            writeFloat(v.Position.x);
            writeFloat(v.Position.y);
            writeFloat(v.Position.z);
            if (hasNormals)
            {
                writeFloat(v.Normal.x);
                writeFloat(v.Normal.y);
                writeFloat(v.Normal.z);
            }
            if (hasIntensity)
            {
                writeFloat(v.Intensity);
            }
            if (hasColors)
            {
                const std::uint8_t rgb[3] = {v.R, v.G, v.B};
                out.write(reinterpret_cast<const char*>(rgb), 3);
            }
            if (opts.TruncateBodyAfterFirst)
            {
                return path;
            }
        }

        return path;
    }

    struct TempBinaryPCD
    {
        std::string Path;

        TempBinaryPCD(std::span<const BinaryPcdPointCloudVertex> vertices,
                      const BinaryPcdFixtureOptions& opts)
            : Path(WriteBinaryPCDFixture(vertices, opts))
        {
        }

        ~TempBinaryPCD()
        {
            if (!Path.empty())
            {
                std::remove(Path.c_str());
            }
        }
    };
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryPCDPointCloud)
{
    const std::array<BinaryPcdPointCloudVertex, 3> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 1.0f, 2.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    BinaryPcdFixtureOptions opts;
    TempBinaryPCD file(vertices, opts);

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 3u);
    EXPECT_FALSE(result->Cloud.HasNormals());
    EXPECT_FALSE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{2}), glm::vec3(0.0f, 1.0f, 2.0f));
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryPCDPointCloudWithNormalsAndColor)
{
    const std::array<BinaryPcdPointCloudVertex, 1> vertices{{
        {glm::vec3{1.0f, 2.0f, 3.0f}, true, glm::vec3{0.0f, 0.0f, 1.0f}, true, 255, 128, 0, false, 0.0f},
    }};
    BinaryPcdFixtureOptions opts;
    TempBinaryPCD file(vertices, opts);

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 1u);
    EXPECT_TRUE(result->Cloud.HasNormals());
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{0}), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(result->Cloud.Normal(Geometry::VertexHandle{0}), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).x, 1.0f, 1.0e-6f);
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).y, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{0}).z, 0.0f, 1.0e-6f);
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryPCDPointCloudSkipsExtraScalars)
{
    const std::array<BinaryPcdPointCloudVertex, 2> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, true, 255, 0, 0, true, 0.5f},
        {glm::vec3{1.0f, 2.0f, 3.0f}, false, glm::vec3{0.0f}, true, 0, 255, 0, true, 0.25f},
    }};
    BinaryPcdFixtureOptions opts;
    TempBinaryPCD file(vertices, opts);

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_FALSE(result->Cloud.HasNormals());
    EXPECT_TRUE(result->Cloud.HasColors());
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(result->Cloud.Color(Geometry::VertexHandle{1}).y, 1.0f, 1.0e-6f);
}

TEST(GeometryIO_PointCloudIO, LoadsBinaryPCDPointCloudFromWidthHeight)
{
    const std::array<BinaryPcdPointCloudVertex, 2> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{1.0f, 2.0f, 3.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    BinaryPcdFixtureOptions opts;
    opts.OmitPointsLine = true;
    TempBinaryPCD file(vertices, opts);

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Cloud.VerticesSize(), 2u);
    EXPECT_EQ(result->Cloud.Position(Geometry::VertexHandle{1}), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(GeometryIO_PointCloudIO, LoadPCDRejectsTruncatedBinaryBody)
{
    const std::array<BinaryPcdPointCloudVertex, 4> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{1.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 1.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
        {glm::vec3{0.0f, 0.0f, 1.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    BinaryPcdFixtureOptions opts;
    opts.TruncateBodyAfterFirst = true;
    TempBinaryPCD file(vertices, opts);

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_PointCloudIO, LoadPCDRejectsBinaryCompressed)
{
    const std::array<BinaryPcdPointCloudVertex, 1> vertices{{
        {glm::vec3{0.0f, 0.0f, 0.0f}, false, glm::vec3{0.0f}, false, 0, 0, 0, false, 0.0f},
    }};
    BinaryPcdFixtureOptions opts;
    opts.DataEncoding = "binary_compressed";
    TempBinaryPCD file(vertices, opts);

    const auto result = Geometry::PointCloudIO::LoadPCD(file.Path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}

TEST(GeometryIO_PointCloudIO, LoadPCDRejectsZeroSizeField)
{
    static int counter = 0;
    const char* tmpDir = std::getenv("TEST_TMPDIR");
    if (tmpDir == nullptr || tmpDir[0] == '\0') tmpDir = "/tmp";
    std::string path = std::string(tmpDir) + "/intrinsic_geometry_io_binpcd_zerosize_" +
                       std::to_string(static_cast<long long>(getpid())) + "_" +
                       std::to_string(counter++) + ".pcd";
    {
        std::ofstream out(path, std::ios::binary);
        out << "# .PCD v0.7\n"
               "FIELDS x y z\n"
               "SIZE 0 4 4\n"
               "TYPE F F F\n"
               "COUNT 1 1 1\n"
               "WIDTH 1\n"
               "HEIGHT 1\n"
               "POINTS 1\n"
               "DATA binary\n";
        const float zero = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            out.write(reinterpret_cast<const char*>(&zero), 4);
        }
    }

    const auto result = Geometry::PointCloudIO::LoadPCD(path);
    std::remove(path.c_str());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
}
