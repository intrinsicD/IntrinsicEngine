#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

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

TEST(HtexPatch, BuildCategoricalPatchAtlasEncodesVertexLabelsLosslessly)
{
    auto mesh = MakeSingleTriangle();
    auto labels = Geometry::VertexProperty<uint32_t>(
        mesh.VertexProperties().GetOrAdd<uint32_t>("v:kmeans_label", 0u));

    labels[Geometry::VertexHandle{0u}] = 3u;
    labels[Geometry::VertexHandle{1u}] = 7u;
    labels[Geometry::VertexHandle{2u}] = 11u;

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_TRUE(patches.has_value());

    Geometry::HtexPatch::PatchAtlasLayout layout{};
    std::vector<uint32_t> atlasTexels;
    ASSERT_TRUE(Geometry::HtexPatch::BuildCategoricalPatchAtlas(
        mesh,
        patches->Patches,
        labels,
        atlasTexels,
        layout,
        Geometry::HtexPatch::kInvalidIndex));

    ASSERT_EQ(atlasTexels.size(), static_cast<size_t>(layout.Width) * static_cast<size_t>(layout.Height));

    bool saw3 = false;
    bool saw7 = false;
    bool saw11 = false;
    for (uint32_t texel : atlasTexels)
    {
        EXPECT_TRUE(texel == Geometry::HtexPatch::kInvalidIndex || texel == 3u || texel == 7u || texel == 11u);
        saw3 |= (texel == 3u);
        saw7 |= (texel == 7u);
        saw11 |= (texel == 11u);
    }

    EXPECT_TRUE(saw3);
    EXPECT_TRUE(saw7);
    EXPECT_TRUE(saw11);
}

TEST(HtexPatch, BuildCategoricalPatchAtlasRejectsMissingLabelField)
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
