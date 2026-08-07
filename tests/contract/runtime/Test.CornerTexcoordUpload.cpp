// BUG-137 Slice B — the corner->halfedge map that lets GPU upload resolve
// corner-domain UVs. `Extrinsic.Runtime.GeometryPlanBuilders` is a PRIVATE
// module, so the split behavior it drives is asserted through the public
// extraction path in Test.MeshGeometryExtraction.cpp; this file covers the
// public topology seam that feeds it.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Geometry.Properties;

namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    // Two triangles sharing edge (v0, v2):
    //
    //   v3 ---- v2          f0 ring = [v1, v2, v0]
    //   |     / |            f1 ring = [v2, v3, v0]
    //   |   /   |
    //   v0 ---- v1
    //
    // v0 and v2 each have two interior corners, so they are the vertices that
    // can carry disagreeing UVs.
    struct QuadSources
    {
        entt::registry Registry{};
        entt::entity Entity{entt::null};
    };

    void MakeSharedEdgeQuad(QuadSources& out)
    {
        out.Entity = out.Registry.create();
        auto& vertices = out.Registry.emplace<gs::Vertices>(out.Entity);
        auto& halfedges = out.Registry.emplace<gs::Halfedges>(out.Entity);
        (void)out.Registry.emplace<gs::Edges>(out.Entity);
        auto& faces = out.Registry.emplace<gs::Faces>(out.Entity);

        vertices.Properties.Resize(4u);
        auto positions = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{pn::kPosition}, glm::vec3{0.0F});
        positions.Vector() = {
            glm::vec3{0.0F, 0.0F, 0.0F},
            glm::vec3{1.0F, 0.0F, 0.0F},
            glm::vec3{1.0F, 1.0F, 0.0F},
            glm::vec3{0.0F, 1.0F, 0.0F},
        };

        halfedges.Properties.Resize(6u);
        auto toVertex = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{pn::kHalfedgeToVertex}, 0u);
        auto next = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{pn::kHalfedgeNext}, 0u);
        auto halfedgeFace = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{pn::kHalfedgeFace}, 0u);
        toVertex.Vector() = {1u, 2u, 0u, 2u, 3u, 0u};
        next.Vector() = {1u, 2u, 0u, 4u, 5u, 3u};
        halfedgeFace.Vector() = {0u, 0u, 0u, 1u, 1u, 1u};

        faces.Properties.Resize(2u);
        auto faceHalfedge = faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{pn::kFaceHalfedge}, 0u);
        faceHalfedge.Vector() = {0u, 3u};
    }
}

TEST(CornerTexcoordUpload, CornerHalfedgeMapIsParallelToTheTriangleList)
{
    QuadSources sources{};
    MakeSharedEdgeQuad(sources);

    const auto view = gs::BuildConstView(sources.Registry, sources.Entity);
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> triangleToFace;
    std::vector<std::uint32_t> cornerHalfedges;
    const auto status =
        Extrinsic::Runtime::BuildMeshSurfaceTriangleCornerTopology(
            view, indices, triangleToFace, cornerHalfedges);

    ASSERT_EQ(status, Extrinsic::Runtime::MeshSurfaceTopologyStatus::Success);
    EXPECT_EQ(indices.size(), 6u);
    EXPECT_EQ(triangleToFace.size(), 2u);
    ASSERT_EQ(cornerHalfedges.size(), indices.size());

    const auto& halfedges = sources.Registry.get<gs::Halfedges>(sources.Entity);
    const auto toVertex = halfedges.Properties.Get<std::uint32_t>(
        std::string{pn::kHalfedgeToVertex});
    ASSERT_TRUE(static_cast<bool>(toVertex));

    // The invariant the upload split depends on: every emitted corner's
    // halfedge actually points at that corner's vertex, so a corner-domain
    // attribute indexed by halfedge lands on the right vertex.
    for (std::size_t i = 0u; i < indices.size(); ++i)
    {
        ASSERT_LT(cornerHalfedges[i], toVertex.Vector().size());
        EXPECT_EQ(toVertex.Vector()[cornerHalfedges[i]], indices[i]);
    }
}

TEST(CornerTexcoordUpload, CornerMapIsClearedOnTopologyFailure)
{
    QuadSources sources{};
    MakeSharedEdgeQuad(sources);
    // Break the face ring so the walk fails after it has emitted corners.
    auto& halfedges = sources.Registry.get<gs::Halfedges>(sources.Entity);
    auto next = halfedges.Properties.Get<std::uint32_t>(std::string{pn::kHalfedgeNext});
    ASSERT_TRUE(static_cast<bool>(next));
    next.Vector()[1u] = 999u;

    const auto view = gs::BuildConstView(sources.Registry, sources.Entity);
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> triangleToFace;
    std::vector<std::uint32_t> cornerHalfedges;
    const auto status =
        Extrinsic::Runtime::BuildMeshSurfaceTriangleCornerTopology(
            view, indices, triangleToFace, cornerHalfedges);

    EXPECT_NE(status, Extrinsic::Runtime::MeshSurfaceTopologyStatus::Success);
    EXPECT_TRUE(indices.empty());
    EXPECT_TRUE(cornerHalfedges.empty());
}
