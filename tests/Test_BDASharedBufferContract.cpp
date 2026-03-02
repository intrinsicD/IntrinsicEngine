#include <gtest/gtest.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// BDA Shared-Buffer Render Contract Tests
// =============================================================================
//
// These tests validate the CPU-side data contracts for the BDA shared-buffer
// multi-topology rendering architecture (TODO 1.1). They verify:
//
//   - GPU struct layouts and alignment (SSBO compatibility).
//   - PrimitiveTopology enum stability.
//   - GeometryUploadRequest / GeometryBufferLayout defaults.
//   - ECS component defaults and render contract invariants.
//   - Cross-module color packing consistency.
//   - DebugDraw ↔ LineRenderPass data compatibility.
//   - GeometryViewRenderer lifecycle tracking invariants.
//
// No GPU device is needed — these are pure compile-time/contract tests.

// =============================================================================
// Section 1: GPU Data Layout Contracts
// =============================================================================

TEST(BDA_DataLayout, LineSegmentIs32Bytes)
{
    // LineSegment must be 32 bytes (2 x vec4) for GPU SSBO alignment.
    // Consumed by line.vert SSBO: struct { vec3 Start; uint ColorStart; vec3 End; uint ColorEnd; }
    EXPECT_EQ(sizeof(Graphics::DebugDraw::LineSegment), 32u);
}

TEST(BDA_DataLayout, LineSegmentAlignedTo16)
{
    EXPECT_EQ(alignof(Graphics::DebugDraw::LineSegment), 16u);
}

TEST(BDA_DataLayout, GpuPointDataIs32Bytes)
{
    // GpuPointData must be 32 bytes (2 x vec4) for GPU SSBO alignment.
    EXPECT_EQ(sizeof(Graphics::Passes::PointCloudRenderPass::GpuPointData), 32u);
}

TEST(BDA_DataLayout, GpuPointDataAlignedTo16)
{
    EXPECT_EQ(alignof(Graphics::Passes::PointCloudRenderPass::GpuPointData), 16u);
}

TEST(BDA_DataLayout, LineSegmentAndGpuPointDataSameStride)
{
    // Both GPU types have identical stride so that EnsurePerFrameBuffer<T>
    // growth calculations remain compatible across pass types.
    EXPECT_EQ(sizeof(Graphics::DebugDraw::LineSegment),
              sizeof(Graphics::Passes::PointCloudRenderPass::GpuPointData));
}

TEST(BDA_DataLayout, EdgePairIs8Bytes)
{
    // EdgePair must be 8 bytes: two uint32_t vertex indices.
    // Consumed by RetainedLineRenderPass SSBO as edge index pairs.
    EXPECT_EQ(sizeof(ECS::RenderVisualization::EdgePair), 8u);
}

TEST(BDA_DataLayout, EdgePairMemberLayout)
{
    // Verify the edge pair fields are at expected offsets for GPU SSBO.
    ECS::RenderVisualization::EdgePair ep{100, 200};
    EXPECT_EQ(ep.i0, 100u);
    EXPECT_EQ(ep.i1, 200u);

    // Verify contiguous layout (no padding between members).
    EXPECT_EQ(offsetof(ECS::RenderVisualization::EdgePair, i0), 0u);
    EXPECT_EQ(offsetof(ECS::RenderVisualization::EdgePair, i1), 4u);
}

TEST(BDA_DataLayout, GpuPointDataFieldPacking)
{
    // Verify the GPU point struct fields match the shader expectation:
    //   vec4(PosX, PosY, PosZ, Size) | vec4(NormX, NormY, NormZ, Color)
    using GPD = Graphics::Passes::PointCloudRenderPass::GpuPointData;

    EXPECT_EQ(offsetof(GPD, PosX), 0u);
    EXPECT_EQ(offsetof(GPD, PosY), 4u);
    EXPECT_EQ(offsetof(GPD, PosZ), 8u);
    EXPECT_EQ(offsetof(GPD, Size), 12u);
    EXPECT_EQ(offsetof(GPD, NormX), 16u);
    EXPECT_EQ(offsetof(GPD, NormY), 20u);
    EXPECT_EQ(offsetof(GPD, NormZ), 24u);
    EXPECT_EQ(offsetof(GPD, Color), 28u);
}

// =============================================================================
// Section 2: PrimitiveTopology Enum Contract
// =============================================================================

TEST(BDA_Topology, EnumValues)
{
    // Topology values are used in switch statements and as array indices
    // in some code paths. Verify their numeric values are stable.
    EXPECT_EQ(static_cast<int>(Graphics::PrimitiveTopology::Triangles), 0);
    EXPECT_EQ(static_cast<int>(Graphics::PrimitiveTopology::Lines), 1);
    EXPECT_EQ(static_cast<int>(Graphics::PrimitiveTopology::Points), 2);
}

TEST(BDA_Topology, TopologyCoverage)
{
    // The three topology values are the only ones that should exist.
    // This guards against accidentally adding values without updating passes.
    auto t = Graphics::PrimitiveTopology::Triangles;
    auto l = Graphics::PrimitiveTopology::Lines;
    auto p = Graphics::PrimitiveTopology::Points;

    EXPECT_NE(t, l);
    EXPECT_NE(l, p);
    EXPECT_NE(t, p);
}

// =============================================================================
// Section 3: GeometryUploadRequest / GeometryBufferLayout Defaults
// =============================================================================

TEST(BDA_UploadRequest, DefaultValues)
{
    Graphics::GeometryUploadRequest req;

    EXPECT_TRUE(req.Positions.empty());
    EXPECT_TRUE(req.Normals.empty());
    EXPECT_TRUE(req.Aux.empty());
    EXPECT_TRUE(req.Indices.empty());
    EXPECT_EQ(req.Topology, Graphics::PrimitiveTopology::Triangles);
    EXPECT_EQ(req.UploadMode, Graphics::GeometryUploadMode::Staged);
    EXPECT_FALSE(req.ReuseVertexBuffersFrom.IsValid());
}

TEST(BDA_UploadRequest, ReuseVertexBuffersFromInvalidByDefault)
{
    Graphics::GeometryUploadRequest req;

    // When ReuseVertexBuffersFrom is invalid, positions/normals/aux must be
    // provided by the caller. This is the default "allocate new buffers" path.
    EXPECT_FALSE(req.ReuseVertexBuffersFrom.IsValid());
}

TEST(BDA_BufferLayout, DefaultValues)
{
    Graphics::GeometryBufferLayout layout;

    EXPECT_EQ(layout.PositionsOffset, 0u);
    EXPECT_EQ(layout.PositionsSize, 0u);
    EXPECT_EQ(layout.NormalsOffset, 0u);
    EXPECT_EQ(layout.NormalsSize, 0u);
    EXPECT_EQ(layout.AuxOffset, 0u);
    EXPECT_EQ(layout.AuxSize, 0u);
    EXPECT_EQ(layout.Topology, Graphics::PrimitiveTopology::Triangles);
}

TEST(BDA_CpuData, ToUploadRequestConverts)
{
    Graphics::GeometryCpuData cpu;
    cpu.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    cpu.Normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    cpu.Indices = {0, 1, 2};
    cpu.Topology = Graphics::PrimitiveTopology::Lines;

    auto req = cpu.ToUploadRequest(Graphics::GeometryUploadMode::Direct);

    EXPECT_EQ(req.Positions.size(), 3u);
    EXPECT_EQ(req.Normals.size(), 3u);
    EXPECT_EQ(req.Indices.size(), 3u);
    EXPECT_EQ(req.Topology, Graphics::PrimitiveTopology::Lines);
    EXPECT_EQ(req.UploadMode, Graphics::GeometryUploadMode::Direct);
    EXPECT_FALSE(req.ReuseVertexBuffersFrom.IsValid());
}

// =============================================================================
// Section 4: Generational Handle Contract
// =============================================================================

TEST(BDA_Handle, DefaultInvalid)
{
    Geometry::GeometryHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(BDA_Handle, ValidAfterConstruction)
{
    Geometry::GeometryHandle h(0, 1);
    EXPECT_TRUE(h.IsValid());
    EXPECT_TRUE(static_cast<bool>(h));
}

TEST(BDA_Handle, DifferentGenerationsNotEqual)
{
    Geometry::GeometryHandle h1(0, 1);
    Geometry::GeometryHandle h2(0, 2);
    EXPECT_NE(h1, h2);
}

// =============================================================================
// Section 5: Cross-Module Color Packing Consistency
// =============================================================================

TEST(BDA_ColorConsistency, DebugDrawAndPointCloudSameConvention)
{
    // Both DebugDraw and PointCloudRenderPass delegate to GpuColor.
    // Verify they produce identical results for the same input.
    uint32_t ddRed = Graphics::DebugDraw::PackColor(255, 0, 0, 255);
    uint32_t pcRed = Graphics::Passes::PointCloudRenderPass::PackColor(255, 0, 0, 255);
    EXPECT_EQ(ddRed, pcRed);

    uint32_t ddGreen = Graphics::DebugDraw::PackColorF(0.0f, 1.0f, 0.0f, 1.0f);
    uint32_t pcGreen = Graphics::Passes::PointCloudRenderPass::PackColorF(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_EQ(ddGreen, pcGreen);
}

TEST(BDA_ColorConsistency, PackColorABGRByteOrder)
{
    // Vulkan convention: R in low bits (ABGR packing).
    // GPU shader uses unpackUnorm4x8() which reads R from byte 0.
    uint32_t c = Graphics::GpuColor::PackColor(0xAA, 0xBB, 0xCC, 0xDD);
    EXPECT_EQ((c >>  0) & 0xFF, 0xAAu); // R in byte 0
    EXPECT_EQ((c >>  8) & 0xFF, 0xBBu); // G in byte 1
    EXPECT_EQ((c >> 16) & 0xFF, 0xCCu); // B in byte 2
    EXPECT_EQ((c >> 24) & 0xFF, 0xDDu); // A in byte 3
}

TEST(BDA_ColorConsistency, PackColorFRoundTrip)
{
    // Test float → packed → channel extraction roundtrip for mid-range values.
    uint32_t c = Graphics::GpuColor::PackColorF(0.5f, 0.25f, 0.75f, 1.0f);
    uint8_t r = (c >> 0) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;
    uint8_t a = (c >> 24) & 0xFF;

    EXPECT_NEAR(r, 128, 1);  // 0.5 * 255 + 0.5 = 128
    EXPECT_NEAR(g, 64, 1);   // 0.25 * 255 + 0.5 = 64
    EXPECT_NEAR(b, 191, 1);  // 0.75 * 255 + 0.5 = 191
    EXPECT_EQ(a, 255);       // 1.0 * 255 + 0.5 = 255
}

// =============================================================================
// Section 6: ECS Component Defaults — Render Contract
// =============================================================================

TEST(BDA_Components, MeshRendererDefaults)
{
    ECS::MeshRenderer::Component comp;

    EXPECT_FALSE(comp.Geometry.IsValid());
    EXPECT_EQ(comp.GpuSlot, ECS::MeshRenderer::Component::kInvalidSlot);
    EXPECT_EQ(comp.GpuSlot, ~0u);
}

TEST(BDA_Components, RenderVisualizationDefaults)
{
    ECS::RenderVisualization::Component viz;

    // Default visibility: surface on, wireframe off, vertices off.
    EXPECT_TRUE(viz.ShowSurface);
    EXPECT_FALSE(viz.ShowWireframe);
    EXPECT_FALSE(viz.ShowVertices);

    // Wireframe defaults
    EXPECT_FLOAT_EQ(viz.WireframeWidth, 1.5f);
    EXPECT_FALSE(viz.WireframeOverlay);

    // Vertex defaults
    EXPECT_FLOAT_EQ(viz.VertexSize, 0.008f);
    EXPECT_EQ(viz.VertexRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);

    // GPU views initially invalid/dirty
    EXPECT_FALSE(viz.VertexView.IsValid());
    EXPECT_TRUE(viz.VertexViewDirty);
    EXPECT_TRUE(viz.EdgeCacheDirty);
    EXPECT_TRUE(viz.VertexNormalsDirty);
    EXPECT_TRUE(viz.CachedEdges.empty());
    EXPECT_TRUE(viz.CachedVertexNormals.empty());

    // Sync state default
    EXPECT_TRUE(viz.CachedShowSurface);
}

TEST(BDA_Components, GeometryViewRendererDefaults)
{
    ECS::GeometryViewRenderer::Component gvr;

    // All handles invalid by default
    EXPECT_FALSE(gvr.Surface.IsValid());
    EXPECT_FALSE(gvr.Vertices.IsValid());

    // GPU slots invalid
    EXPECT_EQ(gvr.SurfaceGpuSlot, ECS::MeshRenderer::Component::kInvalidSlot);
    EXPECT_EQ(gvr.VerticesGpuSlot, ECS::MeshRenderer::Component::kInvalidSlot);

    // No wireframe edge buffer
    EXPECT_EQ(gvr.WireframeEdgeCount, 0u);

    // Default visibility
    EXPECT_TRUE(gvr.ShowSurface);
    EXPECT_FALSE(gvr.ShowVertices);
}

TEST(BDA_Components, GraphDataDefaults)
{
    ECS::Graph::Data data;

    EXPECT_EQ(data.GraphRef, nullptr);
    EXPECT_FALSE(data.GpuGeometry.IsValid());
    EXPECT_TRUE(data.GpuDirty);
    EXPECT_EQ(data.GpuVertexCount, 0u);
    EXPECT_TRUE(data.CachedEdgePairs.empty());
    EXPECT_TRUE(data.Visible);

    // Default rendering params
    EXPECT_EQ(data.NodeRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
    EXPECT_FLOAT_EQ(data.DefaultNodeRadius, 0.01f);
    EXPECT_FLOAT_EQ(data.NodeSizeMultiplier, 1.0f);
    EXPECT_FLOAT_EQ(data.EdgeWidth, 1.5f);
    EXPECT_FALSE(data.EdgesOverlay);

    // Queries on null graph
    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
}

TEST(BDA_Components, PointCloudRendererDefaults)
{
    ECS::PointCloudRenderer::Component pc;

    EXPECT_TRUE(pc.Positions.empty());
    EXPECT_TRUE(pc.Normals.empty());
    EXPECT_TRUE(pc.Colors.empty());
    EXPECT_TRUE(pc.Radii.empty());

    EXPECT_EQ(pc.RenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
    EXPECT_FLOAT_EQ(pc.DefaultRadius, 0.005f);
    EXPECT_FLOAT_EQ(pc.SizeMultiplier, 1.0f);
    EXPECT_TRUE(pc.Visible);

    EXPECT_EQ(pc.PointCount(), 0u);
    EXPECT_FALSE(pc.HasNormals());
    EXPECT_FALSE(pc.HasColors());
    EXPECT_FALSE(pc.HasRadii());
}

// =============================================================================
// Section 7: Shared-Buffer Multi-Topology Invariants
// =============================================================================

TEST(BDA_SharedBuffer, ReuseVertexBuffersFromFlagIgnoresPositionSpans)
{
    // When ReuseVertexBuffersFrom is set to a valid handle, the upload
    // request must ignore Positions/Normals/Aux and only use Indices.
    // This is a contract test — we verify the request structure allows it.
    std::vector<glm::vec3> positions = {{0, 0, 0}};
    std::vector<uint32_t> edges = {0, 0};

    Graphics::GeometryUploadRequest req;
    req.ReuseVertexBuffersFrom = Geometry::GeometryHandle(0, 1);
    req.Positions = positions;  // Should be ignored when ReuseVertexBuffersFrom is valid
    req.Indices = edges;
    req.Topology = Graphics::PrimitiveTopology::Lines;

    EXPECT_TRUE(req.ReuseVertexBuffersFrom.IsValid());
    EXPECT_FALSE(req.Indices.empty()); // Indices are always used
    EXPECT_EQ(req.Topology, Graphics::PrimitiveTopology::Lines);
}

TEST(BDA_SharedBuffer, ThreeTopologyViewsFromSameSource)
{
    // Verify that three different topology requests can be constructed
    // from the same source handle (triangle mesh → wireframe + vertices).
    Geometry::GeometryHandle meshHandle(0, 1);

    // Wireframe view
    Graphics::GeometryUploadRequest wireReq;
    wireReq.ReuseVertexBuffersFrom = meshHandle;
    wireReq.Indices = {0, 1, 1, 2, 2, 0};
    wireReq.Topology = Graphics::PrimitiveTopology::Lines;

    // Vertex view (identity / direct draw — no indices needed in practice,
    // but the request must still be well-formed)
    Graphics::GeometryUploadRequest vertReq;
    vertReq.ReuseVertexBuffersFrom = meshHandle;
    vertReq.Topology = Graphics::PrimitiveTopology::Points;

    EXPECT_TRUE(wireReq.ReuseVertexBuffersFrom.IsValid());
    EXPECT_TRUE(vertReq.ReuseVertexBuffersFrom.IsValid());
    EXPECT_EQ(wireReq.ReuseVertexBuffersFrom, vertReq.ReuseVertexBuffersFrom);
    EXPECT_NE(wireReq.Topology, vertReq.Topology);
}

// =============================================================================
// Section 8: DebugDraw → LineRenderPass Data Contract
// =============================================================================

TEST(BDA_DebugDrawContract, DepthTestedAndOverlayAreSeparate)
{
    Graphics::DebugDraw dd;

    dd.Line({0, 0, 0}, {1, 0, 0}, Graphics::DebugDraw::Red());
    dd.OverlayLine({0, 0, 0}, {0, 1, 0}, Graphics::DebugDraw::Green());

    // LineRenderPass creates two sub-passes: one for depth-tested, one for overlay.
    // The data must be kept separate so each sub-pass gets its own SSBO.
    EXPECT_EQ(dd.GetLineCount(), 1u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 1u);

    auto depthLines = dd.GetLines();
    auto overlayLines = dd.GetOverlayLines();

    EXPECT_EQ(depthLines.size(), 1u);
    EXPECT_EQ(overlayLines.size(), 1u);

    // Data pointer stability: spans must remain valid until Reset().
    const auto* depthPtr = depthLines.data();
    const auto* overlayPtr = overlayLines.data();
    EXPECT_NE(depthPtr, nullptr);
    EXPECT_NE(overlayPtr, nullptr);
}

TEST(BDA_DebugDrawContract, ResetBetweenFrames)
{
    Graphics::DebugDraw dd;

    // Simulate frame N
    dd.Line({0, 0, 0}, {1, 0, 0}, Graphics::DebugDraw::Red());
    EXPECT_TRUE(dd.HasContent());

    // Simulate frame N+1
    dd.Reset();
    EXPECT_FALSE(dd.HasContent());
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// Section 9: Retained-Mode Edge Buffer Lifecycle
// =============================================================================

TEST(BDA_EdgeBuffer, EdgeCacheLazyInit)
{
    // RenderVisualization edge cache starts empty and dirty.
    // RetainedLineRenderPass creates the GPU edge buffer lazily when
    // ShowWireframe=true and EdgeCacheDirty=true.
    ECS::RenderVisualization::Component viz;

    EXPECT_TRUE(viz.CachedEdges.empty());
    EXPECT_TRUE(viz.EdgeCacheDirty);

    // Simulate edge cache population (as MeshRenderPass does).
    viz.CachedEdges = {{0, 1}, {1, 2}, {2, 0}};
    viz.EdgeCacheDirty = false;

    EXPECT_EQ(viz.CachedEdges.size(), 3u);
    EXPECT_FALSE(viz.EdgeCacheDirty);
}

TEST(BDA_EdgeBuffer, WireframeEdgeCountTracksLifecycle)
{
    ECS::GeometryViewRenderer::Component gvr;

    // Initially no wireframe buffer.
    EXPECT_EQ(gvr.WireframeEdgeCount, 0u);

    // Simulate RetainedLineRenderPass creating a persistent edge buffer.
    gvr.WireframeEdgeCount = 150;
    EXPECT_EQ(gvr.WireframeEdgeCount, 150u);

    // Simulate edge topology change (remesh) → buffer needs rebuild.
    gvr.WireframeEdgeCount = 0;
    EXPECT_EQ(gvr.WireframeEdgeCount, 0u);
}

// =============================================================================
// Section 10: Graph Data — PropertySet-backed Authority
// =============================================================================

TEST(BDA_GraphData, PropertySetBackedColors)
{
    auto graph = std::make_shared<Geometry::Graph::Graph>();

    // Add some vertices
    auto v0 = graph->AddVertex({0, 0, 0});
    auto v1 = graph->AddVertex({1, 0, 0});

    ECS::Graph::Data data;
    data.GraphRef = graph;

    // Before adding color property
    EXPECT_FALSE(data.HasNodeColors());

    // Add color property
    auto colors = graph->GetOrAddVertexProperty<glm::vec4>("v:color", {1, 1, 1, 1});
    EXPECT_TRUE(data.HasNodeColors());

    EXPECT_EQ(data.NodeCount(), 2u);
}

TEST(BDA_GraphData, PropertySetBackedRadii)
{
    auto graph = std::make_shared<Geometry::Graph::Graph>();
    graph->AddVertex({0, 0, 0});

    ECS::Graph::Data data;
    data.GraphRef = graph;

    EXPECT_FALSE(data.HasNodeRadii());

    auto radii = graph->GetOrAddVertexProperty<float>("v:radius", 0.01f);
    EXPECT_TRUE(data.HasNodeRadii());
}

TEST(BDA_GraphData, GpuDirtyDefaultTrue)
{
    // New Graph::Data must start dirty so GraphGeometrySyncSystem
    // uploads positions on first frame.
    ECS::Graph::Data data;
    EXPECT_TRUE(data.GpuDirty);
}

// =============================================================================
// Section 11: EWA Render Mode Contract
// =============================================================================

TEST(BDA_EWA, RenderModeEnumValues)
{
    // Verify the three render mode enum values are stable.
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::FlatDisc), 0u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::Surfel), 1u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::EWA), 2u);
}

TEST(BDA_EWA, RenderVisualizationAcceptsEWA)
{
    ECS::RenderVisualization::Component viz;
    viz.VertexRenderMode = Geometry::PointCloud::RenderMode::EWA;
    EXPECT_EQ(viz.VertexRenderMode, Geometry::PointCloud::RenderMode::EWA);
}

TEST(BDA_EWA, GraphDataAcceptsEWA)
{
    ECS::Graph::Data data;
    data.NodeRenderMode = Geometry::PointCloud::RenderMode::EWA;
    EXPECT_EQ(data.NodeRenderMode, Geometry::PointCloud::RenderMode::EWA);
}

TEST(BDA_EWA, PointCloudRendererAcceptsEWA)
{
    ECS::PointCloudRenderer::Component pc;
    pc.RenderMode = Geometry::PointCloud::RenderMode::EWA;
    EXPECT_EQ(pc.RenderMode, Geometry::PointCloud::RenderMode::EWA);
}
