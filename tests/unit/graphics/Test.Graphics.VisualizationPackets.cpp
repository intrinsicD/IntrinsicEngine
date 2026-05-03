#include <gtest/gtest.h>

#include <array>
#include <limits>

#include <glm/glm.hpp>

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

