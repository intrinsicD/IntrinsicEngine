// -------------------------------------------------------------------------
// Mesh::Data — ECS component for PropertySet-backed mesh visualization.
// -------------------------------------------------------------------------
//
// The authoritative CPU geometry data for a mesh entity lives in the
// ECS::Components::GeometrySources::Vertices / Edges / Halfedges / Faces
// components.  Loaders and algorithms write data there first, and all
// lifecycle / attribute-sync systems read from those components.
//
// MeshRef is an *optional computation tool* — kept for algorithms that
// need direct access to the halfedge structure (raycasting, geometry
// operators, topology editing).  It is NOT the data authority and must
// never be used as the primary data source in rendering systems.
//
// Entities without MeshRef still render correctly; they simply cannot
// participate in halfedge-topology-dependent operations until MeshRef
// is constructed (e.g. from GeometrySources topology properties).
//
// Visualization pipeline:
//   GeometrySources::Vertices::Properties["v:color"] → ColorMapper
//     → Surface::Component::CachedVertexColors → SurfacePass shader.
// -------------------------------------------------------------------------

module;
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Graphics.Components.Mesh;

import Graphics.VisualizationConfig;
import Geometry.HalfedgeMesh;
import Geometry.KMeans;

export namespace ECS::Mesh
{
    struct Data
    {
        // ---- Authoritative Data Source ----
        std::shared_ptr<Geometry::Halfedge::Mesh> MeshRef{};

        // ---- Visualization Configuration ----
        // Selects which PropertySet properties drive per-vertex/edge/face colors.
        Graphics::VisualizationConfig Visualization{};

        // When true, re-extract colors from MeshRef's PropertySets.
        bool AttributesDirty = true;

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

        // Centroid positions from the last KMeans run.  Retained so the
        // surface shader can compute true centroid-based Voronoi cells.
        std::vector<glm::vec3> KMeansCentroids{};

        // ---- Queries (delegate to MeshRef) ----
        [[nodiscard]] std::size_t VertexCount() const noexcept
        {
            return MeshRef ? MeshRef->VertexCount() : 0;
        }
        [[nodiscard]] std::size_t EdgeCount() const noexcept
        {
            return MeshRef ? MeshRef->EdgeCount() : 0;
        }
        [[nodiscard]] std::size_t FaceCount() const noexcept
        {
            return MeshRef ? MeshRef->FaceCount() : 0;
        }
    };
}
