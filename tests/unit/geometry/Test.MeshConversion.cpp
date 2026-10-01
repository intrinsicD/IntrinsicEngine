#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
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

// GEOM-012 Slice E: the To* conversion seam produces an owning, independent
// result. Move-assigning that result into a caller-owned container is the
// ownership-transfer pattern; the converted mesh outlives the soup source.
TEST(MeshConversion, ConvertedHalfedgeMeshOutlivesSourceViaMoveOwnershipTransfer)
{
    Geometry::HalfedgeMesh::Mesh converted; // caller-owned result container
    {
        const IndexedMesh soup = MakeTriangleSoup();
        auto result = Geometry::Mesh::Conversion::ToHalfedgeMesh(soup);
        ASSERT_TRUE(result.Succeeded());
        converted = std::move(result.Mesh); // move ownership transfer into the result container
    } // soup + intermediate conversion result destroyed

    EXPECT_EQ(converted.VertexCount(), 3u);
    EXPECT_EQ(converted.FaceCount(), 1u);
    EXPECT_EQ(converted.Position(Geometry::VertexHandle{0u}), (glm::vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(converted.Position(Geometry::VertexHandle{1u}), (glm::vec3{1.0f, 0.0f, 0.0f}));
}

TEST(MeshConversion, MeshReservePreallocatesConstructionArraysWithoutChangingCounts)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    mesh.Reserve(128u, 192u, 64u);
    const auto points = mesh.VertexProperties().Get<glm::vec3>("v:point");
    const auto vertices = mesh.VertexProperties().Get<Geometry::HalfedgeMesh::VertexConnectivity>("v:connectivity");
    const auto halfedges = mesh.HalfedgeProperties().Get<Geometry::HalfedgeMesh::HalfedgeConnectivity>("h:connectivity");
    const auto halfedgeFaces = mesh.HalfedgeProperties().Get<Geometry::HalfedgeMesh::HalfedgeFaceConnectivity>("h:face");
    const auto faces = mesh.FaceProperties().Get<Geometry::HalfedgeMesh::FaceConnectivity>("f:connectivity");
    EXPECT_GE(points.Vector().capacity(), 128u);
    EXPECT_GE(vertices.Vector().capacity(), 128u);
    EXPECT_GE(halfedges.Vector().capacity(), 384u);
    EXPECT_GE(halfedgeFaces.Vector().capacity(), 384u);
    EXPECT_GE(faces.Vector().capacity(), 64u);
    EXPECT_GE(mesh.VertexProperties().Get<bool>("v:deleted").Vector().capacity(), 128u);
    EXPECT_GE(mesh.EdgeProperties().Get<bool>("e:deleted").Vector().capacity(), 192u);
    EXPECT_GE(mesh.FaceProperties().Get<bool>("f:deleted").Vector().capacity(), 64u);
    EXPECT_EQ(mesh.VerticesSize(), 0u);
    EXPECT_EQ(mesh.EdgesSize(), 0u);
    EXPECT_EQ(mesh.FacesSize(), 0u);
    const auto* data = points.Vector().data();
    for (unsigned i = 0; i < 128u; ++i)
        (void)mesh.AddVertex({static_cast<float>(i), 0.0f, 0.0f});
    EXPECT_EQ(points.Vector().data(), data);
}
