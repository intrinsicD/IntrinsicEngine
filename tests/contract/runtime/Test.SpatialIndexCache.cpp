#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <vector>
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
namespace
{
    R::GeometryPropertyRef Ref(D domain, const char* name = "samples")
    {
        return {.Domain = domain, .Name = name, .ValueKind = Geometry::PropertyValueKind::Vec3};
    }
} // namespace
TEST(SpatialIndexCache, ReusesOnlyUnchangedPropertyAndLiveElementIdentity)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("cache-test");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(3);
    auto positions = props.GetOrAdd<glm::vec3>("samples");
    positions.Vector() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    auto deleted = props.GetOrAdd<bool>("v:deleted");
    deleted[0] = true;
    auto first = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    ASSERT_TRUE(first.Ready()) << first.Diagnostic;
    auto nearest = cache.Nearest(first.Handle, {0, 0, 0});
    ASSERT_TRUE(nearest);
    EXPECT_EQ(nearest->Index, 1);
    auto knn = cache.KNearest(first.Handle, {}, 8, 1);
    ASSERT_TRUE(knn);
    ASSERT_EQ(knn->size(), 1);
    EXPECT_EQ(knn->front().Index, 2);
    EXPECT_EQ(cache.Nearest(first.Handle, {}, 1)->Index, 2);
    EXPECT_EQ(cache.Radius(first.Handle, {}, 3, 0, 1)->TotalCount, 1);
    EXPECT_EQ(cache.KNearest(first.Handle, {}, 8, 0)->size(), 2); // Deleted slot excludes nothing.
    EXPECT_TRUE(cache.KNearest(first.Handle, {}, 0)->empty());
    EXPECT_EQ(cache.QueueGpuKNearest(first.Handle, std::vector<glm::vec3>{{}}, 3)->State,
              R::SpatialQueryState::Failed);
    auto second = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    EXPECT_TRUE(second.Reused);
    EXPECT_EQ(first.Handle.Value, second.Handle.Value);
    auto color = props.GetOrAdd<glm::vec3>("unrelated");
    color[0] = {1, 0, 0};
    EXPECT_TRUE(cache.Acquire(world, entity, Ref(D::PointCloudPoint)).Reused);
    positions[1] = {10, 0, 0};
    EXPECT_FALSE(cache.Nearest(first.Handle, {}));
    EXPECT_FALSE(cache.KNearest(first.Handle, {}, 3));
    auto changed = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    ASSERT_TRUE(changed.Ready());
    EXPECT_FALSE(changed.Reused);
    EXPECT_EQ(cache.Nearest(changed.Handle, {})->Index, 2);
    deleted[0] = false;
    EXPECT_FALSE(cache.Nearest(changed.Handle, {}));
    changed = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    EXPECT_EQ(cache.Nearest(changed.Handle, {})->Index, 0);
    const auto radius = cache.Radius(changed.Handle, {}, 3, 1);
    ASSERT_TRUE(radius);
    EXPECT_EQ(radius->TotalCount, 2);
    EXPECT_TRUE(radius->Overflowed());
    worlds.Clear();
    cache.Prune();
    EXPECT_FALSE(cache.Nearest(changed.Handle, {}));
    EXPECT_EQ(cache.Stats().Builds, 3);
    EXPECT_EQ(cache.Stats().Hits, 2);
    EXPECT_EQ(cache.Stats().Evictions, 3);
}
TEST(SpatialIndexCache, CanonicalDomainsAndWorldEntityIdentity)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("cache-test");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    Geometry::HalfedgeMesh::Mesh mesh;
    auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}),
         c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    auto entity = scene.Create();
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto check = [&](D domain, Geometry::PropertySet& props) {
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::uint32_t i = 0; i < props.Size(); ++i)
            samples[i] = {float(i), 0, 0};
        auto value = cache.Acquire(world, entity, Ref(domain));
        ASSERT_TRUE(value.Ready()) << value.Diagnostic;
        auto n = cache.Nearest(value.Handle, {});
        ASSERT_TRUE(n);
        EXPECT_EQ(n->Index, 0);
        auto neighbors = cache.KNearest(value.Handle, {}, 64, 0);
        ASSERT_TRUE(neighbors);
        ASSERT_EQ(neighbors->size(), props.Size()-1);
        for (std::size_t i=0; i<neighbors->size(); ++i) EXPECT_EQ((*neighbors)[i].Index, i+1);
    };
    check(D::MeshVertex, scene.Raw().get<GS::Vertices>(entity).Properties);
    check(D::MeshFace, scene.Raw().get<GS::Faces>(entity).Properties);
    check(D::MeshEdge, scene.Raw().get<GS::Edges>(entity).Properties);
    check(D::MeshHalfedge, scene.Raw().get<GS::Halfedges>(entity).Properties);
    auto wrong = Ref(D::MeshFace);
    wrong.ValueKind = Geometry::PropertyValueKind::Float;
    EXPECT_FALSE(cache.Acquire(world, entity, wrong).Ready());
    const auto other = worlds.CreateWorld("other");
    auto& scene2 = *worlds.Get(other);
    auto entity2 = scene2.Create();
    auto& p = scene2.Raw().emplace<GS::Vertices>(entity2).Properties;
    p.Resize(1);
    p.GetOrAdd<glm::vec3>("samples")[0] = {9, 0, 0};
    auto one = cache.Acquire(world, entity, Ref(D::MeshVertex)),
         two = cache.Acquire(other, entity2, Ref(D::PointCloudPoint));
    ASSERT_TRUE(one.Ready());
    ASSERT_TRUE(two.Ready());
    EXPECT_NE(one.Handle.Value, two.Handle.Value);
    EXPECT_FLOAT_EQ(cache.Nearest(two.Handle, {})->SquaredDistance, 81);
    scene2.Destroy(entity2);
    cache.Prune();
    EXPECT_FALSE(cache.Nearest(two.Handle, {}));
    EXPECT_TRUE(cache.Nearest(one.Handle, {}));
    Geometry::Graph::Graph graph;
    const auto ga = graph.AddVertex({0, 0, 0}), gb = graph.AddVertex({1, 0, 0});
    (void)graph.AddEdge(ga, gb);
    GS::PopulateFromGraph(scene.Raw(), entity, graph);
    check(D::GraphNode, scene.Raw().get<GS::Vertices>(entity).Properties);
    check(D::GraphEdge, scene.Raw().get<GS::Edges>(entity).Properties);
    check(D::GraphHalfedge, scene.Raw().get<GS::Halfedges>(entity).Properties);
    EXPECT_FALSE(cache.Nearest(one.Handle, {}));
}
