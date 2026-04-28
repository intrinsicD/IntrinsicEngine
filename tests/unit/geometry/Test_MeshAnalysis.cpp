#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

import Geometry;

#include "Test_MeshBuilders.h"

// =============================================================================
// MeshAnalysis Tests
// =============================================================================

TEST(MeshAnalysis, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::MeshAnalysis::Analyze(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(MeshAnalysis, TetrahedronHasNoProblems)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::MeshAnalysis::Analyze(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->IsolatedVertexCount, 0u);
    EXPECT_EQ(result->BoundaryVertexCount, 0u);
    EXPECT_EQ(result->NonManifoldVertexCount, 0u);
    EXPECT_EQ(result->NonFiniteVertexCount, 0u);

    EXPECT_EQ(result->BoundaryEdgeCount, 0u);
    EXPECT_EQ(result->ZeroLengthEdgeCount, 0u);
    EXPECT_EQ(result->NonFiniteEdgeCount, 0u);

    EXPECT_EQ(result->BoundaryHalfedgeCount, 0u);
    EXPECT_EQ(result->BoundaryFaceCount, 0u);
    EXPECT_EQ(result->NonTriangleFaceCount, 0u);
    EXPECT_EQ(result->DegenerateFaceCount, 0u);
    EXPECT_EQ(result->SkinnyFaceCount, 0u);
    EXPECT_EQ(result->NonFiniteFaceCount, 0u);

    EXPECT_TRUE(result->ProblemVertices.empty());
    EXPECT_TRUE(result->ProblemEdges.empty());
    EXPECT_TRUE(result->ProblemHalfedges.empty());
    EXPECT_TRUE(result->ProblemFaces.empty());

    auto vertexProblem = mesh.VertexProperties().Get<bool>(Geometry::MeshAnalysis::kVertexProblemPropertyName);
    auto edgeProblem = mesh.EdgeProperties().Get<bool>(Geometry::MeshAnalysis::kEdgeProblemPropertyName);
    auto halfedgeProblem = mesh.HalfedgeProperties().Get<bool>(Geometry::MeshAnalysis::kHalfedgeProblemPropertyName);
    auto halfedgeMask = mesh.HalfedgeProperties().Get<std::uint32_t>(Geometry::MeshAnalysis::kHalfedgeIssueMaskPropertyName);
    auto faceProblem = mesh.FaceProperties().Get<bool>(Geometry::MeshAnalysis::kFaceProblemPropertyName);
    ASSERT_TRUE(vertexProblem.IsValid());
    ASSERT_TRUE(edgeProblem.IsValid());
    ASSERT_TRUE(halfedgeProblem.IsValid());
    ASSERT_TRUE(halfedgeMask.IsValid());
    ASSERT_TRUE(faceProblem.IsValid());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        EXPECT_FALSE(vertexProblem[i]);
    }
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        EXPECT_FALSE(edgeProblem[i]);
    }
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        EXPECT_FALSE(halfedgeProblem[i]);
        EXPECT_EQ(halfedgeMask[i], 0u);
    }
    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        EXPECT_FALSE(faceProblem[i]);
    }
}

TEST(MeshAnalysis, SingleTriangleMarksBoundaryRegion)
{
    auto mesh = MakeSingleTriangle();
    auto result = Geometry::MeshAnalysis::Analyze(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->BoundaryVertexCount, 3u);
    EXPECT_EQ(result->BoundaryEdgeCount, 3u);
    EXPECT_EQ(result->BoundaryHalfedgeCount, 3u);
    EXPECT_EQ(result->BoundaryFaceCount, 1u);

    EXPECT_EQ(result->ProblemVertices.size(), 3u);
    EXPECT_EQ(result->ProblemEdges.size(), 3u);
    EXPECT_EQ(result->ProblemHalfedges.size(), mesh.HalfedgesSize());
    EXPECT_EQ(result->ProblemFaces.size(), 1u);

    auto vertexProblem = mesh.VertexProperties().Get<bool>(Geometry::MeshAnalysis::kVertexProblemPropertyName);
    auto edgeProblem = mesh.EdgeProperties().Get<bool>(Geometry::MeshAnalysis::kEdgeProblemPropertyName);
    auto halfedgeProblem = mesh.HalfedgeProperties().Get<bool>(Geometry::MeshAnalysis::kHalfedgeProblemPropertyName);
    auto halfedgeMask = mesh.HalfedgeProperties().Get<std::uint32_t>(Geometry::MeshAnalysis::kHalfedgeIssueMaskPropertyName);
    auto faceProblem = mesh.FaceProperties().Get<bool>(Geometry::MeshAnalysis::kFaceProblemPropertyName);
    ASSERT_TRUE(vertexProblem.IsValid());
    ASSERT_TRUE(edgeProblem.IsValid());
    ASSERT_TRUE(halfedgeProblem.IsValid());
    ASSERT_TRUE(halfedgeMask.IsValid());
    ASSERT_TRUE(faceProblem.IsValid());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        EXPECT_TRUE(vertexProblem[i]);
    }
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        EXPECT_TRUE(edgeProblem[i]);
    }
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        EXPECT_TRUE(halfedgeProblem[i]);
        EXPECT_NE(halfedgeMask[i] & Geometry::MeshAnalysis::kHalfedgeIssueBoundary, 0u);
    }

    std::size_t faceProblemHalfedges = 0;
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        if (halfedgeMask[i] & Geometry::MeshAnalysis::kHalfedgeIssueFaceProblem)
        {
            ++faceProblemHalfedges;
        }
    }
    EXPECT_EQ(faceProblemHalfedges, 3u);
    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        EXPECT_TRUE(faceProblem[i]);
    }
}

TEST(MeshAnalysis, DegenerateTriangleIsDetected)
{
    auto mesh = MakeSingleTriangle();
    mesh.Position(Geometry::VertexHandle{2}) = {2.0f, 0.0f, 0.0f};

    auto result = Geometry::MeshAnalysis::Analyze(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->DegenerateFaceCount, 1u);
    EXPECT_EQ(result->ProblemVertices.size(), 3u);
    EXPECT_EQ(result->ProblemEdges.size(), 3u);
    EXPECT_EQ(result->ProblemHalfedges.size(), mesh.HalfedgesSize());
    EXPECT_EQ(result->ProblemFaces.size(), 1u);

    auto faceMask = mesh.FaceProperties().Get<std::uint32_t>(Geometry::MeshAnalysis::kFaceIssueMaskPropertyName);
    auto halfedgeMask = mesh.HalfedgeProperties().Get<std::uint32_t>(Geometry::MeshAnalysis::kHalfedgeIssueMaskPropertyName);
    ASSERT_TRUE(faceMask.IsValid());
    ASSERT_TRUE(halfedgeMask.IsValid());
    EXPECT_NE(faceMask[0], 0u);
    EXPECT_NE(faceMask[0] & Geometry::MeshAnalysis::kFaceIssueDegenerateArea, 0u);
    std::size_t faceProblemHalfedges = 0;
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        if (halfedgeMask[i] & Geometry::MeshAnalysis::kHalfedgeIssueFaceProblem)
        {
            ++faceProblemHalfedges;
        }
    }
    EXPECT_EQ(faceProblemHalfedges, 3u);
}

TEST(MeshAnalysis, SingleQuadFlagsNonTriangleFace)
{
    auto mesh = MakeSingleQuad();
    auto result = Geometry::MeshAnalysis::Analyze(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->NonTriangleFaceCount, 1u);
    EXPECT_EQ(result->BoundaryFaceCount, 1u);
    EXPECT_EQ(result->ProblemFaces.size(), 1u);
    EXPECT_EQ(result->ProblemVertices.size(), 4u);
    EXPECT_EQ(result->ProblemEdges.size(), 4u);
    EXPECT_EQ(result->ProblemHalfedges.size(), mesh.HalfedgesSize());

    auto faceMask = mesh.FaceProperties().Get<std::uint32_t>(Geometry::MeshAnalysis::kFaceIssueMaskPropertyName);
    auto halfedgeMask = mesh.HalfedgeProperties().Get<std::uint32_t>(Geometry::MeshAnalysis::kHalfedgeIssueMaskPropertyName);
    ASSERT_TRUE(faceMask.IsValid());
    ASSERT_TRUE(halfedgeMask.IsValid());
    EXPECT_NE(faceMask[0], 0u);
    EXPECT_NE(faceMask[0] & Geometry::MeshAnalysis::kFaceIssueNonTriangle, 0u);
    std::size_t faceProblemHalfedges = 0;
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        if (halfedgeMask[i] & Geometry::MeshAnalysis::kHalfedgeIssueFaceProblem)
        {
            ++faceProblemHalfedges;
        }
    }
    EXPECT_EQ(faceProblemHalfedges, 4u);
}

