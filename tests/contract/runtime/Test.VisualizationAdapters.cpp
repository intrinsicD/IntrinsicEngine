#include <cstdint>
#include <limits>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Geometry.Properties;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.VisualizationAdapters;

namespace G = Extrinsic::Graphics;
namespace R = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Geometry::PropertySet MakeScalarProperties()
    {
        Geometry::PropertySet properties;
        properties.Resize(4u);

        auto curvature = properties.Add<float>("curvature", 0.0f);
        curvature[0] = -1.0f;
        curvature[1] = 0.25f;
        curvature[2] = 2.0f;
        curvature[3] = 1.0f;

        auto heat = properties.Add<double>("heat", 0.0);
        heat[0] = 10.0;
        heat[1] = 12.0;
        heat[2] = 11.0;
        heat[3] = 13.0;

        auto color = properties.Add<glm::vec3>("color", glm::vec3{1.0f});
        color[0] = glm::vec3{1.0f, 0.0f, 0.0f};

        auto kmeansColor = properties.Add<glm::vec4>("v:kmeans_color", glm::vec4{1.0f});
        kmeansColor[0] = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        kmeansColor[1] = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
        kmeansColor[2] = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
        kmeansColor[3] = glm::vec4{1.0f, 1.0f, 0.0f, 1.0f};

        auto velocity = properties.Add<glm::vec3>("velocity", glm::vec3{0.0f});
        velocity[0] = glm::vec3{1.0f, 0.0f, 0.0f};
        velocity[1] = glm::vec3{0.0f, 1.0f, 0.0f};
        velocity[2] = glm::vec3{0.0f, 0.0f, 1.0f};
        velocity[3] = glm::vec3{-1.0f, 0.0f, 0.0f};

        return properties;
    }
}

TEST(VisualizationAdapters, PropertyScalarAdapterAppendsFloatScalarPacket)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .Domain = G::VisualizationAttributeDomain::Face,
                       .BufferBDA = 0x1000u,
                       .Colormap = G::Colormap::Type::Plasma,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    const G::ScalarAttributePacket& packet = batch.Scalars.front();
    EXPECT_EQ(packet.Name, "curvature");
    EXPECT_EQ(packet.Domain, G::VisualizationAttributeDomain::Face);
    EXPECT_EQ(packet.ElementCount, 4u);
    EXPECT_FLOAT_EQ(packet.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(packet.RangeMax, 2.0f);
    EXPECT_EQ(packet.Colormap, G::Colormap::Type::Plasma);
    EXPECT_EQ(packet.ScalarBufferBDA, 0x1000u);

    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);
    EXPECT_EQ(stats.MissingSourceCount, 0u);
    EXPECT_EQ(stats.InvalidRangeCount, 0u);

    const G::VisualizationDiagnostics diagnostics =
        G::ValidateVisualizationPackets(batch.AsPacketBatch(
            true,
            G::VisualizationAttributeDomain::Face));
    EXPECT_EQ(diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(diagnostics.HasErrors);
}

TEST(VisualizationAdapters, PropertyScalarAdapterSupportsDoubleAndManualRange)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "heat",
                       .OutputName = "normalized_heat",
                       .Domain = G::VisualizationAttributeDomain::Vertex,
                       .BufferBDA = 0x2000u,
                       .AutoRange = false,
                       .RangeMin = 0.0f,
                       .RangeMax = 20.0f,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    EXPECT_EQ(batch.Scalars.front().Name, "normalized_heat");
    EXPECT_EQ(batch.Scalars.front().ElementCount, 4u);
    EXPECT_FLOAT_EQ(batch.Scalars.front().RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(batch.Scalars.front().RangeMax, 20.0f);
    EXPECT_EQ(stats.ManualRangeCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);
}

TEST(VisualizationAdapters, PropertyScalarAdapterExpandsFlatAutoRange)
{
    Geometry::PropertySet properties;
    properties.Resize(3u);
    auto density = properties.Add<float>("density", 4.0f);
    density[0] = 4.0f;
    density[1] = 4.0f;
    density[2] = 4.0f;

    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};
    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};

    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "density",
                       .BufferBDA = 0x3000u,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    EXPECT_FLOAT_EQ(batch.Scalars.front().RangeMin, 3.5f);
    EXPECT_FLOAT_EQ(batch.Scalars.front().RangeMax, 4.5f);
    EXPECT_EQ(stats.FlatAutoRangeExpandedCount, 1u);
}

TEST(VisualizationAdapters, PropertyScalarAdapterRejectsInvalidSources)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};

    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "missing",
                       .BufferBDA = 0x4000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "color",
                       .BufferBDA = 0x4000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .BufferBDA = 0u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .BufferBDA = 0x4000u,
                       .AutoRange = false,
                       .RangeMin = 5.0f,
                       .RangeMax = 5.0f,
                   },
                   stats);

    EXPECT_TRUE(batch.Scalars.empty());
    EXPECT_EQ(stats.AdapterInvocationCount, 4u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.UnsupportedSourceTypeCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 1u);
    EXPECT_EQ(stats.InvalidRangeCount, 1u);
}

TEST(VisualizationAdapters, PropertyScalarAdapterRejectsEmptyAndNonFiniteSources)
{
    Geometry::PropertySet emptyProperties;
    auto empty = emptyProperties.Add<float>("empty", 0.0f);
    ASSERT_TRUE(empty.IsValid());
    const R::PropertyScalarAdapter emptyAdapter{
        Geometry::ConstPropertySet{emptyProperties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    emptyAdapter.Append(batch,
                        R::VisualizationAdapterOptions{
                            .SourceName = "empty",
                            .BufferBDA = 0x5000u,
                        },
                        stats);

    Geometry::PropertySet badProperties;
    badProperties.Resize(2u);
    auto bad = badProperties.Add<float>("bad", 0.0f);
    bad[0] = 1.0f;
    bad[1] = std::numeric_limits<float>::infinity();
    const R::PropertyScalarAdapter badAdapter{
        Geometry::ConstPropertySet{badProperties}};
    badAdapter.Append(batch,
                      R::VisualizationAdapterOptions{
                          .SourceName = "bad",
                          .BufferBDA = 0x5000u,
                      },
                      stats);

    EXPECT_TRUE(batch.Scalars.empty());
    EXPECT_EQ(stats.EmptySourceCount, 1u);
    EXPECT_EQ(stats.NonFiniteValueCount, 1u);
}

TEST(VisualizationAdapters, KMeansLabelAdapterAppendsColorPacket)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::KMeansLabelAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "v:kmeans_color",
                       .OutputName = "clusters",
                       .Domain = G::VisualizationAttributeDomain::Vertex,
                       .ColorBufferBDA = 0x6000u,
                   },
                   stats);

    ASSERT_EQ(batch.Colors.size(), 1u);
    const G::ColorAttributePacket& packet = batch.Colors.front();
    EXPECT_EQ(packet.Name, "clusters");
    EXPECT_EQ(packet.Domain, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packet.ElementCount, 4u);
    EXPECT_EQ(packet.ColorBufferBDA, 0x6000u);
    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);

    const G::VisualizationDiagnostics diagnostics =
        G::ValidateVisualizationPackets(batch.AsPacketBatch(
            true,
            G::VisualizationAttributeDomain::Vertex));
    EXPECT_EQ(diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(diagnostics.HasErrors);
}

TEST(VisualizationAdapters, KMeansLabelAdapterRejectsInvalidSources)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::KMeansLabelAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "missing",
                       .ColorBufferBDA = 0x7000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "color",
                       .ColorBufferBDA = 0x7000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "v:kmeans_color",
                       .ColorBufferBDA = 0u,
                   },
                   stats);

    Geometry::PropertySet badProperties;
    badProperties.Resize(2u);
    auto badColors = badProperties.Add<glm::vec4>("v:kmeans_color", glm::vec4{1.0f});
    badColors[1] = glm::vec4{0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f};
    const R::KMeansLabelAdapter badAdapter{
        Geometry::ConstPropertySet{badProperties}};
    badAdapter.Append(batch,
                      R::VisualizationAdapterOptions{
                          .SourceName = "v:kmeans_color",
                          .ColorBufferBDA = 0x7000u,
                      },
                      stats);

    EXPECT_TRUE(batch.Colors.empty());
    EXPECT_EQ(stats.AdapterInvocationCount, 4u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.UnsupportedSourceTypeCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 1u);
    EXPECT_EQ(stats.NonFiniteValueCount, 1u);
}

TEST(VisualizationAdapters, VectorFieldAdapterAppendsVectorFieldPacket)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::VectorFieldAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "velocity",
                       .OutputName = "flow",
                       .Domain = G::VisualizationAttributeDomain::Vertex,
                       .PositionBufferBDA = 0x8000u,
                       .VectorBufferBDA = 0x9000u,
                       .VectorScale = 2.5f,
                       .VectorColor = glm::vec4{0.25f, 0.5f, 1.0f, 1.0f},
                       .DepthTested = false,
                   },
                   stats);

    ASSERT_EQ(batch.VectorFields.size(), 1u);
    const G::VectorFieldOverlayPacket& packet = batch.VectorFields.front();
    EXPECT_EQ(packet.Name, "flow");
    EXPECT_EQ(packet.Domain, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packet.ElementCount, 4u);
    EXPECT_EQ(packet.PositionBufferBDA, 0x8000u);
    EXPECT_EQ(packet.VectorBufferBDA, 0x9000u);
    EXPECT_FLOAT_EQ(packet.Scale, 2.5f);
    EXPECT_EQ(packet.Color, (glm::vec4{0.25f, 0.5f, 1.0f, 1.0f}));
    EXPECT_FALSE(packet.DepthTested);
    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);

    const G::VisualizationDiagnostics diagnostics =
        G::ValidateVisualizationPackets(batch.AsPacketBatch(
            true,
            G::VisualizationAttributeDomain::Vertex));
    EXPECT_EQ(diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(diagnostics.HasErrors);
}

TEST(VisualizationAdapters, VectorFieldAdapterRejectsInvalidSources)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::VectorFieldAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "missing",
                       .PositionBufferBDA = 0xA000u,
                       .VectorBufferBDA = 0xB000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .PositionBufferBDA = 0xA000u,
                       .VectorBufferBDA = 0xB000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "velocity",
                       .PositionBufferBDA = 0u,
                       .VectorBufferBDA = 0xB000u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "velocity",
                       .PositionBufferBDA = 0xA000u,
                       .VectorBufferBDA = 0xB000u,
                       .VectorScale = 0.0f,
                   },
                   stats);

    Geometry::PropertySet badProperties;
    badProperties.Resize(2u);
    auto badVectors = badProperties.Add<glm::vec3>("velocity", glm::vec3{0.0f});
    badVectors[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    badVectors[1] = glm::vec3{0.0f, std::numeric_limits<float>::infinity(), 0.0f};
    const R::VectorFieldAdapter badAdapter{
        Geometry::ConstPropertySet{badProperties}};
    badAdapter.Append(batch,
                      R::VisualizationAdapterOptions{
                          .SourceName = "velocity",
                          .PositionBufferBDA = 0xA000u,
                          .VectorBufferBDA = 0xB000u,
                      },
                      stats);

    EXPECT_TRUE(batch.VectorFields.empty());
    EXPECT_EQ(stats.AdapterInvocationCount, 5u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.UnsupportedSourceTypeCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 1u);
    EXPECT_EQ(stats.InvalidRangeCount, 1u);
    EXPECT_EQ(stats.NonFiniteValueCount, 1u);
}

TEST(VisualizationAdapters, IsolineAdapterAppendsIsolinePacket)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::IsolineAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .OutputName = "curvature_isolines",
                       .Domain = G::VisualizationAttributeDomain::Vertex,
                       .IsoValueCount = 3u,
                       .LineWidth = 2.0f,
                       .OverlayColor = glm::vec4{0.1f, 0.2f, 0.3f, 1.0f},
                       .DepthTested = false,
                   },
                   stats);

    ASSERT_EQ(batch.Isolines.size(), 1u);
    const G::IsolineOverlayPacket& packet = batch.Isolines.front();
    EXPECT_EQ(packet.SourceScalarName, "curvature_isolines");
    EXPECT_EQ(packet.Domain, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packet.IsoValueCount, 3u);
    EXPECT_FLOAT_EQ(packet.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(packet.RangeMax, 2.0f);
    EXPECT_FLOAT_EQ(packet.LineWidth, 2.0f);
    EXPECT_EQ(packet.Color, (glm::vec4{0.1f, 0.2f, 0.3f, 1.0f}));
    EXPECT_FALSE(packet.DepthTested);
    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);

    const G::VisualizationDiagnostics diagnostics =
        G::ValidateVisualizationPackets(batch.AsPacketBatch(
            true,
            G::VisualizationAttributeDomain::Vertex));
    EXPECT_EQ(diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(diagnostics.HasErrors);
}

TEST(VisualizationAdapters, IsolineAdapterRejectsInvalidSources)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::IsolineAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "missing",
                       .IsoValueCount = 2u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "color",
                       .IsoValueCount = 2u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .IsoValueCount = 0u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .AutoRange = false,
                       .RangeMin = 3.0f,
                       .RangeMax = 3.0f,
                       .IsoValueCount = 2u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .IsoValueCount = 2u,
                       .LineWidth = 0.0f,
                   },
                   stats);

    Geometry::PropertySet badProperties;
    badProperties.Resize(2u);
    auto bad = badProperties.Add<double>("curvature", 0.0);
    bad[0] = 1.0;
    bad[1] = std::numeric_limits<double>::infinity();
    const R::IsolineAdapter badAdapter{
        Geometry::ConstPropertySet{badProperties}};
    badAdapter.Append(batch,
                      R::VisualizationAdapterOptions{
                          .SourceName = "curvature",
                          .IsoValueCount = 2u,
                      },
                      stats);

    EXPECT_TRUE(batch.Isolines.empty());
    EXPECT_EQ(stats.AdapterInvocationCount, 6u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.UnsupportedSourceTypeCount, 1u);
    EXPECT_EQ(stats.InvalidRangeCount, 3u);
    EXPECT_EQ(stats.NonFiniteValueCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 0u);
}

TEST(VisualizationAdapters, BatchClearAndPacketViewReflectAllPacketLanes)
{
    R::VisualizationAdapterBatch batch{};
    batch.AttributeBuffers.push_back(G::VisualizationAttributeBufferPacket{
        .Name = "raw",
        .ElementCount = 1u,
        .BufferBDA = 0x1000u,
    });
    batch.Scalars.push_back(G::ScalarAttributePacket{
        .Name = "scalar",
        .ElementCount = 1u,
        .ScalarBufferBDA = 0x2000u,
    });
    batch.Colors.push_back(G::ColorAttributePacket{
        .Name = "color",
        .ElementCount = 1u,
        .ColorBufferBDA = 0x3000u,
    });
    batch.VectorFields.push_back(G::VectorFieldOverlayPacket{
        .Name = "vectors",
        .ElementCount = 1u,
        .PositionBufferBDA = 0x4000u,
        .VectorBufferBDA = 0x5000u,
    });
    batch.Isolines.push_back(G::IsolineOverlayPacket{
        .SourceScalarName = "iso",
        .IsoValueCount = 2u,
    });

    const G::VisualizationPacketBatch packetView =
        batch.AsPacketBatch(true, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packetView.AttributeBuffers.size(), 1u);
    EXPECT_EQ(packetView.Scalars.size(), 1u);
    EXPECT_EQ(packetView.Colors.size(), 1u);
    EXPECT_EQ(packetView.VectorFields.size(), 1u);
    EXPECT_EQ(packetView.Isolines.size(), 1u);
    EXPECT_TRUE(packetView.EnforceDomain);

    batch.Clear();
    EXPECT_TRUE(batch.AttributeBuffers.empty());
    EXPECT_TRUE(batch.Scalars.empty());
    EXPECT_TRUE(batch.Colors.empty());
    EXPECT_TRUE(batch.VectorFields.empty());
    EXPECT_TRUE(batch.Isolines.empty());
}

TEST(VisualizationAdapters, RegistryReplacesAndUnregistersAdapters)
{
    Geometry::PropertySet firstProperties = MakeScalarProperties();
    Geometry::PropertySet secondProperties = MakeScalarProperties();
    const R::PropertyScalarAdapter first{
        Geometry::ConstPropertySet{firstProperties}};
    const R::PropertyScalarAdapter second{
        Geometry::ConstPropertySet{secondProperties}};

    R::VisualizationAdapterRegistry registry{};
    EXPECT_TRUE(registry.Empty());

    registry.Register(7u, first);
    EXPECT_TRUE(registry.Contains(7u));
    EXPECT_EQ(registry.Find(7u), &first);
    EXPECT_EQ(registry.Size(), 1u);

    registry.Register(7u, second);
    EXPECT_EQ(registry.Find(7u), &second);
    EXPECT_EQ(registry.Size(), 1u);

    EXPECT_FALSE(registry.Unregister(9u));
    EXPECT_TRUE(registry.Unregister(7u));
    EXPECT_FALSE(registry.Contains(7u));
    EXPECT_TRUE(registry.Empty());

    registry.Register(8u, first);
    registry.Clear();
    EXPECT_TRUE(registry.Empty());
}
