#include <gtest/gtest.h>

#include <memory>

#include <glm/glm.hpp>
#include <entt/entity/fwd.hpp>

import ECS;
import Graphics;
import Geometry;

TEST(PrimitiveBVHSync, BuildsTriangleBvhFromMeshCollider)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity entity = scene.CreateEntity("MeshBVH");

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f},
    };
    collision->Indices = {0u, 1u, 2u, 0u, 2u, 3u};
    collision->LocalAABB = Geometry::AABB{{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(entity);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& bvh = reg.emplace<ECS::PrimitiveBVH::Data>(entity);
    bvh.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
    bvh.Dirty = true;

    Graphics::Systems::PrimitiveBVHSync::OnUpdate(reg);

    ASSERT_TRUE(bvh.HasValidBVH());
    EXPECT_EQ(bvh.Source, ECS::PrimitiveBVH::SourceKind::MeshTriangles);
    EXPECT_EQ(bvh.PrimitiveCount, 2u);
    EXPECT_EQ(bvh.Triangles.size(), 2u);
    EXPECT_TRUE(bvh.LocalBounds.IsValid());

    std::vector<Geometry::BVH::ElementIndex> hits;
    bvh.LocalBVH.QueryRay(Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, hits);
    EXPECT_EQ(hits.size(), 2u);
}

TEST(PrimitiveBVHSync, BuildsSegmentBvhFromGraph)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity entity = scene.CreateEntity("GraphBVH");

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    const auto v0 = graph->AddVertex(glm::vec3(-1.0f, 0.0f, 0.0f));
    const auto v1 = graph->AddVertex(glm::vec3( 0.0f, 1.0f, 0.0f));
    const auto v2 = graph->AddVertex(glm::vec3( 1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(graph->AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph->AddEdge(v1, v2).has_value());

    auto& gd = reg.emplace<ECS::Graph::Data>(entity);
    gd.GraphRef = graph;
    gd.GpuDirty = true;

    auto& bvh = reg.emplace<ECS::PrimitiveBVH::Data>(entity);
    bvh.Source = ECS::PrimitiveBVH::SourceKind::GraphSegments;
    bvh.Dirty = true;

    Graphics::Systems::PrimitiveBVHSync::OnUpdate(reg);

    ASSERT_TRUE(bvh.HasValidBVH());
    EXPECT_EQ(bvh.Source, ECS::PrimitiveBVH::SourceKind::GraphSegments);
    EXPECT_EQ(bvh.PrimitiveCount, 2u);
    EXPECT_EQ(bvh.Segments.size(), 2u);
}

