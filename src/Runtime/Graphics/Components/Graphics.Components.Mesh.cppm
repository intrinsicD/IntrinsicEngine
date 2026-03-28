// -------------------------------------------------------------------------
// Mesh::Data — ECS component for PropertySet-backed mesh visualization.
// -------------------------------------------------------------------------
//
// Holds a shared_ptr to an authoritative Geometry::Halfedge::Mesh instance.
// Retaining the mesh on the entity enables:
//   - Enumeration of vertex/edge/face PropertySet properties for the UI.
//   - Mapping arbitrary properties to per-element colors via ColorMapper.
//   - Isoline extraction from vertex-domain scalar fields.
//   - Vector field visualization from vertex-domain vec3 properties.
//
// Created by importers (or geometry operators) when PropertySet data is
// available. Entities without Mesh::Data fall back to programmatic color
// setting on Surface::Component::CachedFaceColors.

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
