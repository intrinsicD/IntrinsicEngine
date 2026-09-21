#include <array>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

#include "PointDomainFixture.hpp"

import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.Graph;
import Geometry.HalfedgeMesh;

namespace Intrinsic::Tests
{
    namespace R = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using D = R::GeometryElementDomain;

    Geometry::PropertySet& PointDomainProperties(Extrinsic::ECS::Scene::Registry& scene,
                                                entt::entity entity, D domain)
    {
        return *const_cast<Geometry::PropertySet*>(
            R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(scene.Raw(), entity), domain));
    }

    entt::entity MakePointDomainSource(Extrinsic::ECS::Scene::Registry& scene, D domain)
    {
        constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
        auto entity = scene.Create();
        if (domain >= D::MeshVertex && domain <= D::MeshFace)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            auto a = mesh.AddVertex(plane[0]), b = mesh.AddVertex(plane[1]), c = mesh.AddVertex(plane[2]),
                 d = mesh.AddVertex(plane[3]);
            (void)mesh.AddTriangle(a, b, c);
            (void)mesh.AddTriangle(c, b, d);
            GS::PopulateFromMesh(scene.Raw(), entity, mesh);
            if (domain == D::MeshFace)
                scene.Raw().get<GS::Faces>(entity).Properties.Resize(4);
        }
        else if (domain != D::PointCloudPoint)
        {
            Geometry::Graph::Graph graph;
            auto a = graph.AddVertex(plane[0]), b = graph.AddVertex(plane[1]), c = graph.AddVertex(plane[2]),
                 d = graph.AddVertex(plane[3]);
            (void)graph.AddEdge(a, b);
            (void)graph.AddEdge(a, c);
            (void)graph.AddEdge(a, d);
            (void)graph.AddEdge(b, c);
            (void)graph.AddEdge(b, d);
            (void)graph.AddEdge(c, d);
            GS::PopulateFromGraph(scene.Raw(), entity, graph);
        }
        else
            scene.Raw().emplace<GS::Vertices>(entity).Properties.Resize(5);
        return entity;
    }
}
