#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;

namespace
{
    using Geometry::Mesh::Conversion::ConversionDiagnosticKind;
    using Geometry::MeshSoup::IndexedMesh;
    using Geometry::MeshSoup::ValidationDiagnosticKind;

    void AddVertex(IndexedMesh& mesh, glm::vec3 position)
    {
        static_cast<void>(mesh.AddVertex(position));
    }

    void AddTriangle(IndexedMesh& mesh, Geometry::MeshSoup::Index v0, Geometry::MeshSoup::Index v1, Geometry::MeshSoup::Index v2)
    {
        static_cast<void>(mesh.AddTriangle(v0, v1, v2));
    }

    [[nodiscard]] IndexedMesh MakeTriangleSoup()
    {
        IndexedMesh mesh;
        AddVertex(mesh, {0.0f, 0.0f, 0.0f});
        AddVertex(mesh, {1.0f, 0.0f, 0.0f});
        AddVertex(mesh, {0.0f, 1.0f, 0.0f});
        AddTriangle(mesh, 0u, 1u, 2u);
        return mesh;
    }
}

TEST(MeshConversion, ConvertsValidSoupToHalfedgeMeshWithFailureDiagnostics)
{
    const IndexedMesh soup = MakeTriangleSoup();

    const auto converted = Geometry::Mesh::Conversion::ToHalfedgeMesh(soup);

    ASSERT_TRUE(converted.Succeeded());
    EXPECT_TRUE(converted.Diagnostics.empty());
    EXPECT_EQ(converted.Mesh.VertexCount(), 3u);
    EXPECT_EQ(converted.Mesh.FaceCount(), 1u);
    EXPECT_EQ(converted.Mesh.Position(Geometry::VertexHandle{1u}), (glm::vec3{1.0f, 0.0f, 0.0f}));

    IndexedMesh invalid = MakeTriangleSoup();
    AddTriangle(invalid, 0u, 2u, 99u);

    const auto rejected = Geometry::Mesh::Conversion::ToHalfedgeMesh(invalid);

    ASSERT_FALSE(rejected.Succeeded());
    ASSERT_FALSE(rejected.Diagnostics.empty());
    EXPECT_TRUE(std::ranges::any_of(rejected.Diagnostics, [](const Geometry::Mesh::Conversion::ConversionDiagnostic& diagnostic) {
        return diagnostic.Kind == ConversionDiagnosticKind::ValidationDiagnostic &&
               diagnostic.ValidationKind == ValidationDiagnosticKind::InvalidIndex;
    }));
}

TEST(MeshConversion, ConvertsHalfedgeMeshToIndexedSoupAndBack)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    const auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh.AddQuad(v0, v1, v2, v3).has_value());

    const auto soupResult = Geometry::Mesh::Conversion::ToIndexedMesh(mesh);

    ASSERT_TRUE(soupResult.Succeeded());
    EXPECT_EQ(soupResult.Mesh.VertexCount(), 4u);
    EXPECT_EQ(soupResult.Mesh.FaceCount(), 1u);
    ASSERT_EQ(soupResult.Mesh.Face(0u).Indices.size(), 4u);
    EXPECT_FALSE(Geometry::MeshSoup::Validate(soupResult.Mesh).HasErrors());

    const auto roundTrip = Geometry::Mesh::Conversion::ToHalfedgeMesh(soupResult.Mesh);
    ASSERT_TRUE(roundTrip.Succeeded());
    EXPECT_EQ(roundTrip.Mesh.VertexCount(), 4u);
    EXPECT_EQ(roundTrip.Mesh.FaceCount(), 1u);
}
