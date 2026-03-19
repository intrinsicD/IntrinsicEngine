#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

TEST(HtexPatch, BuildPatchMetadataProducesOneEntryPerEdge)
{
    auto mesh = MakeTetrahedron();

    const auto patches = Geometry::HtexPatch::BuildPatchMetadata(mesh);
    ASSERT_EQ(patches.size(), mesh.EdgeCount());

    for (const auto& patch : patches)
    {
        EXPECT_LT(patch.EdgeIndex, mesh.EdgesSize());
        EXPECT_LT(patch.LayerIndex, patches.size());
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
    ASSERT_EQ(patches.size(), mesh.EdgeCount());

    for (const auto& patch : patches)
    {
        EXPECT_NE(patch.Flags & Geometry::HtexPatch::Boundary, 0u);

        const bool face0Valid = patch.Face0Index != Geometry::HtexPatch::kInvalidIndex;
        const bool face1Valid = patch.Face1Index != Geometry::HtexPatch::kInvalidIndex;
        EXPECT_NE(face0Valid, face1Valid);
    }
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

