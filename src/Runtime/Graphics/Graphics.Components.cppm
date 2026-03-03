module;
#include <memory>
#include <cstdint>
#include <vector>
#include <utility>

#include <glm/glm.hpp>

export module Graphics:Components;

import :Geometry;
import :Material;
import Geometry;
import Core.Assets;

export namespace ECS::MeshRenderer
{
    struct Component
    {
        Geometry::GeometryHandle Geometry;
        Core::Assets::AssetHandle Material;

        // --- Retained Mode Slot ---
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // --- Render Cache ---
        // Allows RenderSystem to avoid AssetManager lookups once resolved.
        Graphics::MaterialHandle CachedMaterialHandle = {};

        // Cached snapshot used by GPUSceneSync to detect when instance TextureID must be refreshed.
        Graphics::MaterialHandle CachedMaterialHandleForInstance = {};
        uint32_t CachedMaterialRevisionForInstance = 0u;
        bool CachedIsSelectedForInstance = false;
    };
}

export namespace ECS::MeshCollider
{
    struct Component
    {
        std::shared_ptr<Graphics::GeometryCollisionData> CollisionRef;
        Geometry::OBB WorldOBB;
    };
}

// -------------------------------------------------------------------------
// PointCloudRenderer — ECS component for standalone point cloud rendering.
// -------------------------------------------------------------------------
//
// Entities with this component are rendered by RetainedPointCloudRenderPass
// via BDA from a device-local vertex buffer. CPU point data is uploaded once
// by PointCloudRendererLifecycle and then freed.
//
// Two creation paths:
//   a) File loading (ModelLoader): Geometry handle is pre-populated by the
//      loader; GpuDirty=false, CPU vectors are empty.
//   b) Code-originated (demo, algorithms): CPU vectors are filled, GpuDirty=true.
//      PointCloudRendererLifecycle uploads on first frame, stores handle,
//      and clears CPU vectors to free memory.
//
// Rendering modes:
//   0 = FlatDisc, 1 = Surfel, 2 = EWA

export namespace ECS::PointCloudRenderer
{
    struct Component
    {
        // ---- Point Cloud Data (CPU-side, consumed by initial upload) ----
        std::vector<glm::vec3> Positions;           // Required for CPU-originated clouds.
        std::vector<glm::vec3> Normals;             // Optional (empty = use default up).
        std::vector<glm::vec4> Colors;              // Optional (empty = use DefaultColor).
        std::vector<float>     Radii;               // Optional (empty = use DefaultRadius).

        // ---- GPU State (managed by PointCloudRendererLifecycle) ----
        Geometry::GeometryHandle Geometry{};         // Handle to device-local GeometryGpuData.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;             // GPUScene slot for frustum culling.
        bool GpuDirty = true;                        // true = needs initial GPU upload.

        // ---- Rendering Parameters ----
        Geometry::PointCloud::RenderMode RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        float    DefaultRadius    = 0.005f;         // World-space radius when Radii is empty.
        float    SizeMultiplier   = 1.0f;           // Per-entity size multiplier.
        glm::vec4 DefaultColor    = {1.f, 1.f, 1.f, 1.f}; // RGBA when Colors is empty.
        bool     Visible          = true;           // Runtime visibility toggle.

        // ---- Queries ----
        [[nodiscard]] std::size_t PointCount() const noexcept { return Positions.size(); }
        [[nodiscard]] bool HasNormals() const noexcept { return !Normals.empty() && Normals.size() == Positions.size(); }
        [[nodiscard]] bool HasColors() const noexcept { return !Colors.empty() && Colors.size() == Positions.size(); }
        [[nodiscard]] bool HasRadii() const noexcept { return !Radii.empty() && Radii.size() == Positions.size(); }
        [[nodiscard]] bool HasGpuGeometry() const noexcept { return Geometry.IsValid(); }
    };
}

// -------------------------------------------------------------------------
// RenderVisualization — Per-entity rendering mode control.
// -------------------------------------------------------------------------
//
// Decouples the visual representation from the CPU data type.  Any entity
// with spatial data (mesh, graph, point cloud) can independently toggle:
//   - Surface rendering  (filled faces — SurfacePass)
//   - Wireframe rendering (edges — LinePass, retained BDA)
//   - Vertex rendering   (points — PointCloudRenderPass)
//
// The component is attached lazily when the user first toggles a mode in
// the Inspector.  Entities without this component use defaults:
// surface=on, wireframe=off, vertices=off.

export namespace ECS::RenderVisualization
{
    // Edge index pair: two uint32 vertex indices into the collision position array.
    // Used by CachedEdges and consumed directly by LinePass BDA uploads.
    struct EdgePair
    {
        uint32_t i0;
        uint32_t i1;
    };
    static_assert(sizeof(EdgePair) == 8);

    struct Component
    {
        // ---- Mode Toggles ----
        bool ShowSurface   = true;   // SurfacePass mesh/line rendering.
        bool ShowWireframe = false;  // Edge overlay via LinePass (retained BDA).
        bool ShowVertices  = false;  // Vertex points via PointCloudRenderPass.

        // ---- Wireframe Settings ----
        glm::vec4 WireframeColor = {0.85f, 0.85f, 0.85f, 1.0f};
        float     WireframeWidth = 1.5f;
        bool      WireframeOverlay = false;  // true = no depth test (always visible).

        // ---- Vertex Settings ----
        glm::vec4 VertexColor      = {1.0f, 0.6f, 0.0f, 1.0f};
        float     VertexSize       = 0.008f;  // World-space radius.
        Geometry::PointCloud::RenderMode VertexRenderMode = Geometry::PointCloud::RenderMode::FlatDisc;

        // ---- Derived Geometry Views (GPU, shared-vertex) ----
        // Wireframe has NO GPU view — it is rendered by LinePass which reads
        // CachedEdges via BDA from persistent GPU buffers. WireframeColor is applied.
        //
        // Vertex view is lazily created once for FlatDisc point rendering via SurfacePass.
        Geometry::GeometryHandle VertexView{};    // Points

        bool VertexViewDirty = true;

        // ---- Edge Cache (internal, rebuilt lazily) ----
        // Populated from MeshCollider collision data when wireframe is first
        // enabled.  Stores unique edge pairs as index offsets into the
        // collision position array.
        std::vector<EdgePair> CachedEdges;
        bool EdgeCacheDirty = true;

        // ---- Per-Edge Color Cache (optional) ----
        // Packed ABGR per edge, sourced from Mesh::EdgeProperties("e:color")
        // or set programmatically (e.g., curvature visualization).
        // Must be same length as CachedEdges when non-empty.
        // When empty, LinePass uses uniform WireframeColor.
        std::vector<uint32_t> CachedEdgeColors;
        bool EdgeColorsDirty = true;

        // ---- Per-Face Color Cache (optional) ----
        // Packed ABGR per face, sourced from Mesh::FaceProperties("f:color")
        // or set programmatically (e.g., segmentation labels, curvature).
        // Indexed by triangle index (gl_PrimitiveID in fragment shader).
        // When empty, SurfacePass uses standard texture/material shading.
        std::vector<uint32_t> CachedFaceColors;
        bool FaceColorsDirty = true;

        // ---- Vertex Normal Cache (internal, rebuilt lazily) ----
        // Area-weighted vertex normals computed from collision mesh triangles.
        std::vector<glm::vec3> CachedVertexNormals;
        bool VertexNormalsDirty = true;

        // ---- Sync State (internal) ----
        // Tracks the last ShowSurface value written to GPUScene so that
        // GPUSceneSync can detect transitions.
        bool CachedShowSurface = true;
    };
}

// -------------------------------------------------------------------------
// Graph::Data — ECS component for graph visualization (PropertySet-backed).
// -------------------------------------------------------------------------
//
// Holds a shared_ptr to an authoritative Geometry::Graph::Graph instance.
// Node positions, colors, radii, and edge topology are sourced directly from
// the Graph's PropertySets — no std::vector copies.
//
// Rendering: retained-mode via BDA shared-buffer architecture.
//   - Nodes rendered via RetainedPointCloudRenderPass (BDA position pull).
//   - Edges rendered via LinePass (BDA position pull + edge buffer).
//   - Falls back to CPU path (GraphRenderPass → PointCloudRenderPass + DebugDraw)
//     when retained passes are disabled via FeatureRegistry.
//
// GPU state is managed by GraphGeometrySyncSystem: positions are uploaded once
// to a device-local vertex buffer (GpuGeometry), edge pairs are extracted from
// graph topology (CachedEdgePairs). Re-upload triggers on GpuDirty = true.
//
// Optional per-node attributes (colors, radii) are stored as named vertex
// properties on the Graph:
//   "v:color"  — glm::vec4 per-node color
//   "v:radius" — float per-node radius

export namespace ECS::Graph
{
    struct Data
    {
        // ---- Authoritative Data Source ----
        std::shared_ptr<Geometry::Graph::Graph> GraphRef;

        // ---- Rendering Parameters (not data — data lives in PropertySets) ----
        Geometry::PointCloud::RenderMode NodeRenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        float     DefaultNodeRadius  = 0.01f;
        float     NodeSizeMultiplier = 1.0f;
        glm::vec4 DefaultNodeColor   = {0.8f, 0.5f, 0.0f, 1.0f};
        glm::vec4 DefaultEdgeColor   = {0.6f, 0.6f, 0.6f, 1.0f};
        float     EdgeWidth          = 1.5f;   // Screen-space edge width in pixels.
        bool      EdgesOverlay       = false;  // true = edges always visible (no depth test).
        bool      Visible            = true;

        // ---- GPU State (managed by GraphGeometrySyncSystem) ----
        // Shared vertex buffer holding compacted node positions + normals.
        // Both LinePass (edges) and RetainedPointCloudRenderPass (nodes) read
        // from this buffer via BDA push constants.
        Geometry::GeometryHandle GpuGeometry{};

        // GPUScene slot for frustum culling and GPU-driven batching.
        // Allocated by GraphGeometrySyncSystem after successful geometry upload.
        // Freed by on_destroy hook in SceneManager.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // Edge index pairs into the compacted vertex buffer.
        // Consumed by LinePass in the same way as
        // RenderVisualization::CachedEdges for mesh wireframe.
        std::vector<ECS::RenderVisualization::EdgePair> CachedEdgePairs;

        // Per-edge colors (packed ABGR), one per edge in CachedEdgePairs order.
        // Extracted from Graph::EdgeProperties("e:color") by GraphGeometrySyncSystem.
        // When empty, LinePass uses uniform DefaultEdgeColor.
        std::vector<uint32_t> CachedEdgeColors;

        // Per-node colors (packed ABGR), one per compacted vertex.
        // Extracted from Graph::VertexProperties("v:color") by GraphGeometrySyncSystem.
        // When empty, RetainedPointCloudRenderPass uses uniform DefaultNodeColor.
        std::vector<uint32_t> CachedNodeColors;

        // Per-node radii (world-space), one per compacted vertex.
        // Extracted from Graph::VertexProperties("v:radius") by GraphGeometrySyncSystem.
        // When empty, RetainedPointCloudRenderPass uses uniform DefaultNodeRadius.
        std::vector<float> CachedNodeRadii;

        // When true, GraphGeometrySyncSystem re-uploads positions and rebuilds
        // edge pairs. Set on first attach, or by layout algorithms that modify
        // graph vertex positions.
        bool GpuDirty = true;

        // Vertex count in the GPU buffer (matches compacted vertex count at
        // upload time — deleted vertices are excluded via remapping).
        uint32_t GpuVertexCount = 0;

        // ---- Queries (delegate to GraphRef) ----
        [[nodiscard]] std::size_t NodeCount() const noexcept
        {
            return GraphRef ? GraphRef->VertexCount() : 0;
        }
        [[nodiscard]] std::size_t EdgeCount() const noexcept
        {
            return GraphRef ? GraphRef->EdgeCount() : 0;
        }
        [[nodiscard]] bool HasNodeColors() const noexcept
        {
            return GraphRef && GraphRef->VertexProperties().Exists("v:color");
        }
        [[nodiscard]] bool HasNodeRadii() const noexcept
        {
            return GraphRef && GraphRef->VertexProperties().Exists("v:radius");
        }
        [[nodiscard]] bool HasEdgeColors() const noexcept
        {
            return GraphRef && GraphRef->EdgeProperties().Exists("e:color");
        }
    };
}

// -------------------------------------------------------------------------
// PointCloud::Data — ECS component for PropertySet-backed point cloud
//                    rendering (Cloud source).
// -------------------------------------------------------------------------
//
// Holds a shared_ptr to an authoritative Geometry::PointCloud::Cloud
// instance. Positions, normals, colors, and radii are sourced directly
// from the Cloud's PropertySets — no std::vector copies on the component.
//
// Rendering: retained-mode via BDA shared-buffer architecture.
//   - Points rendered via RetainedPointCloudRenderPass (BDA position pull).
//   - GPU state managed by PointCloudGeometrySyncSystem: positions/normals
//     are uploaded to a device-local vertex buffer (GpuGeometry), per-point
//     attributes extracted from PropertySets.
//   - Re-upload triggers on GpuDirty = true.
//
// Two creation paths:
//   a) Algorithm output: Cloud is populated from geometry operators (normal
//      estimation, surface reconstruction, downsampling); GpuDirty = true.
//   b) File loading: Cloud is populated by loaders; GpuDirty = true.
//
// Key difference from PointCloudRenderer::Component:
//   - PointCloudRenderer::Component owns raw CPU vectors and frees them
//     after upload (one-shot lifecycle, managed by PointCloudRendererLifecycle).
//   - PointCloud::Data borrows from a shared Cloud instance; the Cloud
//     remains alive for re-upload when its data changes (sync lifecycle,
//     managed by PointCloudGeometrySyncSystem).

export namespace ECS::PointCloud
{
    struct Data
    {
        // ---- Authoritative Data Source ----
        std::shared_ptr<Geometry::PointCloud::Cloud> CloudRef;

        // ---- Rendering Parameters (not data — data lives in PropertySets) ----
        Geometry::PointCloud::RenderMode RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        float     DefaultRadius    = 0.005f;         // World-space radius.
        float     SizeMultiplier   = 1.0f;           // Per-entity size multiplier.
        glm::vec4 DefaultColor     = {1.f, 1.f, 1.f, 1.f}; // RGBA when colors absent.
        bool      Visible          = true;            // Runtime visibility toggle.

        // ---- GPU State (managed by PointCloudGeometrySyncSystem) ----
        // Device-local vertex buffer holding positions + normals.
        // RetainedPointCloudRenderPass reads from this buffer via BDA.
        Geometry::GeometryHandle GpuGeometry{};

        // GPUScene slot for frustum culling and GPU-driven batching.
        // Allocated by PointCloudGeometrySyncSystem after successful upload.
        // Freed by on_destroy hook in SceneManager.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // Per-point colors (packed ABGR), one per point.
        // Extracted from Cloud's "p:color" by PointCloudGeometrySyncSystem.
        // When empty, RetainedPointCloudRenderPass uses uniform DefaultColor.
        std::vector<uint32_t> CachedColors;

        // Per-point radii (world-space), one per point.
        // Extracted from Cloud's "p:radius" by PointCloudGeometrySyncSystem.
        // When empty, RetainedPointCloudRenderPass uses uniform DefaultRadius.
        std::vector<float> CachedRadii;

        // When true, PointCloudGeometrySyncSystem re-uploads positions/normals
        // and re-extracts per-point attributes. Set on first attach, or when
        // Cloud data changes.
        bool GpuDirty = true;

        // Point count in the GPU buffer (matches Cloud::Size() at upload time).
        uint32_t GpuPointCount = 0;

        // ---- Queries (delegate to CloudRef) ----
        [[nodiscard]] std::size_t PointCount() const noexcept
        {
            return CloudRef ? CloudRef->Size() : 0;
        }
        [[nodiscard]] bool HasNormals() const noexcept
        {
            return CloudRef && CloudRef->HasNormals();
        }
        [[nodiscard]] bool HasColors() const noexcept
        {
            return CloudRef && CloudRef->HasColors();
        }
        [[nodiscard]] bool HasRadii() const noexcept
        {
            return CloudRef && CloudRef->HasRadii();
        }
        [[nodiscard]] bool HasGpuGeometry() const noexcept
        {
            return GpuGeometry.IsValid();
        }
    };
}

export namespace ECS::GeometryViewRenderer
{
    struct Component
    {
        // Base (surface / primary) geometry.
        Geometry::GeometryHandle Surface{};
        uint32_t SurfaceGpuSlot = MeshRenderer::Component::kInvalidSlot;

        // Optional vertex point-cloud view geometry (FlatDisc mode via SurfacePass).
        Geometry::GeometryHandle Vertices{}; // Points
        uint32_t VerticesGpuSlot = MeshRenderer::Component::kInvalidSlot;

        // Wireframe edge count — set by LinePass when a persistent BDA-addressable
        // edge buffer exists for this entity. The actual buffer is owned by the
        // pass; this field tracks edge count for lifecycle awareness.
        // 0 = no persistent wireframe buffer exists.
        uint32_t WireframeEdgeCount = 0;

        // Visibility toggles mirrored from RenderVisualization.
        bool ShowSurface = true;
        bool ShowVertices = false;
    };
}

// -------------------------------------------------------------------------
// MeshEdgeView — Edge view derived from a mesh via ReuseVertexBuffersFrom.
// -------------------------------------------------------------------------
//
// Attached to entities with MeshRenderer to request wireframe edge rendering
// as a first-class GPU geometry view. MeshViewLifecycleSystem creates the
// edge index buffer (sharing the mesh's vertex buffer via BDA) and manages
// the GPUScene slot lifecycle.
//
// Edge pairs are flattened from RenderVisualization::CachedEdges into a
// contiguous uint32_t index buffer with topology Lines. The index buffer
// is BDA-accessible, allowing LinePass to read edge pairs directly from
// the GeometryGpuData without maintaining internal buffers.

export namespace ECS::MeshEdgeView
{
    struct Component
    {
        // Edge view geometry handle — index buffer of edge pairs sharing mesh
        // vertex buffer via ReuseVertexBuffersFrom. Topology = Lines.
        Geometry::GeometryHandle Geometry{};

        // GPUScene slot for frustum culling of this edge view.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // Number of edges in the uploaded buffer.
        uint32_t EdgeCount = 0;

        // true when the edge buffer needs (re-)creation.
        // Set on first attach, or when CachedEdges change.
        bool Dirty = true;

        // ---- Queries ----
        [[nodiscard]] bool HasGpuGeometry() const noexcept { return Geometry.IsValid(); }
    };
}

// -------------------------------------------------------------------------
// MeshVertexView — Vertex point view derived from a mesh via
//                  ReuseVertexBuffersFrom.
// -------------------------------------------------------------------------
//
// Attached to entities with MeshRenderer to request vertex point rendering
// as a first-class GPU geometry view. MeshViewLifecycleSystem creates the
// view (sharing the mesh's vertex buffer via BDA, topology Points) and
// manages the GPUScene slot lifecycle.

export namespace ECS::MeshVertexView
{
    struct Component
    {
        // Vertex view geometry handle — sharing mesh vertex buffer via
        // ReuseVertexBuffersFrom. Topology = Points, no index buffer.
        Geometry::GeometryHandle Geometry{};

        // GPUScene slot for frustum culling of this vertex view.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // Number of vertices in the view (derived from source mesh layout).
        uint32_t VertexCount = 0;

        // true when the vertex view needs (re-)creation.
        bool Dirty = true;

        // ---- Queries ----
        [[nodiscard]] bool HasGpuGeometry() const noexcept { return Geometry.IsValid(); }
    };
}

// =========================================================================
// Per-Pass Typed ECS Components (PLAN.md Phase 1)
// =========================================================================
//
// These components implement the three-pass rendering architecture described
// in PLAN.md. Each pass owns a dedicated component type; the toggle is
// presence/absence of the component — no boolean flags. Attaching a
// component enables that visualization, removing it disables it.
//
// During the transition period, these components coexist alongside the
// legacy components (MeshRenderer, RenderVisualization, PointCloudRenderer).
// A migration system (ComponentMigration) keeps them synchronized until
// the legacy components can be retired.

// -------------------------------------------------------------------------
// Surface::Component — Owned by SurfacePass (filled triangle rendering).
// -------------------------------------------------------------------------
//
// Mirrors MeshRenderer::Component initially. During transition, both
// components coexist; the migration system keeps them in sync. After
// Phase 5 (dead code deletion), MeshRenderer::Component is retired and
// Surface::Component becomes the sole authority.

export namespace ECS::Surface
{
    struct Component
    {
        // ---- Geometry & Material ----
        Geometry::GeometryHandle Geometry{};
        Core::Assets::AssetHandle Material{};

        // ---- Retained Mode Slot ----
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // ---- Render Cache ----
        Graphics::MaterialHandle CachedMaterialHandle{};
        Graphics::MaterialHandle CachedMaterialHandleForInstance{};
        uint32_t CachedMaterialRevisionForInstance = 0u;
        bool CachedIsSelectedForInstance = false;
    };
}

// -------------------------------------------------------------------------
// Line::Component — Owned by LinePass (thick anti-aliased edge rendering).
// -------------------------------------------------------------------------
//
// Replaces RenderVisualization wireframe fields and the ShowWireframe
// boolean toggle. Edge data comes from PropertySets on the source geometry
// (Halfedge::Mesh or Graph), not from a pass-local cache. Per-edge
// attributes (colors, widths) are uploaded to a separate BDA channel.

export namespace ECS::Line
{
    struct Component
    {
        // ---- Geometry Handles ----
        // Shared vertex buffer (BDA) — same device-local buffer as the
        // source mesh/graph, referenced via ReuseVertexBuffersFrom.
        Geometry::GeometryHandle Geometry{};

        // Edge index buffer (separate from vertex buffer). Contains
        // flattened uint32_t pairs from PropertySet edge topology.
        Geometry::GeometryHandle EdgeView{};

        // ---- Appearance (defaults; overridden by per-edge attributes) ----
        glm::vec4 Color = {0.85f, 0.85f, 0.85f, 1.0f};
        float     Width = 1.5f;
        bool      Overlay = false;  // true = no depth test (always visible)

        // ---- Per-Edge Attribute Flags ----
        // Set by geometry view lifecycle systems when PropertySet data
        // contains per-edge attributes.
        bool HasPerEdgeColors = false;
        bool HasPerEdgeWidths = false;
    };
}

// -------------------------------------------------------------------------
// Point::Component — Owned by PointPass (vertex/node/point cloud rendering).
// -------------------------------------------------------------------------
//
// Replaces RenderVisualization vertex fields, PointCloudRenderer rendering
// parameters, and GraphRenderer node parameters. The render mode selects
// the pipeline variant (FlatDisc, Surfel, EWA).

export namespace ECS::Point
{
    struct Component
    {
        // ---- Geometry Handle ----
        // Shared vertex buffer (BDA) — same device-local buffer as the
        // source mesh/graph/cloud. For mesh-derived vertex views, created
        // via ReuseVertexBuffersFrom with topology Points.
        Geometry::GeometryHandle Geometry{};

        // ---- Appearance (defaults; overridden by per-point attributes) ----
        glm::vec4 Color = {1.0f, 0.6f, 0.0f, 1.0f};
        float     Size  = 0.008f;         // World-space radius
        float     SizeMultiplier = 1.0f;  // Per-entity size scaling
        Geometry::PointCloud::RenderMode Mode = Geometry::PointCloud::RenderMode::FlatDisc;

        // ---- Per-Point Attribute Flags ----
        // Set by geometry view lifecycle systems when PropertySet data
        // contains per-point attributes.
        bool HasPerPointColors  = false;
        bool HasPerPointRadii   = false;
        bool HasPerPointNormals = false;  // Required for Surfel mode
    };
}
