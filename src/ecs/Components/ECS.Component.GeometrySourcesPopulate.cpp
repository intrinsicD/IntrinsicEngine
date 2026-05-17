module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Extrinsic.ECS.Components.GeometrySourcesPopulate;

import Extrinsic.ECS.Components.GeometrySources;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;
import Geometry.Properties;

namespace Extrinsic::ECS::Components::GeometrySources
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        // Drop every GeometrySources component + topology marker on `entity`
        // before a populate call writes the new domain. Without this reset,
        // emplace_or_replace would only overwrite the components shared
        // between the old and new domains, leaving stale Edges/Halfedges/
        // Faces/Nodes/marker entries behind — and `BuildConstView` /
        // `BuildMutableView` would then resolve `Domain::Unknown` (or expose
        // stale topology) on a domain switch such as mesh→cloud, graph→cloud,
        // or mesh→graph. `entt::registry::remove<T>` is a silent no-op when
        // the component is absent, so the reset is cheap on first-population
        // entities.
        void ResetGeometrySourceComponents(entt::registry& registry, entt::entity entity)
        {
            registry.remove<Vertices>(entity);
            registry.remove<Edges>(entity);
            registry.remove<Halfedges>(entity);
            registry.remove<Faces>(entity);
            registry.remove<Nodes>(entity);
            registry.remove<HasMeshTopology>(entity);
            registry.remove<HasGraphTopology>(entity);
        }

        void RemovePropertyIfPresent(Geometry::PropertySet& properties, const std::string_view name)
        {
            if (const auto id = properties.Registry().Find(name))
            {
                (void)properties.Registry().Remove(*id);
            }
        }
    }

    void PopulateFromMesh(entt::registry& registry,
                          entt::entity entity,
                          Geometry::HalfedgeMesh::Mesh& mesh)
    {
        ResetGeometrySourceComponents(registry, entity);

        const std::size_t vSize = mesh.VerticesSize();
        const std::size_t eSize = mesh.EdgesSize();
        const std::size_t hSize = mesh.HalfedgesSize();
        const std::size_t fSize = mesh.FacesSize();

        // ---- Vertices -------------------------------------------------------
        auto& vComp = registry.emplace_or_replace<Vertices>(entity);
        vComp.Properties = mesh.VertexProperties();
        vComp.NumDeleted = vSize - mesh.VertexCount();

        {
            RemovePropertyIfPresent(vComp.Properties, PropertyNames::kPosition);
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
        auto& eComp = registry.emplace_or_replace<Edges>(entity);
        eComp.Properties = mesh.EdgeProperties();
        eComp.NumDeleted = eSize - mesh.EdgeCount();

        {
            RemovePropertyIfPresent(eComp.Properties, PropertyNames::kEdgeV0);
            RemovePropertyIfPresent(eComp.Properties, PropertyNames::kEdgeV1);
            auto v0Prop = eComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kEdgeV0}, 0u);
            auto v1Prop = eComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kEdgeV1}, 0u);
            v0Prop.Vector().resize(eSize);
            v1Prop.Vector().resize(eSize);

            for (std::size_t i = 0; i < eSize; ++i)
            {
                const Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(i)};
                const auto h0 = mesh.Halfedge(eh, 0);
                v0Prop.Vector()[i] = static_cast<std::uint32_t>(mesh.FromVertex(h0).Index);
                v1Prop.Vector()[i] = static_cast<std::uint32_t>(mesh.ToVertex(h0).Index);
            }
        }

        // ---- Halfedges ------------------------------------------------------
        auto& hComp = registry.emplace_or_replace<Halfedges>(entity);
        hComp.Properties = mesh.HalfedgeProperties();

        {
            RemovePropertyIfPresent(hComp.Properties, PropertyNames::kHalfedgeToVertex);
            RemovePropertyIfPresent(hComp.Properties, PropertyNames::kHalfedgeNext);
            RemovePropertyIfPresent(hComp.Properties, PropertyNames::kHalfedgeFace);
            auto toVtxProp = hComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kHalfedgeToVertex}, kInvalidIndex);
            auto nextProp = hComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kHalfedgeNext}, kInvalidIndex);
            auto faceProp = hComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kHalfedgeFace}, kInvalidIndex);

            toVtxProp.Vector().resize(hSize);
            nextProp.Vector().resize(hSize);
            faceProp.Vector().resize(hSize);

            for (std::size_t i = 0; i < hSize; ++i)
            {
                const Geometry::HalfedgeHandle hh{static_cast<Geometry::PropertyIndex>(i)};
                toVtxProp.Vector()[i] = static_cast<std::uint32_t>(mesh.ToVertex(hh).Index);
                nextProp.Vector()[i] = static_cast<std::uint32_t>(mesh.NextHalfedge(hh).Index);
                const auto face = mesh.Face(hh);
                faceProp.Vector()[i] = face.IsValid()
                    ? static_cast<std::uint32_t>(face.Index)
                    : kInvalidIndex;
            }
        }

        // ---- Faces ----------------------------------------------------------
        auto& fComp = registry.emplace_or_replace<Faces>(entity);
        fComp.Properties = mesh.FaceProperties();
        fComp.NumDeleted = fSize - mesh.FaceCount();

        {
            RemovePropertyIfPresent(fComp.Properties, PropertyNames::kFaceHalfedge);
            auto heProp = fComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kFaceHalfedge}, kInvalidIndex);
            heProp.Vector().resize(fSize);

            for (std::size_t i = 0; i < fSize; ++i)
            {
                const Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(i)};
                const auto he = mesh.Halfedge(fh);
                heProp.Vector()[i] = he.IsValid()
                    ? static_cast<std::uint32_t>(he.Index)
                    : kInvalidIndex;
            }
        }
    }

    void PopulateFromGraph(entt::registry& registry,
                           entt::entity entity,
                           Geometry::Graph::Graph& graph)
    {
        ResetGeometrySourceComponents(registry, entity);

        // Compact so the resulting GeometrySources have no deleted-index gaps.
        if (graph.HasGarbage())
            graph.GarbageCollection();

        const std::size_t vSize = graph.VerticesSize();
        const std::size_t eSize = graph.EdgesSize();

        // ---- Nodes ----------------------------------------------------------
        auto& nComp = registry.emplace_or_replace<Nodes>(entity);
        nComp.Properties = graph.VertexProperties();
        nComp.NumDeleted = 0;

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
        auto& eComp = registry.emplace_or_replace<Edges>(entity);
        eComp.Properties = graph.EdgeProperties();
        eComp.NumDeleted = 0;

        {
            auto v0Prop = eComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kEdgeV0}, 0u);
            auto v1Prop = eComp.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kEdgeV1}, 0u);
            v0Prop.Vector().resize(eSize);
            v1Prop.Vector().resize(eSize);

            for (std::size_t i = 0; i < eSize; ++i)
            {
                const Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(i)};
                const auto [v0, v1] = graph.EdgeVertices(eh);
                v0Prop.Vector()[i] = static_cast<std::uint32_t>(v0.Index);
                v1Prop.Vector()[i] = static_cast<std::uint32_t>(v1.Index);
            }
        }

        // Mark the entity as a graph so DetectDomain returns `Graph` without
        // requiring a Halfedges PropertySet (graph halfedges remain internal
        // to `Geometry::Graph` and are not promoted to GeometrySources).
        registry.emplace_or_replace<HasGraphTopology>(entity);
    }

    void PopulateFromCloud(entt::registry& registry,
                           entt::entity entity,
                           Geometry::PointCloud::Cloud& cloud)
    {
        ResetGeometrySourceComponents(registry, entity);

        const std::size_t pSize = cloud.VerticesSize();

        auto& vComp = registry.emplace_or_replace<Vertices>(entity);
        vComp.Properties = cloud.PointProperties();
        vComp.NumDeleted = pSize - cloud.VertexCount();

        {
            const auto positions = cloud.Positions();
            auto posProp = vComp.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kPosition}, glm::vec3(0.0f));
            posProp.Vector().assign(positions.begin(), positions.end());
        }

        if (cloud.HasNormals())
        {
            const auto normals = cloud.Normals();
            auto normProp = vComp.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kNormal}, glm::vec3(0.0f, 1.0f, 0.0f));
            normProp.Vector().assign(normals.begin(), normals.end());
        }
    }
}
