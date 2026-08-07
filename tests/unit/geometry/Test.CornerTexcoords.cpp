// BUG-137 Slice A — corner-domain texture coordinates and the
// corner-over-vertex resolution order.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    using Geometry::HalfedgeHandle;
    using Geometry::MeshUtils::TexcoordDomain;

    // Two triangles sharing edge (v1, v2): a minimal mesh with interior
    // vertices whose corners can disagree.
    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeSharedEdgeQuad()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0F, 0.0F, 0.0F});
        const auto v1 = mesh.AddVertex({1.0F, 0.0F, 0.0F});
        const auto v2 = mesh.AddVertex({1.0F, 1.0F, 0.0F});
        const auto v3 = mesh.AddVertex({0.0F, 1.0F, 0.0F});
        (void)mesh.AddTriangle(v0, v1, v2);
        (void)mesh.AddTriangle(v0, v2, v3);
        return mesh;
    }

    void AddVertexUvs(Geometry::HalfedgeMesh::Mesh& mesh, const glm::vec2 value)
    {
        auto uvs = mesh.VertexProperties().GetOrAdd<glm::vec2>(
            Geometry::MeshUtils::kVertexTexcoordPropertyName, value);
        for (glm::vec2& uv : uvs.Vector())
            uv = value;
    }

    void AddCornerUvs(Geometry::HalfedgeMesh::Mesh& mesh, const glm::vec2 value)
    {
        auto uvs = mesh.HalfedgeProperties().GetOrAdd<glm::vec2>(
            Geometry::MeshUtils::kHalfedgeTexcoordPropertyName, value);
        for (glm::vec2& uv : uvs.Vector())
            uv = value;
    }
}

TEST(CornerTexcoords, ResolvesNoneWhenMeshCarriesNoUvs)
{
    const auto mesh = MakeSharedEdgeQuad();
    EXPECT_EQ(Geometry::MeshUtils::ResolveTexcoordDomain(mesh), TexcoordDomain::None);
}

TEST(CornerTexcoords, ResolvesVertexDomainWhenOnlyVertexUvsExist)
{
    auto mesh = MakeSharedEdgeQuad();
    AddVertexUvs(mesh, glm::vec2{0.25F, 0.75F});
    EXPECT_EQ(Geometry::MeshUtils::ResolveTexcoordDomain(mesh), TexcoordDomain::Vertex);
}

TEST(CornerTexcoords, CornerUvsWinOverVertexUvs)
{
    auto mesh = MakeSharedEdgeQuad();
    AddVertexUvs(mesh, glm::vec2{0.25F, 0.75F});
    AddCornerUvs(mesh, glm::vec2{0.5F, 0.5F});
    EXPECT_EQ(Geometry::MeshUtils::ResolveTexcoordDomain(mesh), TexcoordDomain::Halfedge);

    glm::vec2 uv{};
    ASSERT_TRUE(Geometry::MeshUtils::TryGetCornerTexcoord(mesh, HalfedgeHandle{0u}, uv));
    EXPECT_FLOAT_EQ(uv.x, 0.5F);
    EXPECT_FLOAT_EQ(uv.y, 0.5F);
}

TEST(CornerTexcoords, CornerReadFallsBackToTargetVertexUv)
{
    auto mesh = MakeSharedEdgeQuad();
    auto uvs = mesh.VertexProperties().GetOrAdd<glm::vec2>(
        Geometry::MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0F});
    for (std::size_t i = 0u; i < uvs.Vector().size(); ++i)
        uvs.Vector()[i] = glm::vec2{static_cast<float>(i), 0.0F};

    const HalfedgeHandle h{0u};
    const auto target = mesh.ToVertex(h);

    glm::vec2 uv{};
    ASSERT_TRUE(Geometry::MeshUtils::TryGetCornerTexcoord(mesh, h, uv));
    EXPECT_FLOAT_EQ(uv.x, static_cast<float>(target.Index));
}

TEST(CornerTexcoords, MismatchedPropertySizeIsIgnoredRatherThanTrusted)
{
    auto mesh = MakeSharedEdgeQuad();
    AddVertexUvs(mesh, glm::vec2{0.25F, 0.75F});
    // Author a corner property, then desynchronize it from the halfedge count.
    AddCornerUvs(mesh, glm::vec2{0.5F, 0.5F});
    auto corner = mesh.HalfedgeProperties().Get<glm::vec2>(
        Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
    ASSERT_TRUE(static_cast<bool>(corner));
    corner.Vector().pop_back();

    // A stale buffer must not be read out of range; resolution falls back.
    EXPECT_EQ(Geometry::MeshUtils::ResolveTexcoordDomain(mesh), TexcoordDomain::Vertex);
}

TEST(CornerTexcoords, UniformCornerUvsHaveNoSeams)
{
    auto mesh = MakeSharedEdgeQuad();
    AddCornerUvs(mesh, glm::vec2{0.5F, 0.5F});
    EXPECT_FALSE(Geometry::MeshUtils::HasTexcoordSeams(mesh));
    EXPECT_EQ(Geometry::MeshUtils::CountTexcoordSplitVertices(mesh), mesh.VertexCount());
}

TEST(CornerTexcoords, VertexOwnedUvsCanNeverProduceSeams)
{
    auto mesh = MakeSharedEdgeQuad();
    auto uvs = mesh.VertexProperties().GetOrAdd<glm::vec2>(
        Geometry::MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0F});
    for (std::size_t i = 0u; i < uvs.Vector().size(); ++i)
        uvs.Vector()[i] = glm::vec2{static_cast<float>(i), 0.0F};

    EXPECT_FALSE(Geometry::MeshUtils::HasTexcoordSeams(mesh));
    EXPECT_EQ(Geometry::MeshUtils::CountTexcoordSplitVertices(mesh), mesh.VertexCount());
}

TEST(CornerTexcoords, DisagreeingCornersAtOneVertexAreASeam)
{
    auto mesh = MakeSharedEdgeQuad();
    AddCornerUvs(mesh, glm::vec2{0.5F, 0.5F});
    auto corner = mesh.HalfedgeProperties().Get<glm::vec2>(
        Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
    ASSERT_TRUE(static_cast<bool>(corner));

    // Find two live halfedges pointing at the same vertex and split their UVs.
    bool seamAuthored = false;
    const std::size_t halfedgeCount = mesh.HalfedgesSize();
    for (std::size_t a = 0u; a < halfedgeCount && !seamAuthored; ++a)
    {
        const HalfedgeHandle ha{static_cast<std::uint32_t>(a)};
        if (mesh.IsDeleted(ha))
            continue;
        for (std::size_t b = a + 1u; b < halfedgeCount; ++b)
        {
            const HalfedgeHandle hb{static_cast<std::uint32_t>(b)};
            if (mesh.IsDeleted(hb))
                continue;
            if (mesh.ToVertex(ha).Index != mesh.ToVertex(hb).Index)
                continue;
            corner.Vector()[b] = glm::vec2{0.9F, 0.1F};
            seamAuthored = true;
            break;
        }
    }
    ASSERT_TRUE(seamAuthored);

    EXPECT_TRUE(Geometry::MeshUtils::HasTexcoordSeams(mesh));
    EXPECT_GT(Geometry::MeshUtils::CountTexcoordSplitVertices(mesh), mesh.VertexCount());
}

TEST(CornerTexcoords, SeamEpsilonSuppressesNegligibleDifferences)
{
    auto mesh = MakeSharedEdgeQuad();
    AddCornerUvs(mesh, glm::vec2{0.5F, 0.5F});
    auto corner = mesh.HalfedgeProperties().Get<glm::vec2>(
        Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
    ASSERT_TRUE(static_cast<bool>(corner));
    corner.Vector()[1] = glm::vec2{0.5F + 1.0e-6F, 0.5F};

    EXPECT_TRUE(Geometry::MeshUtils::HasTexcoordSeams(mesh, 0.0F));
    EXPECT_FALSE(Geometry::MeshUtils::HasTexcoordSeams(mesh, 1.0e-4F));
}
