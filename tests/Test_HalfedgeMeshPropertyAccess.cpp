#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

// =============================================================================
// PropertySet Public Accessor Tests
// =============================================================================

// Use shared builders: MakeRightTriangle(), MakeQuadPair(), MakeSingleQuad()
static Geometry::Halfedge::Mesh MakeTriangle() { return MakeRightTriangle(); }

// =============================================================================
// EdgeProperties() Tests
// =============================================================================

TEST(HalfedgeMesh_PropertyAccess, EdgeProperties_NonConst_ReturnsCorrectSize)
{
    auto mesh = MakeTriangle();
    auto& edges = mesh.EdgeProperties();
    EXPECT_EQ(edges.Size(), mesh.EdgesSize());
    EXPECT_EQ(edges.Size(), 3u); // triangle has 3 edges
}

TEST(HalfedgeMesh_PropertyAccess, EdgeProperties_Const_ReturnsCorrectSize)
{
    const auto mesh = MakeTriangle();
    const auto& edges = mesh.EdgeProperties();
    EXPECT_EQ(edges.Size(), mesh.EdgesSize());
}

TEST(HalfedgeMesh_PropertyAccess, EdgeProperties_CanAddUserProperty)
{
    using namespace Geometry;
    auto mesh = MakeQuadPair();

    // Add a custom per-edge weight property via the public accessor.
    auto weight = EdgeProperty<float>(
        mesh.EdgeProperties().GetOrAdd<float>("e:weight", 0.0f));
    ASSERT_TRUE(weight.IsValid());

    // Write values.
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        EdgeHandle e{static_cast<PropertyIndex>(i)};
        if (!mesh.IsDeleted(e))
            weight[e] = static_cast<float>(i) * 1.5f;
    }

    // Read back and verify.
    auto readWeight = EdgeProperty<float>(
        mesh.EdgeProperties().Get<float>("e:weight"));
    ASSERT_TRUE(readWeight.IsValid());
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        EdgeHandle e{static_cast<PropertyIndex>(i)};
        if (!mesh.IsDeleted(e))
            EXPECT_FLOAT_EQ(readWeight[e], static_cast<float>(i) * 1.5f);
    }
}

TEST(HalfedgeMesh_PropertyAccess, EdgeProperties_ListsProperties)
{
    using namespace Geometry;
    auto mesh = MakeTriangle();

    // The edge PropertySet should have at least the built-in "e:deleted" property.
    auto names = mesh.EdgeProperties().Properties();
    bool hasDeleted = false;
    for (const auto& n : names)
    {
        if (n == "e:deleted") hasDeleted = true;
    }
    EXPECT_TRUE(hasDeleted);
}

// =============================================================================
// FaceProperties() Tests
// =============================================================================

TEST(HalfedgeMesh_PropertyAccess, FaceProperties_NonConst_ReturnsCorrectSize)
{
    auto mesh = MakeQuadPair();
    auto& faces = mesh.FaceProperties();
    EXPECT_EQ(faces.Size(), mesh.FacesSize());
    EXPECT_EQ(faces.Size(), 2u); // two triangles
}

TEST(HalfedgeMesh_PropertyAccess, FaceProperties_Const_ReturnsCorrectSize)
{
    const auto mesh = MakeQuadPair();
    const auto& faces = mesh.FaceProperties();
    EXPECT_EQ(faces.Size(), mesh.FacesSize());
}

TEST(HalfedgeMesh_PropertyAccess, FaceProperties_CanAddUserProperty)
{
    using namespace Geometry;
    auto mesh = MakeQuadPair();

    // Add per-face color.
    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());

    FaceHandle f0{0};
    FaceHandle f1{1};
    color[f0] = glm::vec4(1, 0, 0, 1);
    color[f1] = glm::vec4(0, 1, 0, 1);

    auto readColor = FaceProperty<glm::vec4>(
        mesh.FaceProperties().Get<glm::vec4>("f:color"));
    ASSERT_TRUE(readColor.IsValid());
    EXPECT_FLOAT_EQ(readColor[f0].r, 1.0f);
    EXPECT_FLOAT_EQ(readColor[f1].g, 1.0f);
}

TEST(HalfedgeMesh_PropertyAccess, FaceProperties_SpanAccess)
{
    using namespace Geometry;
    auto mesh = MakeQuadPair();

    auto label = FaceProperty<int>(
        mesh.FaceProperties().GetOrAdd<int>("f:label", -1));
    ASSERT_TRUE(label.IsValid());

    label[FaceHandle{0}] = 42;
    label[FaceHandle{1}] = 99;

    // Access via span for GPU upload.
    auto span = label.Span();
    EXPECT_EQ(span.size(), 2u);
    EXPECT_EQ(span[0], 42);
    EXPECT_EQ(span[1], 99);
}

// =============================================================================
// HalfedgeProperties() Tests
// =============================================================================

TEST(HalfedgeMesh_PropertyAccess, HalfedgeProperties_NonConst_ReturnsCorrectSize)
{
    auto mesh = MakeTriangle();
    auto& halfedges = mesh.HalfedgeProperties();
    EXPECT_EQ(halfedges.Size(), mesh.HalfedgesSize());
    // A triangle has 3 edges × 2 halfedges = 6.
    EXPECT_EQ(halfedges.Size(), 6u);
}

TEST(HalfedgeMesh_PropertyAccess, HalfedgeProperties_Const_ReturnsCorrectSize)
{
    const auto mesh = MakeTriangle();
    const auto& halfedges = mesh.HalfedgeProperties();
    EXPECT_EQ(halfedges.Size(), mesh.HalfedgesSize());
}

TEST(HalfedgeMesh_PropertyAccess, HalfedgeProperties_CanAddUserProperty)
{
    using namespace Geometry;
    auto mesh = MakeTriangle();

    // Add per-halfedge parametric weight.
    auto param = HalfedgeProperty<float>(
        mesh.HalfedgeProperties().GetOrAdd<float>("h:param", 0.0f));
    ASSERT_TRUE(param.IsValid());

    // Write/read round-trip via handle.
    HalfedgeHandle h0{0};
    param[h0] = 3.14f;
    EXPECT_FLOAT_EQ(param[h0], 3.14f);
}

// =============================================================================
// Traversal layer tests
// =============================================================================

TEST(HalfedgeMesh_Traversal, HalfedgesAroundFace_QuadMatchesNextCycle)
{
    using namespace Geometry;

    const auto mesh = MakeSingleQuad();
    const FaceHandle f{0};
    ASSERT_FALSE(mesh.IsDeleted(f));

    std::vector<HalfedgeHandle> visited;
    for (const HalfedgeHandle h : mesh.HalfedgesAroundFace(f))
    {
        visited.push_back(h);
    }

    ASSERT_EQ(visited.size(), 4u);

    HalfedgeHandle h = mesh.Halfedge(f);
    for (std::size_t i = 0; i < visited.size(); ++i)
    {
        EXPECT_EQ(visited[i], h);
        h = mesh.NextHalfedge(h);
    }
    EXPECT_EQ(h, mesh.Halfedge(f));
}

TEST(HalfedgeMesh_Traversal, VerticesAroundFace_QuadMatchesHalfedgeToVertices)
{
    using namespace Geometry;

    const auto mesh = MakeSingleQuad();
    const FaceHandle f{0};

    std::vector<VertexHandle> visited;
    for (const VertexHandle v : mesh.VerticesAroundFace(f))
    {
        visited.push_back(v);
    }

    ASSERT_EQ(visited.size(), 4u);

    std::vector<VertexHandle> expected;
    for (const HalfedgeHandle h : mesh.HalfedgesAroundFace(f))
    {
        expected.push_back(mesh.ToVertex(h));
    }

    EXPECT_EQ(visited, expected);
}

TEST(HalfedgeMesh_Traversal, HalfedgesAroundVertex_CountMatchesValence)
{
    using namespace Geometry;

    const auto mesh = MakeTriangle();
    const VertexHandle v{0};

    std::vector<HalfedgeHandle> visited;
    for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(v))
    {
        visited.push_back(h);
    }

    EXPECT_EQ(visited.size(), mesh.Valence(v));

    std::unordered_set<uint32_t> uniqueHalfedges;
    for (const HalfedgeHandle h : visited)
    {
        EXPECT_TRUE(uniqueHalfedges.insert(h.Index).second);
    }
}

TEST(HalfedgeMesh_Traversal, FacesAroundVertex_BoundaryVertexSkipsBoundaryGap)
{
    using namespace Geometry;

    const auto mesh = MakeTriangle();
    const VertexHandle v{0};

    std::vector<FaceHandle> visited;
    for (const FaceHandle f : mesh.FacesAroundVertex(v))
    {
        visited.push_back(f);
    }

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited.front(), FaceHandle{0});
}

TEST(HalfedgeMesh_Traversal, FacesAroundVertex_InteriorVertexVisitsIncidentFacesOnce)
{
    using namespace Geometry;

    const auto mesh = MakeQuadPair();
    const VertexHandle v{1};

    std::vector<FaceHandle> visited;
    for (const FaceHandle f : mesh.FacesAroundVertex(v))
    {
        visited.push_back(f);
    }

    ASSERT_EQ(visited.size(), 2u);

    std::unordered_set<uint32_t> uniqueFaces;
    for (const FaceHandle f : visited)
    {
        EXPECT_TRUE(uniqueFaces.insert(f.Index).second);
    }
}

TEST(HalfedgeMesh_Traversal, VertexOneRingRangesAreEmptyForIsolatedVertex)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    const VertexHandle isolated = mesh.AddVertex(glm::vec3(2.0f, 3.0f, 4.0f));

    std::size_t halfedgeCount = 0;
    for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(isolated))
    {
        (void)h;
        ++halfedgeCount;
    }

    std::size_t faceCount = 0;
    for (const FaceHandle f : mesh.FacesAroundVertex(isolated))
    {
        (void)f;
        ++faceCount;
    }

    EXPECT_EQ(halfedgeCount, 0u);
    EXPECT_EQ(faceCount, 0u);
}

TEST(HalfedgeMesh_Traversal, BoundaryHalfedges_VisitsEachBoundaryEdgeOnce)
{
    using namespace Geometry;

    const auto mesh = MakeTriangle();
    const HalfedgeHandle h0 = mesh.Halfedge(FaceHandle{0});
    const HalfedgeHandle boundaryStart = mesh.OppositeHalfedge(h0);
    ASSERT_TRUE(mesh.IsBoundary(boundaryStart));

    std::vector<HalfedgeHandle> visited;
    for (const HalfedgeHandle h : mesh.BoundaryHalfedges(boundaryStart))
    {
        visited.push_back(h);
        EXPECT_TRUE(mesh.IsBoundary(h));
    }

    ASSERT_EQ(visited.size(), 3u);
    std::unordered_set<uint32_t> unique;
    for (const HalfedgeHandle h : visited)
    {
        EXPECT_TRUE(unique.insert(h.Index).second);
    }
}

TEST(HalfedgeMesh_Traversal, BoundaryVertices_MatchesBoundaryLoopConnectivity)
{
    using namespace Geometry;

    const auto mesh = MakeTriangle();
    const HalfedgeHandle h0 = mesh.Halfedge(FaceHandle{0});
    const HalfedgeHandle boundaryStart = mesh.OppositeHalfedge(h0);

    std::vector<VertexHandle> visited;
    for (const VertexHandle v : mesh.BoundaryVertices(boundaryStart))
    {
        visited.push_back(v);
    }

    ASSERT_EQ(visited.size(), 3u);

    std::vector<VertexHandle> expected;
    for (const HalfedgeHandle h : mesh.BoundaryHalfedges(boundaryStart))
    {
        expected.push_back(mesh.FromVertex(h));
    }

    EXPECT_EQ(visited, expected);
}

// =============================================================================
// ExtractEdgeVertexPairs Tests
// =============================================================================

TEST(HalfedgeMesh_EdgeExtraction, SingleTriangle_ReturnsThreeEdges)
{
    auto mesh = MakeTriangle();
    auto edges = mesh.ExtractEdgeVertexPairs();
    EXPECT_EQ(edges.size(), 3u);
}

TEST(HalfedgeMesh_EdgeExtraction, EdgeVertices_AreValidIndices)
{
    auto mesh = MakeTriangle();
    auto edges = mesh.ExtractEdgeVertexPairs();

    for (const auto& [i0, i1] : edges)
    {
        EXPECT_LT(i0, static_cast<uint32_t>(mesh.VerticesSize()));
        EXPECT_LT(i1, static_cast<uint32_t>(mesh.VerticesSize()));
        EXPECT_NE(i0, i1); // no self-loops
    }
}

TEST(HalfedgeMesh_EdgeExtraction, TwoTriangles_SharedEdgeAppearsOnce)
{
    auto mesh = MakeQuadPair();
    // Two triangles sharing one edge: 3 + 3 - 1 = 5 unique edges.
    auto edges = mesh.ExtractEdgeVertexPairs();
    EXPECT_EQ(edges.size(), 5u);

    // Verify no duplicates (as unordered pairs).
    std::unordered_set<uint64_t> edgeSet;
    for (const auto& [i0, i1] : edges)
    {
        uint32_t lo = std::min(i0, i1);
        uint32_t hi = std::max(i0, i1);
        uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;
        EXPECT_TRUE(edgeSet.insert(key).second) << "Duplicate edge: " << lo << "-" << hi;
    }
}

TEST(HalfedgeMesh_EdgeExtraction, MatchesFromVertex_ToVertex)
{
    using namespace Geometry;
    auto mesh = MakeQuadPair();
    auto extracted = mesh.ExtractEdgeVertexPairs();

    // Verify against the per-edge FromVertex/ToVertex API.
    std::size_t idx = 0;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        EdgeHandle e{static_cast<PropertyIndex>(i)};
        if (mesh.IsDeleted(e))
            continue;

        auto h0 = mesh.Halfedge(e, 0);
        auto from = mesh.FromVertex(h0).Index;
        auto to = mesh.ToVertex(h0).Index;

        ASSERT_LT(idx, extracted.size());
        EXPECT_EQ(extracted[idx].i0, from);
        EXPECT_EQ(extracted[idx].i1, to);
        ++idx;
    }
    EXPECT_EQ(idx, extracted.size());
}

TEST(HalfedgeMesh_EdgeExtraction, SpanOverload_FillsCorrectly)
{
    auto mesh = MakeQuadPair();
    const std::size_t edgeCount = mesh.EdgeCount();

    std::vector<Geometry::Halfedge::EdgeVertexPair> buffer(edgeCount);
    std::size_t written = mesh.ExtractEdgeVertexPairs(std::span{buffer});
    EXPECT_EQ(written, edgeCount);

    // Should match the vector-returning version.
    auto reference = mesh.ExtractEdgeVertexPairs();
    ASSERT_EQ(reference.size(), written);
    for (std::size_t i = 0; i < written; ++i)
    {
        EXPECT_EQ(buffer[i].i0, reference[i].i0);
        EXPECT_EQ(buffer[i].i1, reference[i].i1);
    }
}

TEST(HalfedgeMesh_EdgeExtraction, SpanOverload_PartialBuffer)
{
    auto mesh = MakeQuadPair();

    // Provide a buffer smaller than the total edge count.
    std::vector<Geometry::Halfedge::EdgeVertexPair> buffer(2);
    std::size_t written = mesh.ExtractEdgeVertexPairs(std::span{buffer});
    EXPECT_EQ(written, 2u); // only fills what fits
}

TEST(HalfedgeMesh_EdgeExtraction, EmptyMesh_ReturnsEmpty)
{
    Geometry::Halfedge::Mesh mesh;
    auto edges = mesh.ExtractEdgeVertexPairs();
    EXPECT_TRUE(edges.empty());
}

TEST(HalfedgeMesh_EdgeExtraction, AfterEdgeDelete_SkipsDeletedEdge)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    auto v3 = mesh.AddVertex(glm::vec3(1, 1, 0));
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v2, v1, v3);

    // 5 edges before deletion.
    ASSERT_EQ(mesh.ExtractEdgeVertexPairs().size(), 5u);

    // Delete one face (this marks its edges as deleted too, unless shared).
    mesh.DeleteFace(FaceHandle{0});

    // After deletion: some edges are deleted. The extracted count should
    // match the non-deleted edge count.
    auto edges = mesh.ExtractEdgeVertexPairs();
    EXPECT_EQ(edges.size(), mesh.EdgeCount());

    // After garbage collection, the result should still be consistent.
    mesh.GarbageCollection();
    auto edgesGC = mesh.ExtractEdgeVertexPairs();
    EXPECT_EQ(edgesGC.size(), mesh.EdgeCount());
}

TEST(HalfedgeMesh_EdgeExtraction, EdgeVertexPair_SizeOf8)
{
    // The struct must be exactly 8 bytes for GPU SSBO compatibility.
    static_assert(sizeof(Geometry::Halfedge::EdgeVertexPair) == 8);
}

// =============================================================================
// Cross-accessor consistency
// =============================================================================

TEST(HalfedgeMesh_PropertyAccess, AllPropertySets_SizesConsistent)
{
    auto mesh = MakeQuadPair();
    EXPECT_EQ(mesh.VertexProperties().Size(), mesh.VerticesSize());
    EXPECT_EQ(mesh.EdgeProperties().Size(), mesh.EdgesSize());
    EXPECT_EQ(mesh.FaceProperties().Size(), mesh.FacesSize());
    EXPECT_EQ(mesh.HalfedgeProperties().Size(), mesh.HalfedgesSize());
}

TEST(HalfedgeMesh_PropertyAccess, PropertySet_RemoveThenReuseSameNameWorks)
{
    Geometry::PropertySet props;
    props.Resize(3);

    auto weights = props.Add<float>("p:weight", 1.0f);
    ASSERT_TRUE(weights.IsValid());
    weights[0] = 2.5f;

    props.Remove(weights);
    EXPECT_FALSE(weights.IsValid());
    EXPECT_FALSE(props.Exists("p:weight"));

    auto weights2 = props.GetOrAdd<float>("p:weight", 7.0f);
    ASSERT_TRUE(weights2.IsValid());
    EXPECT_TRUE(props.Exists("p:weight"));
    EXPECT_EQ(weights2.Vector().size(), 3u);
    EXPECT_FLOAT_EQ(weights2[0], 7.0f);
    EXPECT_FLOAT_EQ(weights2[1], 7.0f);
    EXPECT_FLOAT_EQ(weights2[2], 7.0f);
}

TEST(HalfedgeMesh_PropertyAccess, VertexProperties_RemainUsableAfterGarbageCollection)
{
    using namespace Geometry;

    auto mesh = MakeQuadPair();
    mesh.DeleteFace(FaceHandle{0});
    ASSERT_TRUE(mesh.HasGarbage());

    mesh.GarbageCollection();
    EXPECT_FALSE(mesh.HasGarbage());

    auto heat = VertexProperty<float>(mesh.VertexProperties().GetOrAdd<float>("v:heat", 3.0f));
    ASSERT_TRUE(heat.IsValid());
    EXPECT_EQ(heat.Vector().size(), mesh.VerticesSize());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        VertexHandle v{static_cast<PropertyIndex>(i)};
        heat[v] = static_cast<float>(i);
        EXPECT_FLOAT_EQ(heat[v], static_cast<float>(i));
    }

    auto heatRead = VertexProperty<float>(mesh.VertexProperties().Get<float>("v:heat"));
    ASSERT_TRUE(heatRead.IsValid());
    EXPECT_EQ(heatRead.Vector().size(), mesh.VerticesSize());
}

TEST(HalfedgeMesh_PropertyAccess, CurvatureMutableOverloadsPublishVertexProperties)
{
    using namespace Geometry;

    auto mesh = MakeQuadPair();

    auto meanResult = Curvature::ComputeMeanCurvature(mesh);
    ASSERT_TRUE(meanResult.has_value());
    ASSERT_TRUE(meanResult->Property.IsValid());
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:mean_curvature"));

    auto gaussResult = Curvature::ComputeGaussianCurvature(mesh);
    ASSERT_TRUE(gaussResult.has_value());
    ASSERT_TRUE(gaussResult->Property.IsValid());
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:gaussian_curvature"));

    auto field = Curvature::ComputeCurvature(mesh);
    ASSERT_TRUE(field.MeanCurvatureProperty.IsValid());
    ASSERT_TRUE(field.GaussianCurvatureProperty.IsValid());
    ASSERT_TRUE(field.MinPrincipalCurvatureProperty.IsValid());
    ASSERT_TRUE(field.MaxPrincipalCurvatureProperty.IsValid());
    ASSERT_TRUE(field.MeanCurvatureNormalProperty.IsValid());
}

TEST(HalfedgeMesh_PropertyAccess, GeodesicMutableOverloadPublishesDistanceAndSourceProperties)
{
    using namespace Geometry;

    auto mesh = MakeQuadPair();
    const std::array<std::size_t, 1> sources{0};

    auto result = Geodesic::ComputeDistance(mesh, std::span<const std::size_t>(sources));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->DistanceProperty.IsValid());
    ASSERT_TRUE(result->IsSourceProperty.IsValid());
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:geodesic_distance"));
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:is_geodesic_source"));

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vh{static_cast<PropertyIndex>(i)};
        EXPECT_EQ(result->IsSourceProperty[vh], i == 0u);
    }
}

TEST(HalfedgeMesh_PropertyAccess, ParameterizationMutableOverloadPublishesUvAndPinProperties)
{
    using namespace Geometry;

    auto mesh = MakeTriangle();
    Parameterization::ParameterizationParams params;
    params.PinVertex0 = 0;
    params.PinVertex1 = 1;
    params.PinUV0 = glm::vec2(0.0f, 0.0f);
    params.PinUV1 = glm::vec2(1.0f, 0.0f);

    auto result = Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->UVProperty.IsValid());
    ASSERT_TRUE(result->IsPinnedProperty.IsValid());
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:texcoord"));
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:lscm_pinned"));

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const VertexHandle vh{static_cast<PropertyIndex>(i)};
        EXPECT_EQ(result->UVProperty[vh], result->UVs[i]);
    }

    EXPECT_TRUE(result->IsPinnedProperty[VertexHandle{static_cast<PropertyIndex>(result->PinVertex0)}]);
    EXPECT_TRUE(result->IsPinnedProperty[VertexHandle{static_cast<PropertyIndex>(result->PinVertex1)}]);
}

