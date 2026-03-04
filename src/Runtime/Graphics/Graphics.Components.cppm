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

// =========================================================================
// DirtyTag — Per-domain dirty tracking for PropertySet CPU→GPU sync.
// =========================================================================
//
// Zero-size tag components for fine-grained dirty tracking. Each tag
// represents an independent data domain. Presence of a tag on an entity
// signals that the corresponding data has changed and needs re-sync.
//
// Six domains:
//   VertexPositions  — node/point positions changed → vertex buffer re-upload.
//   VertexAttributes — per-vertex colors/radii changed → re-extract cached attributes.
//   EdgeTopology     — edge connectivity changed → rebuild edge index buffer.
//   EdgeAttributes   — per-edge colors/widths changed → re-extract cached edge attributes.
//   FaceTopology     — face connectivity changed → rebuild face index buffer.
//   FaceAttributes   — per-face colors changed → re-extract cached face attributes.
//
// Consumed by PropertySetDirtySyncSystem. Cleared after successful sync.
// Multiple tags can coexist on the same entity (independent domains).

export namespace ECS::DirtyTag
{
    struct VertexPositions {};
    struct VertexAttributes {};
    struct EdgeTopology {};
    struct EdgeAttributes {};
    struct FaceTopology {};
    struct FaceAttributes {};
}

// =========================================================================
// EdgePair — Standalone edge index pair type (formerly in RenderVisualization).
// =========================================================================
// Two uint32 vertex indices referencing into a position array.
// Used by Graph::Data::CachedEdgePairs, LinePass BDA uploads,
// MeshViewLifecycleSystem, and GraphGeometrySyncSystem.

export namespace ECS
{
    struct EdgePair
    {
        uint32_t i0;
        uint32_t i1;
    };
    static_assert(sizeof(EdgePair) == 8);
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
// Entities with this component are rendered by PointPass
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
// Graph::Data — ECS component for graph visualization (PropertySet-backed).
// -------------------------------------------------------------------------
//
// Holds a shared_ptr to an authoritative Geometry::Graph::Graph instance.
// Node positions, colors, radii, and edge topology are sourced directly from
// the Graph's PropertySets — no std::vector copies.
//
// Rendering: retained-mode via BDA shared-buffer architecture.
//   - Nodes rendered via PointPass (BDA position pull).
//   - Edges rendered via LinePass (BDA position pull + edge buffer).
//
// GPU state is managed by GraphGeometrySyncSystem: positions are uploaded to
// a vertex buffer (GpuGeometry) in either Staged mode (device-local, optimal
// for static graphs — controlled by StaticGeometry flag) or Direct mode
// (host-visible, suitable for dynamic re-layout). Edge pairs are extracted
// from graph topology (CachedEdgePairs). Re-upload triggers on GpuDirty = true.
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

        // When true, GraphGeometrySyncSystem uploads via Staged mode
        // (device-local, VMA_MEMORY_USAGE_GPU_ONLY) — optimal for graphs
        // that don't change every frame (file-loaded, computed once).
        // When false (default), Direct mode (host-visible,
        // VMA_MEMORY_USAGE_CPU_TO_GPU) is used — suitable for dynamic
        // graphs undergoing frequent re-layout.
        bool      StaticGeometry     = false;

        // ---- GPU State (managed by GraphGeometrySyncSystem) ----
        // Shared vertex buffer holding compacted node positions + normals.
        // Both LinePass (edges) and PointPass (nodes) read
        // from this buffer via BDA push constants.
        Geometry::GeometryHandle GpuGeometry{};

        // GPUScene slot for frustum culling and GPU-driven batching.
        // Allocated by GraphGeometrySyncSystem after successful geometry upload.
        // Freed by on_destroy hook in SceneManager.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // Edge index buffer (via ReuseVertexBuffersFrom of GpuGeometry).
        // Contains flattened uint32_t edge pairs. Created by
        // GraphGeometrySyncSystem alongside positions. LinePass reads
        // edge indices from this geometry's index buffer via BDA.
        Geometry::GeometryHandle GpuEdgeGeometry{};
        uint32_t GpuEdgeCount = 0;

        // Edge index pairs into the compacted vertex buffer (CPU-side).
        // Retained for non-rendering consumers (layout algorithms, selection).
        std::vector<ECS::EdgePair> CachedEdgePairs;

        // Per-edge colors (packed ABGR), one per edge in CachedEdgePairs order.
        // Extracted from Graph::EdgeProperties("e:color") by GraphGeometrySyncSystem.
        // When empty, LinePass uses uniform DefaultEdgeColor.
        std::vector<uint32_t> CachedEdgeColors;

        // Per-node colors (packed ABGR), one per compacted vertex.
        // Extracted from Graph::VertexProperties("v:color") by GraphGeometrySyncSystem.
        // When empty, PointPass uses uniform DefaultNodeColor.
        std::vector<uint32_t> CachedNodeColors;

        // Per-node radii (world-space), one per compacted vertex.
        // Extracted from Graph::VertexProperties("v:radius") by GraphGeometrySyncSystem.
        // When empty, PointPass uses uniform DefaultNodeRadius.
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
//   - Points rendered via PointPass (BDA position pull).
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
        // PointPass reads from this buffer via BDA.
        Geometry::GeometryHandle GpuGeometry{};

        // GPUScene slot for frustum culling and GPU-driven batching.
        // Allocated by PointCloudGeometrySyncSystem after successful upload.
        // Freed by on_destroy hook in SceneManager.
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // Per-point colors (packed ABGR), one per point.
        // Extracted from Cloud's "p:color" by PointCloudGeometrySyncSystem.
        // When empty, PointPass uses uniform DefaultColor.
        std::vector<uint32_t> CachedColors;

        // Per-point radii (world-space), one per point.
        // Extracted from Cloud's "p:radius" by PointCloudGeometrySyncSystem.
        // When empty, PointPass uses uniform DefaultRadius.
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

// -------------------------------------------------------------------------
// MeshEdgeView — Edge view derived from a mesh via ReuseVertexBuffersFrom.
// -------------------------------------------------------------------------
//
// Attached to entities with Surface::Component to request wireframe edge
// rendering as a first-class GPU geometry view. MeshViewLifecycleSystem
// creates the edge index buffer (sharing the mesh's vertex buffer via BDA)
// and manages the GPUScene slot lifecycle.
//
// Edge pairs are extracted from collision data and uploaded into a
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
        // Set on first attach, or when collision data changes.
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
// Attached to entities with Surface::Component to request vertex point
// rendering as a first-class GPU geometry view. MeshViewLifecycleSystem
// creates the view (sharing the mesh's vertex buffer via BDA, topology
// Points) and manages the GPUScene slot lifecycle.

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
// Per-Pass Typed ECS Components
// =========================================================================
//
// These components implement the three-pass rendering architecture described
// in PLAN.md. Each pass owns a dedicated component type; the toggle is
// presence/absence of the component — no boolean flags. Attaching a
// component enables that visualization, removing it disables it.

// -------------------------------------------------------------------------
// Surface::Component — Owned by SurfacePass (filled triangle rendering).
// -------------------------------------------------------------------------
//
// The sole authority for surface/mesh rendering. Created by SceneManager
// when spawning mesh entities, managed by MeshRendererLifecycle for GPU
// slot allocation, and consumed by SurfacePass and GPUSceneSync.

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

        // ---- Visibility ----
        // Runtime visibility toggle. When false, the GPU scene slot is
        // deactivated (radius=0) so the culler skips this instance.
        bool Visible = true;

        // Tracks the last Visible value written to GPUScene so that
        // GPUSceneSync can detect transitions.
        bool CachedVisible = true;

        // ---- Per-Face Color Cache (optional) ----
        // Packed ABGR per face, sourced from Mesh::FaceProperties("f:color")
        // or set programmatically (e.g., segmentation labels, curvature).
        // Indexed by triangle index (gl_PrimitiveID in fragment shader).
        // When empty, SurfacePass uses standard texture/material shading.
        std::vector<uint32_t> CachedFaceColors;
        bool FaceColorsDirty = true;

        // ---- Attribute Visualization Toggle ----
        // When true and CachedFaceColors is non-empty, SurfacePass uses
        // per-face colors. When false, standard texture/material shading
        // is used even if face color data exists. Toggled via Inspector UI.
        bool ShowPerFaceColors = true;
    };
}

// -------------------------------------------------------------------------
// Line::Component — Owned by LinePass (thick anti-aliased edge rendering).
// -------------------------------------------------------------------------
//
// Presence of this component enables wireframe/edge rendering for the entity.
// Removal disables it. Edge data comes from MeshViewLifecycleSystem (for
// meshes) or GraphGeometrySyncSystem (for graphs). Per-edge attributes
// (colors, widths) are uploaded to a separate BDA channel by LinePass.

export namespace ECS::Line
{
    struct Component
    {
        // ---- Geometry Handles ----
        // Shared vertex buffer (BDA) — same device-local buffer as the
        // source mesh/graph, referenced via ReuseVertexBuffersFrom.
        Geometry::GeometryHandle Geometry{};

        // Edge index buffer (separate from vertex buffer). Contains
        // flattened uint32_t pairs. Created by MeshViewLifecycleSystem
        // (from collision data) or GraphGeometrySyncSystem (from graph
        // topology via ReuseVertexBuffersFrom). Must be valid for
        // LinePass to render edges — no internal fallback.
        Geometry::GeometryHandle EdgeView{};

        // Number of edges to render.
        uint32_t EdgeCount = 0;

        // ---- Appearance (defaults; overridden by per-edge attributes) ----
        glm::vec4 Color = {0.85f, 0.85f, 0.85f, 1.0f};
        float     Width = 1.5f;
        bool      Overlay = false;  // true = no depth test (always visible)

        // ---- Per-Edge Attribute Flags ----
        bool HasPerEdgeColors = false;
        bool HasPerEdgeWidths = false;

        // ---- Attribute Visualization Toggle ----
        // When true and HasPerEdgeColors is true, LinePass uses per-edge
        // colors from the aux buffer. When false, uniform Color is used
        // even if per-edge data exists. Toggled via Inspector UI.
        bool ShowPerEdgeColors = true;

        // ---- Per-Edge Color Cache (optional) ----
        // Packed ABGR per edge, sourced from mesh edge PropertySets
        // or set programmatically (e.g., curvature visualization).
        // Must be same length as EdgeCount when non-empty.
        // When empty, LinePass uses uniform Color.
        // For graph entities, edge colors come from Graph::Data::CachedEdgeColors.
        std::vector<uint32_t> CachedEdgeColors;
    };
}

// -------------------------------------------------------------------------
// Point::Component — Owned by PointPass (vertex/node/point cloud rendering).
// -------------------------------------------------------------------------
//
// Presence of this component enables point rendering for the entity.
// Removal disables it. The render mode selects the pipeline variant
// (FlatDisc, Surfel, EWA).

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
