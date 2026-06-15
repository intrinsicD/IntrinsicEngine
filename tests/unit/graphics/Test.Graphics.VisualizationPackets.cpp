#include <gtest/gtest.h>

#include <array>
#include <limits>

#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;

using namespace Extrinsic;

TEST(GraphicsVisualizationPackets, ValidPacketsProduceAcceptedDiagnosticsAndSummary)
{
    const std::array<Graphics::VisualizationAttributeBufferPacket, 1> attributes{{
        Graphics::VisualizationAttributeBufferPacket{
            .Name = "temperature",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ValueType = Graphics::VisualizationValueType::ScalarFloat,
            .ElementCount = 12u,
            .BufferBDA = 0x1000u,
        },
    }};
    const std::array<Graphics::ScalarAttributePacket, 1> scalars{{
        Graphics::ScalarAttributePacket{
            .Name = "temperature",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 12u,
            .RangeMin = -1.f,
            .RangeMax = 2.f,
            .Colormap = Graphics::Colormap::Type::Viridis,
            .ScalarBufferBDA = 0x1000u,
        },
    }};
    const std::array<Graphics::ColorAttributePacket, 1> colors{{
        Graphics::ColorAttributePacket{
            .Name = "labels_rgba",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 12u,
            .ColorBufferBDA = 0x2000u,
        },
    }};
    const std::array<Graphics::VectorFieldOverlayPacket, 1> vectors{{
        Graphics::VectorFieldOverlayPacket{
            .Name = "velocity",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 4u,
            .PositionBufferBDA = 0x3000u,
            .VectorBufferBDA = 0x4000u,
            .Scale = 0.5f,
            .Color = {0.f, 1.f, 0.f, 1.f},
        },
    }};
    const std::array<Graphics::IsolineOverlayPacket, 1> isolines{{
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "temperature",
            .Domain = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount = 5u,
            .RangeMin = -1.f,
            .RangeMax = 2.f,
            .LineWidth = 1.5f,
            .Color = {1.f, 1.f, 1.f, 1.f},
        },
    }};
    const std::array<Graphics::HtexPatchPreviewAtlasPacket, 1> htex{{
        Graphics::HtexPatchPreviewAtlasPacket{
            .Name = "patch_preview",
            .PatchCount = 8u,
            .AtlasWidth = 256u,
            .AtlasHeight = 128u,
        },
    }};

    const Graphics::VisualizationPacketBatch batch{
        .AttributeBuffers = attributes,
        .Scalars = scalars,
        .Colors = colors,
        .VectorFields = vectors,
        .Isolines = isolines,
        .HtexAtlases = htex,
    };

    const Graphics::VisualizationDiagnostics diagnostics = Graphics::ValidateVisualizationPackets(batch);
    EXPECT_EQ(diagnostics.InputPacketCount, 6u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 6u);
    EXPECT_EQ(diagnostics.TextureResidencyDeferredCount, 1u);
    EXPECT_FALSE(diagnostics.HasErrors);

    const Graphics::VisualizationOverlaySummary summary = Graphics::BuildVisualizationOverlaySummary(batch);
    EXPECT_EQ(summary.VectorFieldCount, 1u);
    EXPECT_EQ(summary.VectorGlyphCount, 4u);
    EXPECT_EQ(summary.IsolineLayerCount, 1u);
    EXPECT_EQ(summary.IsolineValueCount, 5u);
    EXPECT_EQ(summary.HtexAtlasDescriptorCount, 1u);
    EXPECT_TRUE(summary.RequiresTextureResidency);
}

TEST(GraphicsVisualizationPackets, InvalidRangesAndColormapsAreDiagnosed)
{
    const std::array<Graphics::ScalarAttributePacket, 3> scalars{{
        Graphics::ScalarAttributePacket{
            .Name = "bad_range",
            .ElementCount = 3u,
            .RangeMin = 1.f,
            .RangeMax = 1.f,
            .ScalarBufferBDA = 0x1000u,
        },
        Graphics::ScalarAttributePacket{
            .Name = "bad_map",
            .ElementCount = 3u,
            .RangeMin = 0.f,
            .RangeMax = 1.f,
            .Colormap = static_cast<Graphics::Colormap::Type>(255u),
            .ScalarBufferBDA = 0x1000u,
        },
        Graphics::ScalarAttributePacket{
            .Name = "missing_bda",
            .ElementCount = 3u,
            .RangeMin = 0.f,
            .RangeMax = 1.f,
        },
    }};

    const Graphics::VisualizationDiagnostics diagnostics = Graphics::ValidateVisualizationPackets(
        Graphics::VisualizationPacketBatch{.Scalars = scalars});

    EXPECT_EQ(diagnostics.InputPacketCount, 3u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 0u);
    EXPECT_EQ(diagnostics.InvalidRangeCount, 1u);
    EXPECT_EQ(diagnostics.UnsupportedColormapCount, 1u);
    EXPECT_EQ(diagnostics.MissingAttributeCount, 1u);
    EXPECT_TRUE(diagnostics.HasErrors);
}

TEST(GraphicsVisualizationPackets, DomainMismatchesAndInvalidOverlayPacketsAreDiagnosed)
{
    const std::array<Graphics::ColorAttributePacket, 1> colors{{
        Graphics::ColorAttributePacket{
            .Name = "face_colors",
            .Domain = Graphics::VisualizationAttributeDomain::Face,
            .ElementCount = 4u,
            .ColorBufferBDA = 0x2000u,
        },
    }};
    const std::array<Graphics::VectorFieldOverlayPacket, 1> vectors{{
        Graphics::VectorFieldOverlayPacket{
            .Name = "bad_vectors",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 8u,
            .PositionBufferBDA = 0x3000u,
            .VectorBufferBDA = 0u,
            .Scale = -1.f,
            .Color = {1.f, 0.f, 0.f, 1.f},
        },
    }};
    const std::array<Graphics::IsolineOverlayPacket, 1> isolines{{
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "temperature",
            .Domain = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount = 0u,
            .RangeMin = 0.f,
            .RangeMax = std::numeric_limits<float>::infinity(),
            .LineWidth = 0.f,
        },
    }};

    const Graphics::VisualizationDiagnostics diagnostics = Graphics::ValidateVisualizationPackets(
        Graphics::VisualizationPacketBatch{
            .Colors = colors,
            .VectorFields = vectors,
            .Isolines = isolines,
            .EnforceDomain = true,
            .ExpectedDomain = Graphics::VisualizationAttributeDomain::Vertex,
        });

    EXPECT_EQ(diagnostics.InputPacketCount, 3u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_EQ(diagnostics.DomainMismatchCount, 2u);
    EXPECT_EQ(diagnostics.MissingAttributeCount, 2u);
    EXPECT_EQ(diagnostics.InvalidRangeCount, 2u);
    EXPECT_TRUE(diagnostics.HasErrors);
}

TEST(GraphicsVisualizationPackets, InvalidHtexAtlasDescriptorsRemainDataOnlyDiagnostics)
{
    const std::array<Graphics::HtexPatchPreviewAtlasPacket, 2> htex{{
        Graphics::HtexPatchPreviewAtlasPacket{.Name = "missing_size", .PatchCount = 2u},
        Graphics::HtexPatchPreviewAtlasPacket{.Name = "valid_descriptor", .PatchCount = 2u, .AtlasWidth = 64u, .AtlasHeight = 64u},
    }};

    const Graphics::VisualizationDiagnostics diagnostics = Graphics::ValidateVisualizationPackets(
        Graphics::VisualizationPacketBatch{.HtexAtlases = htex});

    EXPECT_EQ(diagnostics.InputPacketCount, 2u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_EQ(diagnostics.InvalidResourceCount, 1u);
    EXPECT_EQ(diagnostics.TextureResidencyDeferredCount, 1u);
    EXPECT_TRUE(diagnostics.HasErrors);
}

TEST(GraphicsVisualizationPackets, FragmentBakesCanUseExistingTexcoordsOrForcedHtex)
{
    const std::array<Graphics::FragmentBakeAtlasPacket, 3> bakes{{
        Graphics::FragmentBakeAtlasPacket{
            .Name = "kmeans_labels_uv_bake",
            .SourceAttributeName = "kmeans_label_rgba",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 24u,
            .AtlasWidth = 512u,
            .AtlasHeight = 512u,
            .TexcoordBufferBDA = 0xCAFEu,
            .AtlasTextureAsset = Assets::AssetId{301u, 1u},
            .TexcoordProvenance =
                Graphics::VisualizationTexcoordProvenance::GeneratedAtlas,
            .TexcoordDirtyStamp = 42u,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "kmeans_labels_htex_bake",
            .SourceAttributeName = "kmeans_label_rgba",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::RecreateHtex,
            .MeshHasTexcoords = true,
            .FaceCount = 24u,
            .AtlasWidth = 512u,
            .AtlasHeight = 512u,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "existing_htex_bake",
            .SourceAttributeName = "curvature",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingHtex,
            .MeshHasTexcoords = false,
            .FaceCount = 24u,
            .AtlasWidth = 256u,
            .AtlasHeight = 256u,
        },
    }};

    const Graphics::VisualizationPacketBatch batch{.FragmentBakeAtlases = bakes};
    const Graphics::VisualizationDiagnostics diagnostics = Graphics::ValidateVisualizationPackets(batch);
    EXPECT_EQ(diagnostics.InputPacketCount, 3u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 3u);
    EXPECT_EQ(diagnostics.MissingTexcoordCount, 0u);
    EXPECT_EQ(diagnostics.HtexRecreateRequestCount, 1u);
    EXPECT_EQ(diagnostics.TextureResidencyDeferredCount, 3u);
    EXPECT_FALSE(diagnostics.HasErrors);

    const Graphics::VisualizationOverlaySummary summary = Graphics::BuildVisualizationOverlaySummary(batch);
    EXPECT_EQ(summary.UvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.GeneratedUvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.AuthoredUvBakeAtlasDescriptorCount, 0u);
    EXPECT_EQ(summary.RuntimeResolvedUvBakeAtlasDescriptorCount, 0u);
    EXPECT_EQ(summary.HtexBakeAtlasDescriptorCount, 2u);
    EXPECT_EQ(summary.HtexRecreateRequestCount, 1u);
    EXPECT_EQ(summary.FragmentBakeTextureAssetDescriptorCount, 1u);
    EXPECT_TRUE(summary.RequiresTextureResidency);
}

TEST(GraphicsVisualizationPackets, ResolvedUvBakeProvenanceIsReportedWithoutImplicitHtex)
{
    const std::array<Graphics::FragmentBakeAtlasPacket, 3> bakes{{
        Graphics::FragmentBakeAtlasPacket{
            .Name = "authored_uv_bake",
            .SourceAttributeName = "albedo",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 8u,
            .AtlasWidth = 128u,
            .AtlasHeight = 128u,
            .TexcoordBufferBDA = 0x1000u,
            .TexcoordProvenance =
                Graphics::VisualizationTexcoordProvenance::Authored,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "runtime_resolved_uv_bake",
            .SourceAttributeName = "normal",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 8u,
            .AtlasWidth = 128u,
            .AtlasHeight = 128u,
            .TexcoordBufferBDA = 0x2000u,
            .TexcoordProvenance =
                Graphics::VisualizationTexcoordProvenance::RuntimeResolved,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "forced_htex_bake",
            .SourceAttributeName = "labels",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingHtex,
            .MeshHasTexcoords = true,
            .FaceCount = 8u,
            .AtlasWidth = 128u,
            .AtlasHeight = 128u,
        },
    }};

    const Graphics::VisualizationPacketBatch batch{.FragmentBakeAtlases = bakes};
    const Graphics::VisualizationDiagnostics diagnostics =
        Graphics::ValidateVisualizationPackets(batch);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 3u);
    EXPECT_EQ(diagnostics.MissingTexcoordCount, 0u);
    EXPECT_FALSE(diagnostics.HasErrors);

    const Graphics::VisualizationOverlaySummary summary =
        Graphics::BuildVisualizationOverlaySummary(batch);
    EXPECT_EQ(summary.UvBakeAtlasDescriptorCount, 2u);
    EXPECT_EQ(summary.AuthoredUvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.RuntimeResolvedUvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.HtexBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.HtexRecreateRequestCount, 0u);
}

TEST(GraphicsVisualizationPackets, GeneratedBakeTextureSemanticsAreSummarized)
{
    const std::array<Graphics::FragmentBakeAtlasPacket, 7> bakes{{
        Graphics::FragmentBakeAtlasPacket{
            .Name = "scalar_bake",
            .SourceAttributeName = "curvature",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{501u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::ScalarAttribute,
            .SourceAttributeDirtyStamp = 11u,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "label_bake",
            .SourceAttributeName = "cluster_id",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{502u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::LabelAttribute,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "vector2_bake",
            .SourceAttributeName = "uv_flow",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{503u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::Vector2Attribute,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "vector4_bake",
            .SourceAttributeName = "weights",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{504u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::Vector4Attribute,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "pbr_albedo_bake",
            .SourceAttributeName = "v:color",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{505u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::PbrAlbedo,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "pbr_normal_bake",
            .SourceAttributeName = "v:normal",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{506u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::PbrNormal,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "displacement_bake",
            .SourceAttributeName = "height",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 4u,
            .AtlasWidth = 64u,
            .AtlasHeight = 64u,
            .TexcoordBufferBDA = 0x1000u,
            .AtlasTextureAsset = Assets::AssetId{507u, 1u},
            .GeneratedTextureSemantic =
                Graphics::VisualizationGeneratedTextureSemantic::Displacement,
        },
    }};

    const Graphics::VisualizationPacketBatch batch{.FragmentBakeAtlases = bakes};
    const Graphics::VisualizationDiagnostics diagnostics =
        Graphics::ValidateVisualizationPackets(batch);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, bakes.size());
    EXPECT_EQ(diagnostics.InvalidResourceCount, 0u);
    EXPECT_FALSE(diagnostics.HasErrors);

    const Graphics::VisualizationOverlaySummary summary =
        Graphics::BuildVisualizationOverlaySummary(batch);
    EXPECT_EQ(summary.FragmentBakeTextureAssetDescriptorCount, 7u);
    EXPECT_EQ(summary.ScalarBakeTextureAssetDescriptorCount, 1u);
    EXPECT_EQ(summary.LabelBakeTextureAssetDescriptorCount, 1u);
    EXPECT_EQ(summary.VectorBakeTextureAssetDescriptorCount, 2u);
    EXPECT_EQ(summary.PbrBakeTextureAssetDescriptorCount, 2u);
    EXPECT_EQ(summary.DisplacementBakeTextureAssetDescriptorCount, 1u);
    EXPECT_TRUE(summary.RequiresTextureResidency);
}

TEST(GraphicsVisualizationPackets, TexcoordBakesRequireExistingUvData)
{
    const std::array<Graphics::FragmentBakeAtlasPacket, 2> bakes{{
        Graphics::FragmentBakeAtlasPacket{
            .Name = "missing_uv_mesh",
            .SourceAttributeName = "kmeans_label_rgba",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = false,
            .FaceCount = 12u,
            .AtlasWidth = 256u,
            .AtlasHeight = 256u,
        },
        Graphics::FragmentBakeAtlasPacket{
            .Name = "invalid_shape",
            .SourceAttributeName = "kmeans_label_rgba",
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 12u,
            .AtlasWidth = 0u,
            .AtlasHeight = 256u,
            .TexcoordBufferBDA = 0x1234u,
        },
    }};

    const Graphics::VisualizationDiagnostics diagnostics = Graphics::ValidateVisualizationPackets(
        Graphics::VisualizationPacketBatch{.FragmentBakeAtlases = bakes});
    EXPECT_EQ(diagnostics.InputPacketCount, 2u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 0u);
    EXPECT_EQ(diagnostics.MissingTexcoordCount, 1u);
    EXPECT_EQ(diagnostics.InvalidResourceCount, 1u);
    EXPECT_EQ(diagnostics.TextureResidencyDeferredCount, 0u);
    EXPECT_TRUE(diagnostics.HasErrors);

    const Graphics::VisualizationOverlaySummary summary = Graphics::BuildVisualizationOverlaySummary(
        Graphics::VisualizationPacketBatch{.FragmentBakeAtlases = bakes});
    EXPECT_EQ(summary.UvBakeAtlasDescriptorCount, 0u);
    EXPECT_EQ(summary.HtexBakeAtlasDescriptorCount, 0u);
}
