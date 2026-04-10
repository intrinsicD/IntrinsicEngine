module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module ECS:Components.GeometrySourcesPopulate.Impl;

import :Components.GeometrySources;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;
import Geometry.Properties;

namespace ECS::Components::GeometrySources
{
    // =========================================================================
    // PopulateFromMesh
    // =========================================================================
    void PopulateFromMesh(entt::registry& reg,
                          entt::entity   entity,
                          Geometry::Halfedge::Mesh& mesh)
    {
        const std::size_t vSize  = mesh.VerticesSize();
        const std::size_t eSize  = mesh.EdgesSize();
        const std::size_t hSize  = mesh.HalfedgesSize();
        const std::size_t fSize  = mesh.FacesSize();

        // ---- Vertices -------------------------------------------------------
        // Copy the full vertex PropertySet (includes user props: colors, labels…)
        // then write the canonical "v:position" key.
        auto& vComp        = reg.emplace_or_replace<Vertices>(entity);
        vComp.Properties   = mesh.VertexProperties();          // PropertySet copy
        vComp.NumDeleted   = vSize - mesh.VertexCount();

        {
            auto posProp = vComp.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kPosition}, glm::vec3(0.0f));
            posProp.Vector().resize(vSize);
            for (std::size_t i = 0; i < vSize; ++i)
            {
                const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                posProp.Vector()[i] = mesh.Position(vh);
            }
        }

        // ---- Edges ----------------------------------------------------------
        // Copy full edge PropertySet, then write "e:v0" / "e:v1" connectivity.
        auto& eComp      = reg.emplace_or_replace<Edges>(entity);
        eComp.Properties = mesh.EdgeProperties();
        eComp.NumDeleted = eSize - mesh.EdgeCount();

        {
            auto v0Prop = eComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kEdgeV0}, 0u);
            auto v1Prop = eComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kEdgeV1}, 0u);
            v0Prop.Vector().resize(eSize);
            v1Prop.Vector().resize(eSize);

            for (std::size_t i = 0; i < eSize; ++i)
            {
                const Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(i)};
                const auto h0 = mesh.Halfedge(eh, 0);
                v0Prop.Vector()[i] = static_cast<uint32_t>(mesh.FromVertex(h0).Index);
                v1Prop.Vector()[i] = static_cast<uint32_t>(mesh.ToVertex(h0).Index);
            }
        }

        // ---- Halfedges ------------------------------------------------------
        auto& hComp      = reg.emplace_or_replace<Halfedges>(entity);
        hComp.Properties = mesh.HalfedgeProperties();

        {
            constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();

            auto toVtxProp = hComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kHalfedgeToVertex}, kInvalid);
            auto nextProp  = hComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kHalfedgeNext}, kInvalid);
            auto faceProp  = hComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kHalfedgeFace}, kInvalid);

            toVtxProp.Vector().resize(hSize);
            nextProp.Vector().resize(hSize);
            faceProp.Vector().resize(hSize);

            for (std::size_t i = 0; i < hSize; ++i)
            {
                const Geometry::HalfedgeHandle hh{static_cast<Geometry::PropertyIndex>(i)};
                toVtxProp.Vector()[i] = static_cast<uint32_t>(mesh.ToVertex(hh).Index);
                nextProp.Vector()[i]  = static_cast<uint32_t>(mesh.NextHalfedge(hh).Index);
                const auto face       = mesh.Face(hh);
                faceProp.Vector()[i]  = face.IsValid()
                    ? static_cast<uint32_t>(face.Index)
                    : kInvalid;
            }
        }

        // ---- Faces ----------------------------------------------------------
        auto& fComp      = reg.emplace_or_replace<Faces>(entity);
        fComp.Properties = mesh.FaceProperties();
        fComp.NumDeleted = fSize - mesh.FaceCount();

        {
            constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();
            auto heProp = fComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kFaceHalfedge}, kInvalid);
            heProp.Vector().resize(fSize);

            for (std::size_t i = 0; i < fSize; ++i)
            {
                const Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(i)};
                const auto he = mesh.Halfedge(fh);
                heProp.Vector()[i] = he.IsValid()
                    ? static_cast<uint32_t>(he.Index)
                    : kInvalid;
            }
        }
    }

    // =========================================================================
    // PopulateFromGraph
    // =========================================================================
    void PopulateFromGraph(entt::registry& reg,
                           entt::entity    entity,
                           Geometry::Graph::Graph& graph)
    {
        // Compact the graph so GeometrySources has no deleted-index gaps.
        if (graph.HasGarbage())
            graph.GarbageCollection();

        const std::size_t vSize = graph.VerticesSize();
        const std::size_t eSize = graph.EdgesSize();

        // ---- Nodes ----------------------------------------------------------
        auto& nComp      = reg.emplace_or_replace<Nodes>(entity);
        nComp.Properties = graph.VertexProperties();   // PropertySet copy
        nComp.NumDeleted = 0;                          // 0 after GC

        {
            auto posProp = nComp.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kPosition}, glm::vec3(0.0f));
            posProp.Vector().resize(vSize);
            for (std::size_t i = 0; i < vSize; ++i)
            {
                const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                posProp.Vector()[i] = graph.VertexPosition(vh);
            }
        }

        // ---- Edges ----------------------------------------------------------
        auto& eComp      = reg.emplace_or_replace<Edges>(entity);
        eComp.Properties = graph.EdgeProperties();
        eComp.NumDeleted = 0;

        {
            auto v0Prop = eComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kEdgeV0}, 0u);
            auto v1Prop = eComp.Properties.GetOrAdd<uint32_t>(
                std::string{PropertyNames::kEdgeV1}, 0u);
            v0Prop.Vector().resize(eSize);
            v1Prop.Vector().resize(eSize);

            for (std::size_t i = 0; i < eSize; ++i)
            {
                const Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(i)};
                const auto [v0, v1] = graph.EdgeVertices(eh);
                v0Prop.Vector()[i]  = static_cast<uint32_t>(v0.Index);
                v1Prop.Vector()[i]  = static_cast<uint32_t>(v1.Index);
            }
        }
    }

    // =========================================================================
    // PopulateFromCloud
    // =========================================================================
    void PopulateFromCloud(entt::registry& reg,
                           entt::entity    entity,
                           Geometry::PointCloud::Cloud& cloud)
    {
        const std::size_t pSize = cloud.VerticesSize();

        auto& vComp      = reg.emplace_or_replace<Vertices>(entity);
        vComp.Properties = cloud.PointProperties();   // PropertySet copy (p:* keys)
        vComp.NumDeleted = pSize - cloud.VertexCount();

        // Write canonical "v:position" from the cloud's position span.
        {
            const auto positions = cloud.Positions();
            auto posProp = vComp.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kPosition}, glm::vec3(0.0f));
            posProp.Vector().assign(positions.begin(), positions.end());
        }

        // Write canonical "v:normal" when the cloud has normals.
        if (cloud.HasNormals())
        {
            const auto normals = cloud.Normals();
            auto normProp = vComp.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kNormal}, glm::vec3(0.0f, 1.0f, 0.0f));
            normProp.Vector().assign(normals.begin(), normals.end());
        }
    }
}

