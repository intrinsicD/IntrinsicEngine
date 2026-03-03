#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// MeshViewLifecycle — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the mesh-derived geometry view
// lifecycle (edge views and vertex views) without requiring a GPU device. They
// verify:
//   - MeshEdgeView::Component default state and field semantics.
//   - MeshVertexView::Component default state and field semantics.
//   - Invalid slot sentinel consistency with other component types.
//   - HasGpuGeometry() query correctness.
//   - Dirty lifecycle transitions.
//   - Edge index flattening layout compatible with BDA EdgePair reads.
//   - ReuseVertexBuffersFrom upload request construction.

// =============================================================================
// Section 1: MeshEdgeView Component Defaults
// =============================================================================

TEST(MeshViewLifecycle_Contract, EdgeViewDefaultState)
{
    ECS::MeshEdgeView::Component comp;

    EXPECT_FALSE(comp.Geometry.IsValid());
    EXPECT_EQ(comp.GpuSlot, ECS::MeshEdgeView::Component::kInvalidSlot);
    EXPECT_EQ(comp.EdgeCount, 0u);
    EXPECT_TRUE(comp.Dirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(MeshViewLifecycle_Contract, EdgeViewInvalidSlotSentinel)
{
    // kInvalidSlot must be ~0u, consistent with other component types.
    EXPECT_EQ(ECS::MeshEdgeView::Component::kInvalidSlot, ~0u);
    EXPECT_EQ(ECS::MeshEdgeView::Component::kInvalidSlot,
              ECS::MeshRenderer::Component::kInvalidSlot);
    EXPECT_EQ(ECS::MeshEdgeView::Component::kInvalidSlot,
              ECS::PointCloudRenderer::Component::kInvalidSlot);
}

TEST(MeshViewLifecycle_Contract, EdgeViewHasGpuGeometryFalseByDefault)
{
    ECS::MeshEdgeView::Component comp;
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(MeshViewLifecycle_Contract, EdgeViewHasGpuGeometryTrueWhenValid)
{
    ECS::MeshEdgeView::Component comp;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    EXPECT_TRUE(comp.HasGpuGeometry());
}

// =============================================================================
// Section 2: MeshVertexView Component Defaults
// =============================================================================

TEST(MeshViewLifecycle_Contract, VertexViewDefaultState)
{
    ECS::MeshVertexView::Component comp;

    EXPECT_FALSE(comp.Geometry.IsValid());
    EXPECT_EQ(comp.GpuSlot, ECS::MeshVertexView::Component::kInvalidSlot);
    EXPECT_EQ(comp.VertexCount, 0u);
    EXPECT_TRUE(comp.Dirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(MeshViewLifecycle_Contract, VertexViewInvalidSlotSentinel)
{
    EXPECT_EQ(ECS::MeshVertexView::Component::kInvalidSlot, ~0u);
    EXPECT_EQ(ECS::MeshVertexView::Component::kInvalidSlot,
              ECS::MeshRenderer::Component::kInvalidSlot);
}

TEST(MeshViewLifecycle_Contract, VertexViewHasGpuGeometryTrueWhenValid)
{
    ECS::MeshVertexView::Component comp;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    EXPECT_TRUE(comp.HasGpuGeometry());
}

// =============================================================================
// Section 3: Dirty Lifecycle Transitions
// =============================================================================

TEST(MeshViewLifecycle_Contract, EdgeViewStartsDirty)
{
    // When first attached, component is Dirty=true so the lifecycle
    // system knows to create the edge index buffer.
    ECS::MeshEdgeView::Component comp;
    EXPECT_TRUE(comp.Dirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(MeshViewLifecycle_Contract, EdgeViewDirtyClearedAfterCreation)
{
    // Simulate lifecycle clearing Dirty after successful creation.
    ECS::MeshEdgeView::Component comp;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    comp.EdgeCount = 42;
    comp.Dirty = false;

    EXPECT_FALSE(comp.Dirty);
    EXPECT_TRUE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.EdgeCount, 42u);
}

TEST(MeshViewLifecycle_Contract, VertexViewStartsDirty)
{
    ECS::MeshVertexView::Component comp;
    EXPECT_TRUE(comp.Dirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(MeshViewLifecycle_Contract, VertexViewDirtyClearedAfterCreation)
{
    ECS::MeshVertexView::Component comp;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    comp.VertexCount = 1024;
    comp.Dirty = false;

    EXPECT_FALSE(comp.Dirty);
    EXPECT_TRUE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.VertexCount, 1024u);
}

// =============================================================================
// Section 4: Edge Index Flattening Contract
// =============================================================================

TEST(MeshViewLifecycle_Contract, EdgePairFlatteningLayout)
{
    // Edge pairs are flattened to contiguous uint32_t indices:
    //   [i0_0, i1_0, i0_1, i1_1, ...]
    // This layout is compatible with both:
    //   - Lines topology index buffer (pairs of indices)
    //   - BDA EdgePair reads (struct { uint32_t i0, i1; })
    using EdgePair = ECS::RenderVisualization::EdgePair;

    std::vector<EdgePair> edges = {{0, 1}, {2, 3}, {4, 5}};

    std::vector<uint32_t> flattened;
    flattened.reserve(edges.size() * 2);
    for (const auto& [i0, i1] : edges)
    {
        flattened.push_back(i0);
        flattened.push_back(i1);
    }

    EXPECT_EQ(flattened.size(), 6u);
    EXPECT_EQ(flattened[0], 0u); // Edge 0 start
    EXPECT_EQ(flattened[1], 1u); // Edge 0 end
    EXPECT_EQ(flattened[2], 2u); // Edge 1 start
    EXPECT_EQ(flattened[3], 3u); // Edge 1 end
    EXPECT_EQ(flattened[4], 4u); // Edge 2 start
    EXPECT_EQ(flattened[5], 5u); // Edge 2 end

    // Verify reinterpret-cast compatibility with EdgePair.
    static_assert(sizeof(EdgePair) == 2 * sizeof(uint32_t));
    static_assert(alignof(EdgePair) <= alignof(uint32_t));
}

// =============================================================================
// Section 5: Upload Request Construction
// =============================================================================

TEST(MeshViewLifecycle_Contract, EdgeViewUploadRequestConstruction)
{
    // Simulate what MeshViewLifecycleSystem does: build a GeometryUploadRequest
    // for an edge view using ReuseVertexBuffersFrom.
    using EdgePair = ECS::RenderVisualization::EdgePair;

    Geometry::GeometryHandle meshHandle(0, 1);
    std::vector<EdgePair> cachedEdges = {{0, 1}, {1, 2}, {2, 0}};

    std::vector<uint32_t> indices;
    indices.reserve(cachedEdges.size() * 2);
    for (const auto& [i0, i1] : cachedEdges)
    {
        indices.push_back(i0);
        indices.push_back(i1);
    }

    Graphics::GeometryUploadRequest req{};
    req.ReuseVertexBuffersFrom = meshHandle;
    req.Indices = indices;
    req.Topology = Graphics::PrimitiveTopology::Lines;
    req.UploadMode = Graphics::GeometryUploadMode::Direct;

    EXPECT_TRUE(req.ReuseVertexBuffersFrom.IsValid());
    EXPECT_EQ(req.Indices.size(), 6u);
    EXPECT_EQ(req.Topology, Graphics::PrimitiveTopology::Lines);
    EXPECT_EQ(req.UploadMode, Graphics::GeometryUploadMode::Direct);
    EXPECT_TRUE(req.Positions.empty());
    EXPECT_TRUE(req.Normals.empty());
}

TEST(MeshViewLifecycle_Contract, VertexViewUploadRequestConstruction)
{
    // Vertex view: ReuseVertexBuffersFrom with Points topology, no indices.
    Geometry::GeometryHandle meshHandle(0, 1);

    Graphics::GeometryUploadRequest req{};
    req.ReuseVertexBuffersFrom = meshHandle;
    req.Topology = Graphics::PrimitiveTopology::Points;
    req.UploadMode = Graphics::GeometryUploadMode::Direct;

    EXPECT_TRUE(req.ReuseVertexBuffersFrom.IsValid());
    EXPECT_TRUE(req.Indices.empty());
    EXPECT_EQ(req.Topology, Graphics::PrimitiveTopology::Points);
    EXPECT_TRUE(req.Positions.empty());
    EXPECT_TRUE(req.Normals.empty());
}

// =============================================================================
// Section 6: Slot Lifecycle Contract
// =============================================================================

TEST(MeshViewLifecycle_Contract, EdgeViewSlotLifecycle)
{
    // Simulate full lifecycle: attach → create → slot → destroy.
    ECS::MeshEdgeView::Component comp;

    // Phase 1: Newly attached (Dirty, no geometry, no slot).
    EXPECT_TRUE(comp.Dirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.GpuSlot, ECS::MeshEdgeView::Component::kInvalidSlot);

    // Phase 2: Lifecycle system creates geometry and allocates slot.
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    comp.EdgeCount = 150;
    comp.GpuSlot = 42;
    comp.Dirty = false;

    EXPECT_FALSE(comp.Dirty);
    EXPECT_TRUE(comp.HasGpuGeometry());
    EXPECT_NE(comp.GpuSlot, ECS::MeshEdgeView::Component::kInvalidSlot);

    // Phase 3: On destroy hook frees slot.
    comp.GpuSlot = ECS::MeshEdgeView::Component::kInvalidSlot;
    EXPECT_EQ(comp.GpuSlot, ECS::MeshEdgeView::Component::kInvalidSlot);
}

TEST(MeshViewLifecycle_Contract, VertexViewSlotLifecycle)
{
    ECS::MeshVertexView::Component comp;

    EXPECT_TRUE(comp.Dirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.GpuSlot, ECS::MeshVertexView::Component::kInvalidSlot);

    comp.Geometry = Geometry::GeometryHandle(0, 1);
    comp.VertexCount = 1024;
    comp.GpuSlot = 7;
    comp.Dirty = false;

    EXPECT_FALSE(comp.Dirty);
    EXPECT_TRUE(comp.HasGpuGeometry());
    EXPECT_NE(comp.GpuSlot, ECS::MeshVertexView::Component::kInvalidSlot);

    comp.GpuSlot = ECS::MeshVertexView::Component::kInvalidSlot;
    EXPECT_EQ(comp.GpuSlot, ECS::MeshVertexView::Component::kInvalidSlot);
}

// =============================================================================
// Section 7: Cross-Component Consistency
// =============================================================================

TEST(MeshViewLifecycle_Contract, EdgeAndVertexViewsIndependent)
{
    // Edge view and vertex view on the same entity are independent:
    // they have separate GeometryHandles, separate GPUScene slots,
    // and separate dirty flags.
    ECS::MeshEdgeView::Component ev;
    ECS::MeshVertexView::Component pv;

    ev.Geometry = Geometry::GeometryHandle(0, 1);
    ev.GpuSlot = 10;
    ev.EdgeCount = 42;
    ev.Dirty = false;

    pv.Geometry = Geometry::GeometryHandle(1, 1);
    pv.GpuSlot = 11;
    pv.VertexCount = 100;
    pv.Dirty = false;

    EXPECT_NE(ev.Geometry.Index, pv.Geometry.Index);
    EXPECT_NE(ev.GpuSlot, pv.GpuSlot);
    EXPECT_TRUE(ev.HasGpuGeometry());
    EXPECT_TRUE(pv.HasGpuGeometry());
}

TEST(MeshViewLifecycle_Contract, ReuseVertexBuffersFromSharedHandle)
{
    // Both edge and vertex views reference the same source mesh handle
    // via ReuseVertexBuffersFrom. Verify the upload requests can coexist.
    Geometry::GeometryHandle meshHandle(0, 1);

    Graphics::GeometryUploadRequest edgeReq{};
    edgeReq.ReuseVertexBuffersFrom = meshHandle;
    edgeReq.Topology = Graphics::PrimitiveTopology::Lines;

    Graphics::GeometryUploadRequest vtxReq{};
    vtxReq.ReuseVertexBuffersFrom = meshHandle;
    vtxReq.Topology = Graphics::PrimitiveTopology::Points;

    EXPECT_EQ(edgeReq.ReuseVertexBuffersFrom, vtxReq.ReuseVertexBuffersFrom);
    EXPECT_NE(edgeReq.Topology, vtxReq.Topology);
}

// =============================================================================
// Section 8: Render Pass Wiring Contract
// =============================================================================
//
// These tests validate the CPU-side contract for how retained render passes
// consume MeshEdgeView and MeshVertexView geometry:
//
//   - LinePass requires a valid EdgeView geometry handle (BDA index buffer)
//     for all retained edge sources. MeshViewLifecycleSystem auto-attaches
//     MeshEdgeView when ShowWireframe=true and creates the edge index buffer
//     from collision data. GraphGeometrySyncSystem creates edge index buffers
//     via ReuseVertexBuffersFrom. No LinePass-internal fallback buffers.
//
//   - RetainedPointCloudRenderPass prefers MeshVertexView::Geometry when
//     available (vertex buffer BDA from GeometryGpuData), falling back to
//     direct MeshRenderer::Geometry lookup.
//
//   - Edge aux (per-edge color) buffers remain internally managed since
//     MeshEdgeView doesn't carry attribute data.

TEST(MeshViewLifecycle_Contract, EdgeViewReadyForRenderPass)
{
    // Simulate a MeshEdgeView that has completed lifecycle setup.
    // LinePass reads edge BDA from this geometry's index buffer.
    ECS::MeshEdgeView::Component ev;
    ev.Geometry = Geometry::GeometryHandle(0, 1);
    ev.EdgeCount = 100;
    ev.GpuSlot = 5;
    ev.Dirty = false;

    // The render pass checks: HasGpuGeometry() && EdgeCount > 0
    EXPECT_TRUE(ev.HasGpuGeometry());
    EXPECT_GT(ev.EdgeCount, 0u);
}

TEST(MeshViewLifecycle_Contract, EdgeViewNotReadySkipsRendering)
{
    // When MeshEdgeView is present but Dirty (not yet created),
    // LinePass skips the entity (no fallback — EdgeView must be valid).
    ECS::MeshEdgeView::Component ev;

    // Default state: Dirty=true, no geometry
    EXPECT_FALSE(ev.HasGpuGeometry());
    EXPECT_EQ(ev.EdgeCount, 0u);
    EXPECT_TRUE(ev.Dirty);
}

TEST(MeshViewLifecycle_Contract, VertexViewReadyForRenderPass)
{
    // Simulate a MeshVertexView that has completed lifecycle setup.
    // RetainedPointCloudRenderPass should prefer this over direct
    // MeshRenderer::Geometry lookup.
    ECS::MeshVertexView::Component pv;
    pv.Geometry = Geometry::GeometryHandle(1, 1);
    pv.VertexCount = 512;
    pv.GpuSlot = 12;
    pv.Dirty = false;

    // The render pass checks: HasGpuGeometry() && VertexCount > 0
    EXPECT_TRUE(pv.HasGpuGeometry());
    EXPECT_GT(pv.VertexCount, 0u);
}

TEST(MeshViewLifecycle_Contract, VertexViewNotReadyFallsBack)
{
    // When MeshVertexView is present but not ready, the render pass
    // should fall back to direct MeshRenderer::Geometry.
    ECS::MeshVertexView::Component pv;

    EXPECT_FALSE(pv.HasGpuGeometry());
    EXPECT_EQ(pv.VertexCount, 0u);
    EXPECT_TRUE(pv.Dirty);
}

TEST(MeshViewLifecycle_Contract, EdgeViewIndexBufferBDACompatible)
{
    // The MeshEdgeView's GeometryGpuData index buffer is created via
    // GeometryUploadRequest with Indices = flattened edge pairs and
    // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT. The layout is:
    //   [i0_0, i1_0, i0_1, i1_1, ...]
    // which is binary-compatible with EdgePair { uint32_t i0, i1; }.
    //
    // LinePass reads this via PtrEdges BDA pointer, and the
    // vertex shader interprets it as EdgePair structs (sizeof == 8).
    using EdgePair = ECS::RenderVisualization::EdgePair;

    // Verify the flattened layout is reinterpret-safe.
    static_assert(sizeof(EdgePair) == 2 * sizeof(uint32_t));
    static_assert(alignof(EdgePair) <= alignof(uint32_t));

    // The edge count from MeshEdgeView::EdgeCount is in edge pairs, not indices.
    // LinePass uses EdgeCount * 6 for vkCmdDraw (6 verts per edge).
    constexpr uint32_t edgeCount = 42;
    constexpr uint32_t gpuVertices = edgeCount * 6;
    EXPECT_EQ(gpuVertices, 252u);
}

TEST(MeshViewLifecycle_Contract, EdgeAuxBuffersIndependentOfEdgeView)
{
    // Per-edge color attribute buffers (CachedEdgeColors) are still managed
    // internally by LinePass even when MeshEdgeView is used
    // for the edge index buffer. This is because MeshEdgeView only carries
    // the index buffer — attribute channels are not part of the geometry view.
    ECS::RenderVisualization::Component viz;
    viz.CachedEdges = {{0, 1}, {1, 2}, {2, 0}};
    viz.CachedEdgeColors = {0xFF0000FF, 0xFF00FF00, 0xFFFF0000};
    viz.EdgeColorsDirty = false;

    ECS::MeshEdgeView::Component ev;
    ev.Geometry = Geometry::GeometryHandle(0, 1);
    ev.EdgeCount = static_cast<uint32_t>(viz.CachedEdges.size());
    ev.Dirty = false;

    // Edge view has geometry, but does not carry per-edge colors.
    EXPECT_TRUE(ev.HasGpuGeometry());
    EXPECT_EQ(ev.EdgeCount, viz.CachedEdgeColors.size());
    // The render pass must still use EnsureEdgeAuxBuffer for colors.
}
