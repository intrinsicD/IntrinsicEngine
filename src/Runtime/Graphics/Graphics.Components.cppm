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
// PointCloudRenderer — ECS component for point cloud visualization.
// -------------------------------------------------------------------------
//
// Entities with this component are rendered by PointCloudRenderPass.
// The component holds the actual point data (positions, normals, colors, radii)
// and per-entity rendering parameters.
//
// Point data is uploaded to a consolidated SSBO each frame by the
// PointCloudRenderPass during AddPasses().
//
// Rendering modes:
//   0 = Flat disc (screen-aligned billboard, constant pixel radius)

export namespace ECS::PointCloudRenderer
{
    struct Component
    {
        // ---- Point Cloud Data ----
        std::vector<glm::vec3> Positions;           // Required.
        std::vector<glm::vec3> Normals;             // Optional (empty = use default up).
        std::vector<glm::vec4> Colors;              // Optional (empty = use DefaultColor).
        std::vector<float>     Radii;               // Optional (empty = use DefaultRadius).

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
    };
}

// -------------------------------------------------------------------------
// RenderVisualization — Per-entity rendering mode control.
// -------------------------------------------------------------------------
//
// Decouples the visual representation from the CPU data type.  Any entity
// with spatial data (mesh, graph, point cloud) can independently toggle:
//   - Surface rendering  (filled faces — ForwardPass)
//   - Wireframe rendering (edges — LineRenderPass via DebugDraw)
//   - Vertex rendering   (points — PointCloudRenderPass)
//
// The component is attached lazily when the user first toggles a mode in
// the Inspector.  Entities without this component use defaults:
// surface=on, wireframe=off, vertices=off.

export namespace ECS::RenderVisualization
{
    // Edge index pair: two uint32 vertex indices into the collision position array.
    // Used by CachedEdges and consumed directly by RetainedLineRenderPass SSBO uploads.
    struct EdgePair
    {
        uint32_t i0;
        uint32_t i1;
    };
    static_assert(sizeof(EdgePair) == 8);

    struct Component
    {
        // ---- Mode Toggles ----
        bool ShowSurface   = true;   // ForwardPass mesh/line rendering.
        bool ShowWireframe = false;  // Edge overlay via DebugDraw → LineRenderPass.
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
        // Wireframe has NO GPU view — it is rendered by the CPU DebugDraw path
        // (MeshRenderPass → LineRenderPass) which correctly applies WireframeColor.
        //
        // Vertex view is lazily created once for FlatDisc point rendering via ForwardPass.
        Geometry::GeometryHandle VertexView{};    // Points

        bool VertexViewDirty = true;

        // ---- Edge Cache (internal, rebuilt lazily) ----
        // Populated from MeshCollider collision data when wireframe is first
        // enabled.  Stores unique edge pairs as index offsets into the
        // collision position array.
        std::vector<EdgePair> CachedEdges;
        bool EdgeCacheDirty = true;

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
//   - Edges rendered via RetainedLineRenderPass (BDA position pull + edge SSBO).
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
        // Both RetainedLineRenderPass (edges) and RetainedPointCloudRenderPass
        // (nodes) read from this buffer via BDA push constants.
        Geometry::GeometryHandle GpuGeometry{};

        // Edge index pairs into the compacted vertex buffer.
        // Consumed by RetainedLineRenderPass in the same way as
        // RenderVisualization::CachedEdges for mesh wireframe.
        std::vector<ECS::RenderVisualization::EdgePair> CachedEdgePairs;

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
    };
}

export namespace ECS::GeometryViewRenderer
{
    struct Component
    {
        // Base (surface / primary) geometry.
        Geometry::GeometryHandle Surface{};
        uint32_t SurfaceGpuSlot = MeshRenderer::Component::kInvalidSlot;

        // Optional vertex point-cloud view geometry (FlatDisc mode via ForwardPass).
        // Wireframe has NO GPU view — it is always CPU-driven via DebugDraw → LineRenderPass.
        Geometry::GeometryHandle Vertices{}; // Points
        uint32_t VerticesGpuSlot = MeshRenderer::Component::kInvalidSlot;

        // Visibility toggles mirrored from RenderVisualization.
        bool ShowSurface = true;
        bool ShowVertices = false;
    };
}
