#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

TEST(HtexPatch, BuildPatchMetadataProducesOneEntryPerEdge)
{
    auto mesh = MakeTetrahedron();

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_TRUE(patches.has_value());
    const auto& result = *patches;
    const auto& metadata = result.Patches;
    ASSERT_EQ(metadata.size(), mesh.EdgeCount());
    EXPECT_EQ(result.BoundaryPatchCount, 0u);
    EXPECT_EQ(result.InteriorPatchCount, mesh.EdgeCount());
    EXPECT_GE(result.MaxAssignedResolution, 8u);

    for (const auto& patch : metadata)
    {
        EXPECT_LT(patch.EdgeIndex, mesh.EdgesSize());
        EXPECT_LT(patch.LayerIndex, metadata.size());
        EXPECT_GE(patch.Resolution, 8u);
        EXPECT_LE(patch.Resolution, 128u);
        EXPECT_EQ(patch.Flags & Geometry::HtexPatch::Boundary, 0u);
        EXPECT_NE(patch.Face0Index, Geometry::HtexPatch::kInvalidIndex);
        EXPECT_NE(patch.Face1Index, Geometry::HtexPatch::kInvalidIndex);
    }
}

TEST(HtexPatch, BoundaryEdgesAreFlaggedAndHaveOneIncidentFace)
{
    auto mesh = MakeSingleTriangle();

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_TRUE(patches.has_value());
    const auto& result = *patches;
    const auto& metadata = result.Patches;
    ASSERT_EQ(metadata.size(), mesh.EdgeCount());
    EXPECT_EQ(result.BoundaryPatchCount, mesh.EdgeCount());
    EXPECT_EQ(result.InteriorPatchCount, 0u);

    for (const auto& patch : metadata)
    {
        EXPECT_NE(patch.Flags & Geometry::HtexPatch::Boundary, 0u);

        const bool face0Valid = patch.Face0Index != Geometry::HtexPatch::kInvalidIndex;
        const bool face1Valid = patch.Face1Index != Geometry::HtexPatch::kInvalidIndex;
        EXPECT_NE(face0Valid, face1Valid);
    }
}

TEST(HtexPatch, BuildPatchMetadataRejectsEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    EXPECT_FALSE(Geometry::HtexPatch::BuildPatchMetadata(mesh).has_value());
}

TEST(HtexPatch, TriangleToPatchUVReflectsOrientation)
{
    const glm::vec2 uv{0.25f, 0.75f};

    const glm::vec2 canonical = Geometry::HtexPatch::TriangleToPatchUV(12u, uv, 5u);
    EXPECT_FLOAT_EQ(canonical.x, uv.x);
    EXPECT_FLOAT_EQ(canonical.y, uv.y);

    const glm::vec2 reflected = Geometry::HtexPatch::TriangleToPatchUV(5u, uv, 12u);
    EXPECT_FLOAT_EQ(reflected.x, 0.75f);
    EXPECT_FLOAT_EQ(reflected.y, 0.25f);
}

TEST(HtexPatch, PatchToTriangleUVInvertsPatchOrientation)
{
    const glm::vec2 patchUV{0.2f, 0.35f};

    const glm::vec2 canonical = Geometry::HtexPatch::PatchToTriangleUV(12u, patchUV, 5u);
    EXPECT_FLOAT_EQ(canonical.x, patchUV.x);
    EXPECT_FLOAT_EQ(canonical.y, patchUV.y);

    const glm::vec2 reflected = Geometry::HtexPatch::PatchToTriangleUV(5u, patchUV, 12u);
    EXPECT_FLOAT_EQ(reflected.x, 0.8f);
    EXPECT_FLOAT_EQ(reflected.y, 0.65f);
}

TEST(HtexPatch, IsTriangleLocalUVRejectsTexelsOutsideHalfedgeTriangle)
{
    EXPECT_TRUE(Geometry::HtexPatch::IsTriangleLocalUV(glm::vec2{0.1f, 0.2f}));
    EXPECT_TRUE(Geometry::HtexPatch::IsTriangleLocalUV(glm::vec2{0.0f, 1.0f}));
    EXPECT_FALSE(Geometry::HtexPatch::IsTriangleLocalUV(glm::vec2{-0.01f, 0.2f}));
    EXPECT_FALSE(Geometry::HtexPatch::IsTriangleLocalUV(glm::vec2{0.75f, 0.5f}));
}


TEST(HtexPatch, ComputeAtlasLayoutPacksPatchTilesIntoBoundedGrid)
{
    const auto layout = Geometry::HtexPatch::ComputeAtlasLayout(10u, 8u, 3u);

    EXPECT_EQ(layout.TileSize, 8u);
    EXPECT_EQ(layout.Columns, 3u);
    EXPECT_EQ(layout.Rows, 4u);
    EXPECT_EQ(layout.Width, 24u);
    EXPECT_EQ(layout.Height, 32u);
}

TEST(HtexPatch, BuildPatchMetadataHandlesReversedResolutionBounds)
{
    auto mesh = MakeSingleTriangle();

    Geometry::HtexPatch::PatchBuildParams params{};
    params.TexelsPerUnit = 32.0f;
    params.MinResolution = 64u;
    params.MaxResolution = 8u;

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh, params);
    ASSERT_TRUE(patches.has_value());

    for (const auto& patch : patches->Patches)
    {
        EXPECT_GE(patch.Resolution, 8u);
        EXPECT_LE(patch.Resolution, 64u);
    }
}

TEST(HtexPatch, BuildHalfedgeTriangleKeepsValidSliverFaces)
{
    Geometry::Halfedge::Mesh mesh;
    const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({1.0e-5f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({0.0f, 1.0e-5f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    const auto h0 = mesh.Halfedge(Geometry::FaceHandle{0});
    const auto tri = Geometry::HtexPatch::BuildHalfedgeTriangle(mesh, static_cast<uint32_t>(h0.Index));
    ASSERT_TRUE(tri.has_value());

    EXPECT_EQ(std::count(tri->Vertices.begin(), tri->Vertices.end(), v0), 1);
    EXPECT_EQ(std::count(tri->Vertices.begin(), tri->Vertices.end(), v1), 1);
    EXPECT_EQ(std::count(tri->Vertices.begin(), tri->Vertices.end(), v2), 1);
    EXPECT_NE(tri->Vertices[0], tri->Vertices[1]);
    EXPECT_NE(tri->Vertices[1], tri->Vertices[2]);
    EXPECT_NE(tri->Vertices[2], tri->Vertices[0]);

    const glm::vec3 e0 = tri->Positions[1] - tri->Positions[0];
    const glm::vec3 e1 = tri->Positions[2] - tri->Positions[0];
    EXPECT_GT(glm::length(glm::cross(e0, e1)), 0.0f);
}

TEST(HtexPatch, BuildCategoricalPatchAtlasHandlesValidSliverTriangles)
{
    Geometry::Halfedge::Mesh mesh;
    const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({1.0e-5f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({0.0f, 1.0e-5f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_TRUE(patches.has_value());

    const std::vector<glm::vec3> centroids{
        mesh.Position(v0),
        mesh.Position(v1),
        mesh.Position(v2),
    };

    Geometry::HtexPatch::PatchAtlasLayout layout{};
    std::vector<uint32_t> atlasTexels;
    ASSERT_TRUE(Geometry::HtexPatch::BuildCategoricalPatchAtlas(
        mesh,
        patches->Patches,
        centroids,
        atlasTexels,
        layout,
        Geometry::HtexPatch::kInvalidIndex));

    ASSERT_EQ(atlasTexels.size(), static_cast<size_t>(layout.Width) * static_cast<size_t>(layout.Height));
    EXPECT_TRUE(std::any_of(
        atlasTexels.begin(),
        atlasTexels.end(),
        [](uint32_t texel)
        {
            return texel != Geometry::HtexPatch::kInvalidIndex;
        }));
}

TEST(HtexPatch, BuildCategoricalPatchAtlasEncodesNearestClusterIdsLosslessly)
{
    auto mesh = MakeSingleTriangle();
    const std::vector<glm::vec3> centroids{
        mesh.Position(Geometry::VertexHandle{0u}),
        mesh.Position(Geometry::VertexHandle{1u}),
        mesh.Position(Geometry::VertexHandle{2u}),
    };

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_TRUE(patches.has_value());

    Geometry::HtexPatch::PatchAtlasLayout layout{};
    std::vector<uint32_t> atlasTexels;
    ASSERT_TRUE(Geometry::HtexPatch::BuildCategoricalPatchAtlas(
        mesh,
        patches->Patches,
        centroids,
        atlasTexels,
        layout,
        Geometry::HtexPatch::kInvalidIndex));

    ASSERT_EQ(atlasTexels.size(), static_cast<size_t>(layout.Width) * static_cast<size_t>(layout.Height));

    bool saw0 = false;
    bool saw1 = false;
    bool saw2 = false;
    for (uint32_t texel : atlasTexels)
    {
        EXPECT_TRUE(texel == Geometry::HtexPatch::kInvalidIndex || texel == 0u || texel == 1u || texel == 2u);
        saw0 |= (texel == 0u);
        saw1 |= (texel == 1u);
        saw2 |= (texel == 2u);
    }

    EXPECT_TRUE(saw0);
    EXPECT_TRUE(saw1);
    EXPECT_TRUE(saw2);
}

TEST(HtexPatch, BuildCategoricalPatchAtlasRejectsMissingCentroids)
{
    auto mesh = MakeSingleTriangle();
    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_TRUE(patches.has_value());

    Geometry::HtexPatch::PatchAtlasLayout layout{};
    std::vector<uint32_t> atlasTexels;
    EXPECT_FALSE(Geometry::HtexPatch::BuildCategoricalPatchAtlas(
        mesh,
        patches->Patches,
        {},
        atlasTexels,
        layout,
        Geometry::HtexPatch::kInvalidIndex));
    EXPECT_EQ(layout.Width, 16u);
    EXPECT_EQ(layout.Height, 16u);
    ASSERT_EQ(atlasTexels.size(), static_cast<size_t>(layout.Width) * static_cast<size_t>(layout.Height));
    EXPECT_TRUE(std::all_of(
        atlasTexels.begin(),
        atlasTexels.end(),
        [](uint32_t texel)
        {
            return texel == Geometry::HtexPatch::kInvalidIndex;
        }));
}
