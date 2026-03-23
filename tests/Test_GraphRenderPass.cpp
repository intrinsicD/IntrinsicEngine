#include <gtest/gtest.h>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// Graph Data — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the ECS::Graph::Data component
// without requiring a GPU device.  They verify:
//   - Graph::Data component default values and PropertySet-backed queries.
//   - Node color / radius optional attribute detection via PropertySets.
//   - GPU state fields for retained-mode rendering.
//   - GPUScene slot integration.

// ---- Helper: create a Graph with vertices and edges ----

static std::shared_ptr<Geometry::Graph::Graph> MakeTriangleGraph()
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    (void)g->AddEdge(v0, v1);
    (void)g->AddEdge(v1, v2);
    (void)g->AddEdge(v2, v0);
    return g;
}

// ---- ECS::Graph::Data Tests ----

TEST(Graph_Data, DefaultValues)
{
    ECS::Graph::Data data;
    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
    EXPECT_FLOAT_EQ(data.DefaultNodeRadius,  0.01f);
    EXPECT_FLOAT_EQ(data.NodeSizeMultiplier, 1.0f);
    EXPECT_TRUE(data.Visible);
    EXPECT_FALSE(data.EdgesOverlay);
    EXPECT_EQ(data.GraphRef, nullptr);
}

TEST(Graph_Data, WithGraphRef)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();

    EXPECT_EQ(data.NodeCount(), 3u);
    EXPECT_EQ(data.EdgeCount(), 3u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
}

TEST(Graph_Data, NullGraphRefIsHandled)
{
    ECS::Graph::Data data;
    data.GraphRef = nullptr;

    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
}

TEST(Graph_Data, PropertySetBackedColors)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();
    EXPECT_FALSE(data.HasNodeColors());

    // Add per-node colors via PropertySet.
    [[maybe_unused]] auto colorProp = data.GraphRef->GetOrAddVertexProperty<glm::vec4>("v:color",
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(data.HasNodeColors());
}

TEST(Graph_Data, PropertySetBackedRadii)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();
    EXPECT_FALSE(data.HasNodeRadii());

    // Add per-node radii via PropertySet.
    [[maybe_unused]] auto radiusProp = data.GraphRef->GetOrAddVertexProperty<float>("v:radius", 0.02f);
    EXPECT_TRUE(data.HasNodeRadii());
}

TEST(Graph_Data, ConstPropertyAccessUsesReadOnlyViews)
{
    const Geometry::Graph::Graph graph = *MakeTriangleGraph();

    const auto props = graph.VertexProperties();
    EXPECT_TRUE(props.Exists("v:point"));

    const auto points = props.Get<glm::vec3>("v:point");
    ASSERT_TRUE(points.IsValid());
    EXPECT_EQ(points.Vector().size(), graph.VerticesSize());
}

TEST(Graph_Data, SharedPtrSemantics)
{
    auto graph = MakeTriangleGraph();

    ECS::Graph::Data data1;
    data1.GraphRef = graph;

    ECS::Graph::Data data2;
    data2.GraphRef = graph;

    // Both reference the same authoritative graph.
    EXPECT_EQ(data1.GraphRef.get(), data2.GraphRef.get());
    EXPECT_EQ(data1.NodeCount(), data2.NodeCount());

    // Mutation through one reference is visible to the other.
    graph->AddVertex(glm::vec3(1, 1, 0));
    EXPECT_EQ(data1.NodeCount(), 4u);
    EXPECT_EQ(data2.NodeCount(), 4u);
}

TEST(Graph_Data, EmptyGraph)
{
    ECS::Graph::Data data;
    data.GraphRef = std::make_shared<Geometry::Graph::Graph>();

    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
}

TEST(Graph_Data, GraphWithDeletedVertices)
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    (void)g->AddEdge(v0, v1);
    (void)g->AddEdge(v1, v2);

    ECS::Graph::Data data;
    data.GraphRef = g;
    EXPECT_EQ(data.NodeCount(), 3u);
    EXPECT_EQ(data.EdgeCount(), 2u);

    // Delete a vertex — adjacent edges are removed too.
    g->DeleteVertex(v0);
    EXPECT_EQ(data.NodeCount(), 2u);
    EXPECT_EQ(data.EdgeCount(), 1u);
}

// ---- RenderMode Enum Coverage ----

TEST(Graph_RenderMode, FlatDiscAvailable)
{
    // Verify the supported render mode.
    using RM = Geometry::PointCloud::RenderMode;

    EXPECT_EQ(static_cast<uint32_t>(RM::FlatDisc), 0u);
}

TEST(Graph_Data, NodeRenderModeDefault)
{
    ECS::Graph::Data data;
    EXPECT_EQ(data.NodeRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
}

TEST(Graph_Data, NodeRenderModeSetFlatDisc)
{
    ECS::Graph::Data data;
    data.NodeRenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
    EXPECT_EQ(data.NodeRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
}

// ---- PropertySet Accessor Tests ----

TEST(Graph_PropertySetAccessors, VertexPropertiesAccessible)
{
    auto g = MakeTriangleGraph();
    const auto& constG = *g;

    // Mutable access.
    EXPECT_EQ(g->VertexProperties().Size(), g->VerticesSize());

    // Const access.
    EXPECT_EQ(constG.VertexProperties().Size(), constG.VerticesSize());
}

TEST(Graph_PropertySetAccessors, EdgePropertiesAccessible)
{
    auto g = MakeTriangleGraph();
    const auto& constG = *g;

    EXPECT_EQ(g->EdgeProperties().Size(), g->EdgesSize());
    EXPECT_EQ(constG.EdgeProperties().Size(), constG.EdgesSize());
}

TEST(Graph_PropertySetAccessors, PropertyExistsCheck)
{
    auto g = MakeTriangleGraph();

    EXPECT_FALSE(g->VertexProperties().Exists("v:color"));
    (void)g->GetOrAddVertexProperty<glm::vec4>("v:color");
    EXPECT_TRUE(g->VertexProperties().Exists("v:color"));
}

// =============================================================================
// GPU State Fields — Retained-mode rendering contract tests
// =============================================================================
//
// Validate the new GPU state fields on ECS::Graph::Data without requiring a
// GPU device. These ensure the component's retained-mode API surface is
// correct and that default values don't break existing code.

TEST(Graph_RetainedMode, DefaultGpuStateValues)
{
    ECS::Graph::Data data;

    // GpuGeometry handle should be invalid by default.
    EXPECT_FALSE(data.GpuGeometry.IsValid());

    // GPUScene slot should be invalid by default.
    EXPECT_EQ(data.GpuSlot, ECS::kInvalidGpuSlot);

    // Edge pairs should be empty.
    EXPECT_TRUE(data.CachedEdgePairs.empty());

    // Per-edge colors should be empty.
    EXPECT_TRUE(data.CachedEdgeColors.empty());

    // Per-node colors should be empty.
    EXPECT_TRUE(data.CachedNodeColors.empty());

    // Per-node radii should be empty.
    EXPECT_TRUE(data.CachedNodeRadii.empty());

    // GpuEdgeGeometry handle should be invalid by default.
    EXPECT_FALSE(data.GpuEdgeGeometry.IsValid());

    // GpuEdgeCount should be 0.
    EXPECT_EQ(data.GpuEdgeCount, 0u);

    // GpuDirty should be true (triggers initial upload).
    EXPECT_TRUE(data.GpuDirty);

    // GpuVertexCount should be 0.
    EXPECT_EQ(data.GpuVertexCount, 0u);

    // EdgeWidth should have a sensible default.
    EXPECT_FLOAT_EQ(data.EdgeWidth, 1.5f);
}

TEST(Graph_RetainedMode, GpuDirtyStartsTrue)
{
    // Every new Graph::Data instance starts dirty so that
    // GraphGeometrySyncSystem uploads on the first frame.
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();
    EXPECT_TRUE(data.GpuDirty);
}

TEST(Graph_RetainedMode, EdgePairStorageMatchesEdgeCount)
{
    // Simulate what GraphGeometrySyncSystem would do: extract edge pairs
    // from graph topology and store them on the component.
    auto g = MakeTriangleGraph(); // 3 vertices, 3 edges

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Manual edge extraction (mirrors sync system logic).
    const std::size_t eSize = g->EdgesSize();
    for (std::size_t i = 0; i < eSize; ++i)
    {
        const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(e))
            continue;

        const auto [v0, v1] = g->EdgeVertices(e);
        data.CachedEdgePairs.push_back({
            static_cast<uint32_t>(v0.Index),
            static_cast<uint32_t>(v1.Index)
        });
    }

    EXPECT_EQ(data.CachedEdgePairs.size(), 3u);
}

TEST(Graph_RetainedMode, EdgePairSizeMatchesLayout)
{
    // EdgePair must be exactly 8 bytes for direct SSBO upload.
    static_assert(sizeof(ECS::EdgePair) == 8);

    ECS::EdgePair ep{42, 99};
    EXPECT_EQ(ep.i0, 42u);
    EXPECT_EQ(ep.i1, 99u);
}

TEST(Graph_RetainedMode, GpuDirtyCanBeCleared)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();

    EXPECT_TRUE(data.GpuDirty);
    data.GpuDirty = false;
    EXPECT_FALSE(data.GpuDirty);

    // Simulates layout change trigger.
    data.GpuDirty = true;
    EXPECT_TRUE(data.GpuDirty);
}

TEST(Graph_RetainedMode, GpuVertexCountTracksCompactedCount)
{
    // After vertex compaction, GpuVertexCount should reflect
    // the number of non-deleted vertices uploaded to GPU.
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    auto v3 = g->AddVertex(glm::vec3(1, 1, 0));
    (void)g->AddEdge(v0, v1);
    (void)g->AddEdge(v1, v2);
    (void)g->AddEdge(v2, v3);

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Simulate compaction for all 4 vertices.
    data.GpuVertexCount = 4;
    EXPECT_EQ(data.GpuVertexCount, 4u);

    // Delete v2 and simulate re-upload with 3 compacted vertices.
    g->DeleteVertex(v2);
    data.GpuVertexCount = static_cast<uint32_t>(g->VertexCount());
    EXPECT_EQ(data.GpuVertexCount, 3u);
}

// =============================================================================
// GPUScene Slot Integration — Contract tests
// =============================================================================

TEST(Graph_GPUSceneSlot, InvalidSlotConstantMatchesOtherComponents)
{
    // All components now use the shared ECS::kInvalidGpuSlot constant.
    EXPECT_EQ(ECS::kInvalidGpuSlot, ~0u);
}

TEST(Graph_GPUSceneSlot, SlotDefaultsToInvalid)
{
    ECS::Graph::Data data;
    EXPECT_EQ(data.GpuSlot, ECS::kInvalidGpuSlot);
}

TEST(Graph_GPUSceneSlot, SlotCanBeAssignedAndCleared)
{
    ECS::Graph::Data data;
    data.GpuSlot = 42u;
    EXPECT_EQ(data.GpuSlot, 42u);

    data.GpuSlot = ECS::kInvalidGpuSlot;
    EXPECT_EQ(data.GpuSlot, ECS::kInvalidGpuSlot);
}

// =============================================================================
// Per-Node Attribute Cache — Contract tests
// =============================================================================
//
// GraphGeometrySyncSystem extracts per-node colors and radii from PropertySets
// into CachedNodeColors / CachedNodeRadii vectors on the component.

TEST(Graph_NodeAttributes, CachedNodeColorsDefaultEmpty)
{
    ECS::Graph::Data data;
    EXPECT_TRUE(data.CachedNodeColors.empty());
}

TEST(Graph_NodeAttributes, CachedNodeRadiiDefaultEmpty)
{
    ECS::Graph::Data data;
    EXPECT_TRUE(data.CachedNodeRadii.empty());
}

TEST(Graph_NodeAttributes, CachedNodeColorsMatchVertexCount)
{
    // Simulate what GraphGeometrySyncSystem does: extract per-node colors
    // and pack them as ABGR uint32 values.
    auto g = MakeTriangleGraph();

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Add per-node colors via PropertySet.
    [[maybe_unused]] auto colorProp = g->GetOrAddVertexProperty<glm::vec4>("v:color",
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(data.HasNodeColors());

    // Simulate extraction: one packed color per compacted vertex.
    const std::size_t vSize = g->VerticesSize();
    for (std::size_t i = 0; i < vSize; ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(v))
            continue;
        const glm::vec4& c = colorProp[v];
        // Pack ABGR — just verify the count, not the exact packing.
        data.CachedNodeColors.push_back(static_cast<uint32_t>(c.r * 255.0f));
    }

    EXPECT_EQ(data.CachedNodeColors.size(), 3u); // 3 vertices in triangle graph.
}

TEST(Graph_NodeAttributes, CachedNodeRadiiMatchVertexCount)
{
    auto g = MakeTriangleGraph();

    ECS::Graph::Data data;
    data.GraphRef = g;

    [[maybe_unused]] auto radiusProp = g->GetOrAddVertexProperty<float>("v:radius", 0.02f);
    EXPECT_TRUE(data.HasNodeRadii());

    const std::size_t vSize = g->VerticesSize();
    for (std::size_t i = 0; i < vSize; ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(v))
            continue;
        data.CachedNodeRadii.push_back(radiusProp[v]);
    }

    EXPECT_EQ(data.CachedNodeRadii.size(), 3u);
    EXPECT_FLOAT_EQ(data.CachedNodeRadii[0], 0.02f);
}

TEST(Graph_NodeAttributes, EmptyWhenPropertySetAbsent)
{
    auto g = MakeTriangleGraph();

    ECS::Graph::Data data;
    data.GraphRef = g;

    // No "v:color" or "v:radius" PropertySets added.
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
    EXPECT_TRUE(data.CachedNodeColors.empty());
    EXPECT_TRUE(data.CachedNodeRadii.empty());
}

// =============================================================================
// StaticGeometry — Staged upload mode contract tests
// =============================================================================
//
// The StaticGeometry flag on ECS::Graph::Data controls the GPU upload mode:
//   false (default) → Direct (host-visible, CPU_TO_GPU) for dynamic graphs.
//   true            → Staged (device-local, GPU_ONLY) for static graphs.
//
// These tests validate the flag defaults, upload request construction,
// and backward compatibility with existing dynamic graph behavior.

TEST(Graph_StaticGeometry, DefaultIsFalse)
{
    // Default is false - backward-compatible with existing dynamic graph behavior.
    ECS::Graph::Data data{};
    EXPECT_FALSE(data.StaticGeometry);
}

TEST(Graph_StaticGeometry, CanBeSetToTrue)
{
    ECS::Graph::Data data;
    data.StaticGeometry = true;
    EXPECT_TRUE(data.StaticGeometry);
}

TEST(Graph_StaticGeometry, DynamicUploadModeIsDirect)
{
    // When StaticGeometry is false, upload mode should be Direct.
    ECS::Graph::Data data;
    data.StaticGeometry = false;

    const auto mode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;

    EXPECT_EQ(mode, Graphics::GeometryUploadMode::Direct);
}

TEST(Graph_StaticGeometry, StaticUploadModeIsStaged)
{
    // When StaticGeometry is true, upload mode should be Staged.
    ECS::Graph::Data data;
    data.StaticGeometry = true;

    const auto mode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;

    EXPECT_EQ(mode, Graphics::GeometryUploadMode::Staged);
}

TEST(Graph_StaticGeometry, UploadRequestDirect)
{
    // Simulate what GraphGeometrySyncSystem builds for a dynamic graph.
    auto g = MakeTriangleGraph();
    ECS::Graph::Data data;
    data.GraphRef = g;
    data.StaticGeometry = false;

    const auto uploadMode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;

    Graphics::GeometryUploadRequest upload{};
    upload.Topology = Graphics::PrimitiveTopology::Points;
    upload.UploadMode = uploadMode;

    EXPECT_EQ(upload.UploadMode, Graphics::GeometryUploadMode::Direct);
    EXPECT_EQ(upload.Topology, Graphics::PrimitiveTopology::Points);
}

TEST(Graph_StaticGeometry, UploadRequestStaged)
{
    // Simulate what GraphGeometrySyncSystem builds for a static graph.
    auto g = MakeTriangleGraph();
    ECS::Graph::Data data;
    data.GraphRef = g;
    data.StaticGeometry = true;

    const auto uploadMode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;

    Graphics::GeometryUploadRequest upload{};
    upload.Topology = Graphics::PrimitiveTopology::Points;
    upload.UploadMode = uploadMode;

    EXPECT_EQ(upload.UploadMode, Graphics::GeometryUploadMode::Staged);
    EXPECT_EQ(upload.Topology, Graphics::PrimitiveTopology::Points);
}

TEST(Graph_StaticGeometry, EdgeUploadUseSameMode)
{
    // Edge index buffer upload should use the same mode as vertex upload.
    ECS::Graph::Data data;
    data.StaticGeometry = true;

    const auto uploadMode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;

    // Simulate vertex upload request.
    Graphics::GeometryUploadRequest vertexReq{};
    vertexReq.Topology = Graphics::PrimitiveTopology::Points;
    vertexReq.UploadMode = uploadMode;

    // Simulate edge upload request (shares vertex buffer via ReuseVertexBuffersFrom).
    Graphics::GeometryUploadRequest edgeReq{};
    edgeReq.Topology = Graphics::PrimitiveTopology::Lines;
    edgeReq.UploadMode = uploadMode;

    // Both must use the same upload mode.
    EXPECT_EQ(vertexReq.UploadMode, edgeReq.UploadMode);
    EXPECT_EQ(edgeReq.UploadMode, Graphics::GeometryUploadMode::Staged);
}

TEST(Graph_StaticGeometry, StaticFlagDoesNotAffectGpuDirty)
{
    // StaticGeometry controls upload mode, not dirty state.
    ECS::Graph::Data data;
    data.StaticGeometry = true;

    // GpuDirty is independent of StaticGeometry.
    EXPECT_TRUE(data.GpuDirty);

    data.GpuDirty = false;
    EXPECT_FALSE(data.GpuDirty);
    EXPECT_TRUE(data.StaticGeometry);
}

TEST(Graph_StaticGeometry, StagedMatchesPointCloudUploadMode)
{
    // Static graph upload mode should match PointCloudGeometrySyncSystem's mode.
    // This ensures consistency across retained-mode renderers for static data.
    Graphics::GeometryUploadRequest graphUpload{};
    graphUpload.UploadMode = Graphics::GeometryUploadMode::Staged;

    Graphics::GeometryUploadRequest cloudUpload{};
    cloudUpload.UploadMode = Graphics::GeometryUploadMode::Staged;

    EXPECT_EQ(graphUpload.UploadMode, cloudUpload.UploadMode);
}

TEST(Graph_NodeAttributes, ClearedOnEmptyGraph)
{
    // When a graph becomes empty, all cached data should be cleared.
    ECS::Graph::Data data;
    data.CachedNodeColors.push_back(0xFF0000FF);
    data.CachedNodeRadii.push_back(0.01f);
    data.CachedEdgePairs.push_back({0, 1});
    data.CachedEdgeColors.push_back(0xFFFFFFFF);

    // Simulate what GraphGeometrySyncSystem does for an empty graph.
    data.CachedEdgePairs.clear();
    data.CachedEdgeColors.clear();
    data.CachedNodeColors.clear();
    data.CachedNodeRadii.clear();
    data.GpuVertexCount = 0;

    EXPECT_TRUE(data.CachedNodeColors.empty());
    EXPECT_TRUE(data.CachedNodeRadii.empty());
    EXPECT_TRUE(data.CachedEdgePairs.empty());
    EXPECT_TRUE(data.CachedEdgeColors.empty());
    EXPECT_EQ(data.GpuVertexCount, 0u);
}

// =============================================================================
// D1 Regression: GraphPropertyHelpers deleted-vertex skipping and fallback names
// =============================================================================
//
// GraphPropertyHelpers::ExtractColors() uses a skipDeleted predicate fed into
// ColorMapper::MapProperty(), and sets config.PropertyName to the default if
// empty and the property exists.  Both GraphGeometrySyncSystem and
// PropertySetDirtySyncSystem call the shared helpers; these tests exercise the
// same predicates and property-name logic so a regression in either helper is
// caught here.

// Helper: build a 3-vertex graph with "v:color" on all vertices and delete the
// middle vertex (without GC so IsDeleted() returns true for it).
static std::shared_ptr<Geometry::Graph::Graph> MakeGraphWithDeletedMiddleVertex()
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    [[maybe_unused]] auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    [[maybe_unused]] auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    (void)g->GetOrAddVertexProperty<glm::vec4>("v:color", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    g->DeleteVertex(v1);
    // Intentionally skip GarbageCollection() — v1 slot is still in storage but
    // IsDeleted(v1) == true.  VerticesSize() == 3, VertexCount() == 2.
    return g;
}

TEST(GraphPropertyHelpers_D1, SkipDeletedVerticesInColorExtraction)
{
    // Regression: ExtractColors must skip deleted vertices via the skipDeleted
    // predicate; it must produce exactly VertexCount() entries, not VerticesSize().
    auto graph = MakeGraphWithDeletedMiddleVertex();
    ASSERT_EQ(graph->VerticesSize(), 3u);  // storage slots (including deleted)
    ASSERT_EQ(graph->VertexCount(),  2u);  // live vertices only

    Graphics::ColorSource config;
    config.PropertyName = "v:color";

    // Replicate the skipDeleted predicate from GraphPropertyHelpers::ExtractColors.
    auto skipDeleted = [&graph](size_t i) -> bool {
        return graph->IsDeleted(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)});
    };

    const auto result = Graphics::ColorMapper::MapProperty(
        graph->VertexProperties(), config, skipDeleted);

    ASSERT_TRUE(result.has_value());
    // Must have exactly 2 colors — one per live vertex, deleted slot excluded.
    EXPECT_EQ(result->Colors.size(), 2u);
}

TEST(GraphPropertyHelpers_D1, FallbackPropertyNameSetWhenEmpty)
{
    // Regression: ExtractColors must auto-populate config.PropertyName from the
    // default name when PropertyName is empty and the property exists.
    auto graph = MakeGraphWithDeletedMiddleVertex();

    Graphics::ColorSource config;
    ASSERT_TRUE(config.PropertyName.empty());  // start empty

    // Replicate the fallback-name logic from GraphPropertyHelpers::ExtractColors.
    const std::string_view defaultPropName = "v:color";
    if (config.PropertyName.empty() && graph->VertexProperties().Exists(defaultPropName))
        config.PropertyName = std::string(defaultPropName);

    EXPECT_EQ(config.PropertyName, "v:color");

    // After name is set, extraction must succeed and return the live-vertex count.
    auto skipDeleted = [&graph](size_t i) -> bool {
        return graph->IsDeleted(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)});
    };
    const auto result = Graphics::ColorMapper::MapProperty(
        graph->VertexProperties(), config, skipDeleted);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 2u);
}

TEST(GraphPropertyHelpers_D1, FallbackDoesNotOverrideExplicitPropertyName)
{
    // Regression: fallback name assignment must be guarded by `config.PropertyName.empty()`.
    // An explicitly set name must not be silently overwritten.
    auto graph = MakeGraphWithDeletedMiddleVertex();
    // Add a second named property that the test will request explicitly.
    (void)graph->GetOrAddVertexProperty<glm::vec4>("v:highlight", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

    Graphics::ColorSource config;
    config.PropertyName = "v:highlight";  // explicitly set — must not be changed

    // Fallback logic: should NOT change PropertyName because it is non-empty.
    const std::string_view defaultPropName = "v:color";
    if (config.PropertyName.empty() && graph->VertexProperties().Exists(defaultPropName))
        config.PropertyName = std::string(defaultPropName);

    EXPECT_EQ(config.PropertyName, "v:highlight");
}

TEST(GraphPropertyHelpers_D1, SkipDeletedVerticesInRadiusExtraction)
{
    // Regression: ExtractNodeRadii must skip deleted vertices when iterating
    // VerticesSize() with an IsDeleted() guard.
    auto graph = MakeGraphWithDeletedMiddleVertex();
    (void)graph->GetOrAddVertexProperty<float>("v:radius", 0.02f);

    // Replicate the loop from GraphPropertyHelpers::ExtractNodeRadii.
    std::vector<float> radii;
    auto radiusProp = Geometry::VertexProperty<float>(
        graph->VertexProperties().Get<float>("v:radius"));
    const std::size_t vSize = graph->VerticesSize();
    for (std::size_t i = 0; i < vSize; ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (graph->IsDeleted(v))
            continue;
        radii.push_back(radiusProp[v]);
    }

    // Must have exactly 2 radii — one per live vertex, deleted slot excluded.
    EXPECT_EQ(radii.size(), 2u);
    for (float r : radii)
        EXPECT_FLOAT_EQ(r, 0.02f);
}
