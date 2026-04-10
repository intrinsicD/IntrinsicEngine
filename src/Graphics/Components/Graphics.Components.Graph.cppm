// -------------------------------------------------------------------------
// Graph::Data — ECS component for graph visualization (PropertySet-backed).
// -------------------------------------------------------------------------
//
// The authoritative CPU geometry data for a graph entity lives in the
// ECS::Components::GeometrySources::Nodes / Edges components.  Loaders
// and algorithms write data there; lifecycle systems read from those
// components exclusively.
//
// GraphRef is an *optional computation tool* — kept for algorithms that
// need direct graph-traversal APIs (connectivity queries, topology edits).
// It is NOT the data authority and must never be used as the primary data
// source in rendering systems.
//
// Rendering: retained-mode via BDA shared-buffer architecture.
//   - Nodes rendered via PointPass (BDA position pull from GeometrySources).
//   - Edges rendered via LinePass (BDA position pull + edge buffer).
// -------------------------------------------------------------------------

module;
#include <string>
#include <memory>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

export module Graphics.Components.Graph;

import Graphics.Components.Core;
import Graphics.VisualizationConfig;
import Geometry.Graph;
import Geometry.PointCloudUtils;
import Geometry.Handle;
import Geometry.KMeans;

export namespace ECS::Graph
{
    struct Data
    {
        // ---- Authoritative data: GeometrySources::Nodes / Edges (ECS components) ----

        // ---- Optional computation tool (NOT the data authority) ----
        // GraphRef is kept for algorithms that need direct halfedge/traversal APIs.
        // Lifecycle systems never read from GraphRef; they read from GeometrySources.
        std::shared_ptr<Geometry::Graph::Graph> GraphRef;

        // ---- Rendering Parameters (not data — data lives in PropertySets) ----
        Geometry::PointCloud::RenderMode NodeRenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        float     DefaultNodeRadius  = 0.01f;
        float     NodeSizeMultiplier = 1.0f;
        glm::vec4 DefaultNodeColor   = {0.8f, 0.5f, 0.0f, 1.0f};
        glm::vec4 DefaultEdgeColor   = {0.6f, 0.6f, 0.6f, 1.0f};
        float     EdgeWidth          = 1.5f;
        bool      EdgesOverlay       = false;
        bool      Visible            = true;
        bool      StaticGeometry     = false;
        bool      VectorFieldMode    = false;

        // ---- Visualization Configuration ----
        // Selects which PropertySet properties drive per-node/edge color rendering.
        // When VertexColors.PropertyName is empty, falls back to "v:color".
        // When EdgeColors.PropertyName is empty, falls back to "e:color".
        Graphics::VisualizationConfig Visualization;

        // ---- GPU State (managed by GraphLifecycleSystem) ----
        Geometry::GeometryHandle GpuGeometry{};
        uint32_t GpuSlot = ECS::kInvalidGpuSlot;

        Geometry::GeometryHandle GpuEdgeGeometry{};
        uint32_t GpuEdgeCount = 0;

        std::vector<ECS::EdgePair> CachedEdgePairs;
        std::vector<uint32_t> CachedEdgeColors;
        std::vector<uint32_t> CachedNodeColors;
        std::vector<float> CachedNodeRadii;

        bool GpuDirty = true;
        uint32_t GpuVertexCount = 0;

        // ---- K-means compute state ----
        bool KMeansJobPending = false;
        uint32_t KMeansPendingClusterCount = 0;
        Geometry::KMeans::Backend KMeansLastBackend = Geometry::KMeans::Backend::CPU;
        uint32_t KMeansLastIterations = 0;
        bool KMeansLastConverged = false;
        float KMeansLastInertia = 0.0f;
        uint32_t KMeansLastMaxDistanceIndex = 0;
        double KMeansLastDurationMs = 0.0;
        entt::entity KMeansCentroidEntity = entt::null;
        uint64_t KMeansResultRevision = 0;

        // ---- Queries ----
        // NOTE: These delegate to GpuVertexCount / GpuEdgeCount which are set
        // after the first GPU upload from GeometrySources.  Before the first
        // upload they return 0.  For live counts use GeometrySources::NodeCount()
        // / EdgeCount() with the GeometrySources::Nodes / Edges ECS components.
        [[nodiscard]] std::size_t NodeCount() const noexcept
        {
            return GpuVertexCount;
        }
        [[nodiscard]] std::size_t EdgeCount() const noexcept
        {
            return GpuEdgeCount;
        }
        // Property presence is now resolved via GeometrySources::Nodes/Edges
        // PropertySets.  These helpers remain for legacy call-sites.
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
