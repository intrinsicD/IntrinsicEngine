module;
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"

namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    namespace GS = ECS::Components::GeometrySources;
    using D = GeometryElementDomain;
    PointPropertyWatch ObserveGeometryProperty(const GeometryEntityAvailability& a, D domain, std::string name)
    {
        const auto* props = ResolveGeometryPropertySet(a, domain);
        return {domain, name, props ? props->Size() : 0,
                props ? props->FindPropertyRevision(name) : std::nullopt};
    }
    Geometry::PropertySet *MutableGeometryProperties(entt::registry &raw, entt::entity entity, D domain)
    {
        auto view = GS::BuildMutableView(raw, entity);
        switch (domain)
        {
        case D::MeshVertex:
        case D::GraphNode:
        case D::PointCloudPoint:
            return view.VertexSource ? &view.VertexSource->Properties : nullptr;
        case D::MeshEdge:
        case D::GraphEdge:
            return view.EdgeSource ? &view.EdgeSource->Properties : nullptr;
        case D::MeshHalfedge:
        case D::GraphHalfedge:
            return view.HalfedgeSource ? &view.HalfedgeSource->Properties : nullptr;
        case D::MeshFace:
            return view.FaceSource ? &view.FaceSource->Properties : nullptr;
        default:
            return nullptr;
        }
    }
    D PrimaryPointDomain(const GeometryEntityAvailability &a)
    {
        for (auto d : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
            if (SupportsGeometryElementDomain(a, d))
                return d;
        return D::Unknown;
    }
    bool FinitePosition(glm::vec3 p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    bool GeometryPropertiesCurrent(const EditorGeometryProcessingContext& context, entt::entity entity,
                       std::span<const PointPropertyWatch> inputs)
    {
        if (!context.Scene || !context.Scene->Raw().valid(entity)) return false;
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
        for (const auto& w : inputs)
            if (ObserveGeometryProperty(a, w.Domain, w.Name) != w) return false;
        return true;
    }
}
