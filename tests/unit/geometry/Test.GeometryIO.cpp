#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <span>
#include <sstream>
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


