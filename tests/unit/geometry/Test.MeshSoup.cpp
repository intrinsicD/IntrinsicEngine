#include <gtest/gtest.h>
#include <limits>

#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Geometry.MeshSoup;
import Geometry.Properties;

namespace
{
    using Geometry::MeshSoup::AttributeDomain;
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

    void AddFace(IndexedMesh& mesh, std::vector<Geometry::MeshSoup::Index> indices)
    {
        static_cast<void>(mesh.AddFace(std::move(indices)));
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

TEST(MeshSoup, EmptyInputIsValid)
{
    const IndexedMesh mesh;

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_FALSE(result.HasErrors());
    EXPECT_TRUE(Geometry::MeshSoup::IsValid(result));
    EXPECT_EQ(mesh.VertexCount(), 0u);
    EXPECT_EQ(mesh.FaceCount(), 0u);
}

TEST(MeshSoup, ValidTriangleSoupAndBorrowedViewAreAccepted)
{
    const IndexedMesh mesh = MakeTriangleSoup();

    const auto view = Geometry::MeshSoup::BorrowView(mesh);
    const auto result = Geometry::MeshSoup::Validate(view);

    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(view.Positions.size(), 3u);
    EXPECT_EQ(view.Faces.size(), 1u);
    EXPECT_EQ(view.Faces[0].Indices.size(), 3u);
    ASSERT_NE(view.VertexProperties, nullptr);
    ASSERT_NE(view.FaceProperties, nullptr);
    ASSERT_NE(view.CornerProperties, nullptr);
    EXPECT_EQ(view.VertexProperties->Size(), 3u);
    EXPECT_EQ(view.FaceProperties->Size(), 1u);
    EXPECT_EQ(view.CornerProperties->Size(), 3u);
}

TEST(MeshSoup, PolygonSoupIsAccepted)
{
    IndexedMesh mesh;
    AddVertex(mesh, {0.0f, 0.0f, 0.0f});
    AddVertex(mesh, {1.0f, 0.0f, 0.0f});
    AddVertex(mesh, {1.0f, 1.0f, 0.0f});
    AddVertex(mesh, {0.0f, 1.0f, 0.0f});
    AddFace(mesh, std::vector<Geometry::MeshSoup::Index>{0u, 1u, 2u, 3u});

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::DegenerateFace), 0u);
}

TEST(MeshSoup, DuplicateVerticesAreWarnings)
{
    IndexedMesh mesh = MakeTriangleSoup();
    AddVertex(mesh, {1.0f, 0.0f, 0.0f});

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::DuplicateVertex), 1u);
}

TEST(MeshSoup, DuplicateVerticesTreatSignedZeroAsEqual)
{
    IndexedMesh mesh;
    AddVertex(mesh, {0.0f, 0.0f, 0.0f});
    AddVertex(mesh, {-0.0f, 0.0f, 0.0f});

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::DuplicateVertex), 1u);
}

TEST(MeshSoup, InvalidIndicesAreErrors)
{
    IndexedMesh mesh = MakeTriangleSoup();
    AddTriangle(mesh, 0u, 2u, 7u);

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::InvalidIndex), 1u);
    EXPECT_EQ(result.Diagnostics.back().FaceIndex, 1u);
    EXPECT_EQ(result.Diagnostics.back().VertexIndex, 7u);
}

TEST(MeshSoup, SmallValidTrianglesRemainValidAtDifferentScales)
{
    for (const float scale : {1.0e-12f, 1.0e-4f, 1.0f, 1.0e12f})
    {
        SCOPED_TRACE(scale);
        IndexedMesh mesh = MakeTriangleSoup();
        for (glm::vec3& position : mesh.Positions())
            position *= scale;
        const auto result = Geometry::MeshSoup::Validate(mesh);
        EXPECT_FALSE(result.HasErrors());
        EXPECT_EQ(result.Count(ValidationDiagnosticKind::DegenerateFace), 0u);
    }
}

TEST(MeshSoup, DegenerateFacesAreErrors)
{
    IndexedMesh duplicateIndex;
    AddVertex(duplicateIndex, {0.0f, 0.0f, 0.0f});
    AddVertex(duplicateIndex, {1.0f, 0.0f, 0.0f});
    AddTriangle(duplicateIndex, 0u, 1u, 1u);

    const auto duplicateResult = Geometry::MeshSoup::Validate(duplicateIndex);
    EXPECT_TRUE(duplicateResult.HasErrors());
    EXPECT_EQ(duplicateResult.Count(ValidationDiagnosticKind::RepeatedFaceIndex), 1u);

    IndexedMesh collinear;
    AddVertex(collinear, {0.0f, 0.0f, 0.0f});
    AddVertex(collinear, {1.0f, 0.0f, 0.0f});
    AddVertex(collinear, {2.0f, 0.0f, 0.0f});
    AddTriangle(collinear, 0u, 1u, 2u);

    const auto collinearResult = Geometry::MeshSoup::Validate(collinear);
    EXPECT_TRUE(collinearResult.HasErrors());
    EXPECT_EQ(collinearResult.Count(ValidationDiagnosticKind::DegenerateFace), 1u);
}

TEST(MeshSoup, NonManifoldEdgesAreErrors)
{
    IndexedMesh mesh;
    AddVertex(mesh, {0.0f, 0.0f, 0.0f});
    AddVertex(mesh, {1.0f, 0.0f, 0.0f});
    AddVertex(mesh, {0.0f, 1.0f, 0.0f});
    AddVertex(mesh, {0.0f, -1.0f, 0.0f});
    AddVertex(mesh, {0.5f, 0.0f, 1.0f});
    AddTriangle(mesh, 0u, 1u, 2u);
    AddTriangle(mesh, 1u, 0u, 3u);
    AddTriangle(mesh, 0u, 1u, 4u);

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::NonManifoldEdge), 1u);
}

TEST(MeshSoup, InconsistentWindingIsReportedForSameDirectedSharedEdge)
{
    IndexedMesh mesh;
    AddVertex(mesh, {0.0f, 0.0f, 0.0f});
    AddVertex(mesh, {1.0f, 0.0f, 0.0f});
    AddVertex(mesh, {0.0f, 1.0f, 0.0f});
    AddVertex(mesh, {0.0f, -1.0f, 0.0f});
    AddTriangle(mesh, 0u, 1u, 2u);
    AddTriangle(mesh, 0u, 1u, 3u);

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::InconsistentWinding), 1u);
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::NonManifoldEdge), 0u);
}

TEST(MeshSoup, PropertySetsStoreVertexFaceAndCornerDomains)
{
    IndexedMesh mesh = MakeTriangleSoup();

    auto vertexColors = mesh.GetOrAddVertexProperty<glm::vec4>("v:color", glm::vec4{1.0f});
    auto faceMaterials = mesh.GetOrAddFaceProperty<std::uint32_t>("f:material", 0u);
    auto cornerTexcoords = mesh.GetOrAddCornerProperty<glm::vec2>("c:uv", glm::vec2{0.0f});

    vertexColors[Geometry::VertexHandle{1u}] = glm::vec4{0.25f, 0.5f, 0.75f, 1.0f};
    faceMaterials[Geometry::FaceHandle{0u}] = 7u;
    cornerTexcoords[2u] = glm::vec2{0.5f, 1.0f};

    const auto result = Geometry::MeshSoup::Validate(mesh);

    EXPECT_FALSE(result.HasErrors());
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:point"));
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:color"));
    EXPECT_TRUE(mesh.FaceProperties().Exists("f:material"));
    EXPECT_TRUE(mesh.CornerProperties().Exists("c:uv"));
    EXPECT_EQ(mesh.VertexProperties().Size(), mesh.VertexCount());
    EXPECT_EQ(mesh.FaceProperties().Size(), mesh.FaceCount());
    EXPECT_EQ(mesh.CornerProperties().Size(), mesh.CornerCount());
    EXPECT_EQ(mesh.GetVertexProperty<glm::vec4>("v:color")[1u], (glm::vec4{0.25f, 0.5f, 0.75f, 1.0f}));
    EXPECT_EQ(mesh.GetFaceProperty<std::uint32_t>("f:material")[0u], 7u);
    EXPECT_EQ(mesh.GetCornerProperty<glm::vec2>("c:uv")[2u], (glm::vec2{0.5f, 1.0f}));
}

TEST(MeshSoup, CopyAndMoveRebindBuiltInPropertyHandles)
{
    IndexedMesh source = MakeTriangleSoup();
    auto sourceColors = source.GetOrAddVertexProperty<glm::vec4>("v:color", glm::vec4{1.0f});
    sourceColors[Geometry::VertexHandle{1u}] = glm::vec4{0.1f, 0.2f, 0.3f, 1.0f};

    IndexedMesh copied = source;
    copied.Position(1u) = glm::vec3{2.0f, 0.0f, 0.0f};

    EXPECT_EQ(source.Position(1u), (glm::vec3{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(copied.Position(1u), (glm::vec3{2.0f, 0.0f, 0.0f}));
    EXPECT_EQ(copied.GetVertexProperty<glm::vec4>("v:color")[1u], (glm::vec4{0.1f, 0.2f, 0.3f, 1.0f}));

    IndexedMesh moved = std::move(copied);
    moved.Position(2u) = glm::vec3{0.0f, 2.0f, 0.0f};

    EXPECT_EQ(moved.Position(2u), (glm::vec3{0.0f, 2.0f, 0.0f}));
    EXPECT_EQ(moved.VertexProperties().Size(), 3u);
    EXPECT_EQ(moved.FaceProperties().Size(), 1u);
    EXPECT_EQ(moved.CornerProperties().Size(), 3u);
    EXPECT_FALSE(Geometry::MeshSoup::Validate(moved).HasErrors());
}

TEST(MeshSoup, AttributeArityMismatchesUsePropertySetDomainCardinality)
{
    IndexedMesh mesh = MakeTriangleSoup();
    auto view = Geometry::MeshSoup::BorrowView(mesh);
    Geometry::PropertySet mismatchedVertices;
    mismatchedVertices.Resize(2u);
    view.VertexProperties = &mismatchedVertices;

    const auto result = Geometry::MeshSoup::Validate(view);

    ASSERT_TRUE(result.HasErrors());
    ASSERT_EQ(result.Count(ValidationDiagnosticKind::AttributeArityMismatch), 1u);
    EXPECT_EQ(result.Diagnostics.back().AttributeName, "vertex properties");
    EXPECT_EQ(result.Diagnostics.back().AttributeDomainValue, AttributeDomain::Vertex);
    EXPECT_EQ(result.Diagnostics.back().ExpectedCount, 3u);
    EXPECT_EQ(result.Diagnostics.back().ActualCount, 2u);
}


TEST(MeshSoup, ValidationContextReusesCompletedChecks)
{
    auto mesh = MakeTriangleSoup();
    AddVertex(mesh, {0.0f, 0.0f, 0.0f});
    Geometry::MeshSoup::ValidationContext context(Geometry::MeshSoup::BorrowView(mesh));
    context.CheckDuplicateVertices();
    context.CheckDuplicateVertices();
    context.CheckTopology();
    context.CheckTopology();
    const auto& result = context.Validate();
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::DuplicateVertex), 1u);
    EXPECT_EQ(result.CornerCount, 3u);
    EXPECT_EQ(result.BoundaryEdgeCount, 3u);
    EXPECT_TRUE(result.StructureChecked);
    EXPECT_TRUE(result.Executed.FaceGeometry);
    EXPECT_TRUE(result.Executed.Attributes);
}

TEST(MeshSoup, SkippingOptionalChecksNeverSkipsStructuralSafety)
{
    auto mesh = MakeTriangleSoup();
    mesh.Position(0).x = std::numeric_limits<float>::infinity();
    mesh.Face(0).Indices[2] = 42u;
    const auto result = Geometry::MeshSoup::Validate(mesh, {.Checks = {false, false, false, false}});
    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::NonFinitePosition), 1u);
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::InvalidIndex), 1u);
    EXPECT_TRUE(result.StructureChecked);
    EXPECT_FALSE(result.Executed.DuplicateVertices);
    EXPECT_FALSE(result.Executed.FaceGeometry);
    EXPECT_FALSE(result.Executed.Topology);
    EXPECT_FALSE(result.Executed.Attributes);
}

TEST(MeshSoup, ZeroAreaAndThinFacesHaveDifferentPolicies)
{
    auto mesh = MakeTriangleSoup();
    mesh.Position(2) = {1.0f, 1.0e-14f, 0.0f};
    const auto thin = Geometry::MeshSoup::Validate(mesh);
    EXPECT_FALSE(thin.HasErrors());
    EXPECT_EQ(thin.Count(ValidationDiagnosticKind::PoorFaceQuality), 1u);
    EXPECT_EQ(thin.Count(ValidationDiagnosticKind::DegenerateFace), 0u);
    mesh.Position(2) = {2.0f, 0.0f, 0.0f};
    EXPECT_TRUE(Geometry::MeshSoup::Validate(mesh).HasErrors());
    const auto render = Geometry::MeshSoup::Validate(mesh, {.AllowZeroAreaFaces = true});
    EXPECT_FALSE(render.HasErrors());
    EXPECT_EQ(render.Count(ValidationDiagnosticKind::DegenerateFace), 1u);
    EXPECT_EQ(render.BoundaryEdgeCount, 3u);
}

TEST(MeshSoup, SmallDetailsDoNotDependOnWholeMeshBounds)
{
    auto mesh = MakeTriangleSoup();
    for (auto& p : mesh.Positions()) p *= 1.0e-8f;
    AddVertex(mesh, {1.0e15f, 0.0f, 0.0f});
    EXPECT_FALSE(Geometry::MeshSoup::Validate(mesh).HasErrors());
}

TEST(MeshSoup, DiagnosticsKeepCountsBeyondRepresentativeSample)
{
    IndexedMesh mesh;
    for (std::size_t i = 0; i < 100; ++i)
        AddVertex(mesh, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    const auto result = Geometry::MeshSoup::Validate(mesh);
    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::NonFinitePosition), 100u);
    EXPECT_EQ(result.Diagnostics.size(), 16u);
}

TEST(MeshSoup, DuplicateFacesAndDegenerateTopologyAreReported)
{
    auto mesh = MakeTriangleSoup();
    mesh.Position(2) = {2.0f, 0.0f, 0.0f};
    AddTriangle(mesh, 0u, 1u, 2u);
    AddTriangle(mesh, 0u, 1u, 2u);
    const auto result = Geometry::MeshSoup::Validate(mesh, {.AllowZeroAreaFaces = true});
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::DuplicateFace), 2u);
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::NonManifoldEdge), 3u);
    EXPECT_EQ(result.Count(ValidationDiagnosticKind::InconsistentWinding), 3u);
}
