// -------------------------------------------------------------------------
// Surface::Component — Owned by SurfacePass (filled triangle rendering).
// -------------------------------------------------------------------------
//
// The sole authority for surface/mesh rendering. Created by SceneManager
// when spawning mesh entities, managed by MeshRendererLifecycle for GPU
// slot allocation, and consumed by SurfacePass and GPUSceneSync.

module;
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

export module Graphics.Components.Surface;

import Graphics.Components.Core;
import Graphics.Geometry;
import Graphics.Material;
import Geometry.Handle;
import Core.Assets;

export namespace ECS::Surface
{
    struct Component
    {
        // ---- Geometry & Material ----
        Geometry::GeometryHandle Geometry{};
        Core::Assets::AssetHandle Material{};

        // ---- Retained Mode Slot ----
        uint32_t GpuSlot = ECS::kInvalidGpuSlot;

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

        // ---- Per-Vertex Color Cache (optional) ----
        // Packed ABGR per vertex, sourced from vertex PropertySet properties
        // (scalar fields via colormap, or direct vec3/vec4 RGB/RGBA).
        // Interpolated across triangles by the rasterizer for smooth
        // scalar field visualization on mesh surfaces.
        // When empty, falls back to per-face colors or texture.
        std::vector<uint32_t> CachedVertexColors{};
        bool VertexColorsDirty = true;

        // ---- Per-Face Color Cache (optional) ----
        // Packed ABGR per face, sourced from Mesh::FaceProperties("f:color")
        // or set programmatically (e.g., segmentation labels, curvature).
        // Indexed by triangle index (gl_PrimitiveID in fragment shader).
        // When empty, SurfacePass uses standard texture/material shading.
        std::vector<uint32_t> CachedFaceColors{};
        bool FaceColorsDirty = true;

        // ---- Attribute Visualization Toggles ----
        // Per-vertex colors take priority over per-face colors.
        bool ShowPerVertexColors = true;
        // When true and CachedFaceColors is non-empty, SurfacePass uses
        // per-face colors. When false, standard texture/material shading
        // is used even if face color data exists. Toggled via Inspector UI.
        bool ShowPerFaceColors = true;

        // When true, per-vertex colors are rendered as nearest-vertex (Voronoi)
        // instead of smooth interpolation. Requires PtrIndices in the draw batch.
        bool UseNearestVertexColors = false;

        // ---- Centroid-Based Voronoi (optional) ----
        // When non-empty, the shader uses vertex labels (not colors) to look
        // up centroid positions and computes true centroid-based Voronoi cells.
        // CachedVertexLabels: uint32 per vertex (cluster ID).
        // CachedCentroids: {vec3 pos, uint32 packedColor} per centroid.
        std::vector<uint32_t> CachedVertexLabels{};

        struct CentroidEntry { glm::vec3 Position; uint32_t PackedColor; };
        std::vector<CentroidEntry> CachedCentroids{};
    };
}
