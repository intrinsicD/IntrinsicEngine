#include <cstdint>
#include <limits>
#include <string>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Geometry.Properties;
import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.VisualizationAdapters;
import Extrinsic.Runtime.StreamingExecutor;

namespace Assets = Extrinsic::Assets;
namespace G = Extrinsic::Graphics;
namespace R = Extrinsic::Runtime;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

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

        auto positions =
            properties.Add<glm::vec3>(std::string{PN::kPosition}, glm::vec3{0.0f});
        positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
        positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        positions[2] = glm::vec3{0.0f, 1.0f, 0.0f};
        positions[3] = glm::vec3{0.0f, 0.0f, 1.0f};

        auto mean = properties.Add<double>(
            std::string{PN::kMeanCurvature},
            0.0);
        mean[0] = 0.5;
        mean[1] = 0.25;
        mean[2] = 0.75;
        mean[3] = 1.0;

        auto gaussian = properties.Add<double>(
            std::string{PN::kGaussianCurvature},
            0.0);
        gaussian[0] = 0.1;
        gaussian[1] = 0.2;
        gaussian[2] = 0.3;
        gaussian[3] = 0.4;

        auto principalDir1 = properties.Add<glm::vec3>(
            std::string{PN::kPrincipalDir1},
            glm::vec3{1.0f, 0.0f, 0.0f});
        principalDir1[1] = glm::vec3{0.0f, 1.0f, 0.0f};
        principalDir1[2] = glm::vec3{0.0f, 0.0f, 1.0f};
        principalDir1[3] = glm::vec3{-1.0f, 0.0f, 0.0f};

        auto principalDir2 = properties.Add<glm::vec3>(
            std::string{PN::kPrincipalDir2},
            glm::vec3{0.0f, 1.0f, 0.0f});
        principalDir2[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        principalDir2[2] = glm::vec3{0.0f, -1.0f, 0.0f};
        principalDir2[3] = glm::vec3{0.0f, 0.0f, 1.0f};

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
    EXPECT_EQ(stats.ScalarValueScanCount, 4u);

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
    EXPECT_EQ(stats.ScalarValueScanCount, 4u);
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
    EXPECT_EQ(stats.ScalarValueScanCount, 3u);
}

// BUG-059 — heavy-tailed fields (curvature spikes at degenerate slivers) must
// not stretch the auto range so far that the surface bulk normalizes into the
// colormap's darkest bin; large sample counts clamp to the [2%, 98%]
// quantiles while outliers saturate at the colormap ends.
TEST(VisualizationAdapters, PropertyScalarAdapterClampsHeavyTailedAutoRange)
{
    constexpr std::size_t kCount = 200u;
    Geometry::PropertySet properties;
    properties.Resize(kCount);
    auto curvature = properties.Add<double>("v:mean_curvature", 0.0);
    for (std::size_t i = 0u; i < kCount; ++i)
    {
        curvature[i] =
            static_cast<double>(i) / static_cast<double>(kCount - 1u);
    }
    curvature[10] = -1.0e6;
    curvature[20] = 1.0e6;

    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};
    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};

    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "v:mean_curvature",
                       .BufferBDA = 0x5000u,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    EXPECT_EQ(stats.RobustAutoRangeClampedCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);
    const G::ScalarAttributePacket& packet = batch.Scalars.front();
    EXPECT_GT(packet.RangeMin, -100.0f);
    EXPECT_LT(packet.RangeMax, 100.0f);
    EXPECT_LT(packet.RangeMin, packet.RangeMax);
}

// BUG-059 — small fields keep exact min/max extremes: quantile clamping only
// engages above the robust-range sample threshold.
TEST(VisualizationAdapters, PropertyScalarAdapterKeepsExactAutoRangeForSmallFields)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "heat",
                       .BufferBDA = 0x5100u,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    EXPECT_EQ(stats.RobustAutoRangeClampedCount, 0u);
    EXPECT_FLOAT_EQ(batch.Scalars.front().RangeMin, 10.0f);
    EXPECT_FLOAT_EQ(batch.Scalars.front().RangeMax, 13.0f);
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
                       .BufferBDA = 0x4000u,
                       .AutoRange = false,
                       .RangeMin = 5.0f,
                       .RangeMax = 5.0f,
                   },
                   stats);

    EXPECT_TRUE(batch.Scalars.empty());
    EXPECT_EQ(stats.AdapterInvocationCount, 3u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.UnsupportedSourceTypeCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 0u);
    EXPECT_EQ(stats.InvalidRangeCount, 1u);
}

TEST(VisualizationAdapters, PropertyScalarAdapterEmitsPropertyBufferWhenBdaMissing)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::PropertyScalarAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .Domain = G::VisualizationAttributeDomain::Vertex,
                       .DirtyStamp = 9u,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    EXPECT_EQ(batch.Scalars.front().SourceBufferKey, "curvature");
    EXPECT_EQ(batch.Scalars.front().ScalarBufferBDA, 0u);
    ASSERT_EQ(batch.PropertyBuffers.size(), 1u);
    const G::VisualizationPropertyBufferUploadDescriptor& descriptor =
        batch.PropertyBuffers.front();
    EXPECT_EQ(descriptor.SourceKey, "curvature");
    EXPECT_EQ(descriptor.Domain, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(descriptor.ValueType, G::VisualizationValueType::ScalarFloat);
    EXPECT_EQ(descriptor.ElementCount, 4u);
    EXPECT_EQ(descriptor.StrideBytes, sizeof(float));
    EXPECT_EQ(descriptor.DirtyStamp, 9u);
    EXPECT_EQ(descriptor.Bytes.size(), 4u * sizeof(float));
    EXPECT_EQ(stats.PacketAppendCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 0u);
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
    EXPECT_EQ(stats.AdapterInvocationCount, 3u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.UnsupportedSourceTypeCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 0u);
    EXPECT_EQ(stats.NonFiniteValueCount, 1u);
}

TEST(VisualizationAdapters, KMeansLabelAdapterEmitsPropertyBufferWhenBdaMissing)
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
                       .DirtyStamp = 10u,
                   },
                   stats);

    ASSERT_EQ(batch.Colors.size(), 1u);
    EXPECT_EQ(batch.Colors.front().Name, "clusters");
    EXPECT_EQ(batch.Colors.front().SourceBufferKey, "clusters");
    EXPECT_EQ(batch.Colors.front().ColorBufferBDA, 0u);
    ASSERT_EQ(batch.PropertyBuffers.size(), 1u);
    const G::VisualizationPropertyBufferUploadDescriptor& descriptor =
        batch.PropertyBuffers.front();
    EXPECT_EQ(descriptor.SourceKey, "clusters");
    EXPECT_EQ(descriptor.ValueType, G::VisualizationValueType::RgbaFloat4);
    EXPECT_EQ(descriptor.ElementCount, 4u);
    EXPECT_EQ(descriptor.StrideBytes, sizeof(glm::vec4));
    EXPECT_EQ(descriptor.DirtyStamp, 10u);
    EXPECT_EQ(descriptor.Bytes.size(), 4u * sizeof(glm::vec4));
    EXPECT_EQ(stats.PacketAppendCount, 1u);
    EXPECT_EQ(stats.InvalidBufferCount, 0u);
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

TEST(VisualizationAdapters, CurvatureVisualizationAdapterAppendsScalarAndPrincipalDirections)
{
    Geometry::PropertySet properties = MakeScalarProperties();
    const R::CurvatureVisualizationAdapter adapter{
        Geometry::ConstPropertySet{properties}};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = std::string{PN::kMeanCurvature},
                       .OutputName = "curvature.mean",
                       .Domain = G::VisualizationAttributeDomain::Vertex,
                       .PositionBufferSourceKey = std::string{PN::kPosition},
                       .DirtyStamp = 17u,
                       .Colormap = G::Colormap::Type::Plasma,
                       .VectorScale = 0.25f,
                       .VectorColor = glm::vec4{1.0f, 0.9f, 0.2f, 1.0f},
                       .DepthTested = false,
                       .EmitPrincipalDirections = true,
                   },
                   stats);

    ASSERT_EQ(batch.Scalars.size(), 1u);
    EXPECT_EQ(batch.Scalars.front().Name, "curvature.mean");
    EXPECT_EQ(batch.Scalars.front().SourceBufferKey, "curvature.mean");
    EXPECT_EQ(batch.Scalars.front().ElementCount, 4u);
    EXPECT_EQ(batch.Scalars.front().Colormap, G::Colormap::Type::Plasma);

    ASSERT_EQ(batch.VectorFields.size(), 2u);
    EXPECT_EQ(batch.VectorFields[0].Name, "curvature.mean.principal_dir1");
    EXPECT_EQ(batch.VectorFields[0].PositionBufferSourceKey,
              std::string{PN::kPosition});
    EXPECT_EQ(batch.VectorFields[0].VectorBufferSourceKey,
              "curvature.mean.principal_dir1");
    EXPECT_EQ(batch.VectorFields[0].ElementCount, 4u);
    EXPECT_FLOAT_EQ(batch.VectorFields[0].Scale, 0.25f);
    EXPECT_FALSE(batch.VectorFields[0].DepthTested);
    EXPECT_EQ(batch.VectorFields[1].Name, "curvature.mean.principal_dir2");
    EXPECT_EQ(batch.VectorFields[1].VectorBufferSourceKey,
              "curvature.mean.principal_dir2");

    ASSERT_EQ(batch.PropertyBuffers.size(), 3u);
    EXPECT_EQ(batch.PropertyBuffers[0].SourceKey, "curvature.mean");
    EXPECT_EQ(batch.PropertyBuffers[0].ValueType,
              G::VisualizationValueType::ScalarFloat);
    EXPECT_EQ(batch.PropertyBuffers[1].SourceKey,
              "curvature.mean.principal_dir1");
    EXPECT_EQ(batch.PropertyBuffers[1].ValueType,
              G::VisualizationValueType::VectorFloat3);
    EXPECT_EQ(batch.PropertyBuffers[2].SourceKey,
              "curvature.mean.principal_dir2");
    EXPECT_EQ(batch.PropertyBuffers[2].ValueType,
              G::VisualizationValueType::VectorFloat3);

    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 3u);
    EXPECT_EQ(stats.MissingSourceCount, 0u);
    EXPECT_EQ(stats.InvalidResourceCount, 0u);

    const G::VisualizationPropertyBufferDiagnostics bufferDiagnostics =
        G::ValidateVisualizationPropertyBufferUploads(batch.PropertyBuffers);
    EXPECT_EQ(bufferDiagnostics.InputBufferCount, 3u);
    EXPECT_EQ(bufferDiagnostics.AcceptedBufferCount, 3u);
    EXPECT_FALSE(bufferDiagnostics.HasErrors);
}

TEST(VisualizationAdapters, CurvatureVisualizationAdapterFallsBackToScalarOnlyForInvalidDirections)
{
    Geometry::PropertySet missingDirections;
    missingDirections.Resize(4u);
    auto mean = missingDirections.Add<double>(
        std::string{PN::kMeanCurvature},
        0.0);
    mean[0] = 0.0;
    mean[1] = 0.5;
    mean[2] = 1.0;
    mean[3] = 1.5;
    auto positions = missingDirections.Add<glm::vec3>(
        std::string{PN::kPosition},
        glm::vec3{0.0f});
    positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};

    const R::CurvatureVisualizationAdapter missingAdapter{
        Geometry::ConstPropertySet{missingDirections}};
    R::VisualizationAdapterBatch missingBatch{};
    R::VisualizationAdapterStats missingStats{};
    missingAdapter.Append(missingBatch,
                          R::VisualizationAdapterOptions{
                              .SourceName = std::string{PN::kMeanCurvature},
                              .PositionBufferSourceKey =
                                  std::string{PN::kPosition},
                              .EmitPrincipalDirections = true,
                          },
                          missingStats);
    ASSERT_EQ(missingBatch.Scalars.size(), 1u);
    EXPECT_TRUE(missingBatch.VectorFields.empty());
    EXPECT_EQ(missingBatch.PropertyBuffers.size(), 1u);
    EXPECT_EQ(missingStats.PacketAppendCount, 1u);
    EXPECT_EQ(missingStats.MissingSourceCount, 2u);
    EXPECT_EQ(missingStats.InvalidResourceCount, 0u);

    Geometry::PropertySet mismatchedDirections = MakeScalarProperties();
    auto dir1 = mismatchedDirections.Get<glm::vec3>(PN::kPrincipalDir1);
    ASSERT_TRUE(dir1.IsValid());
    dir1.Vector().pop_back();
    const R::CurvatureVisualizationAdapter mismatchedAdapter{
        Geometry::ConstPropertySet{mismatchedDirections}};
    R::VisualizationAdapterBatch mismatchedBatch{};
    R::VisualizationAdapterStats mismatchedStats{};
    mismatchedAdapter.Append(mismatchedBatch,
                             R::VisualizationAdapterOptions{
                                 .SourceName =
                                     std::string{PN::kMeanCurvature},
                                 .PositionBufferSourceKey =
                                     std::string{PN::kPosition},
                                 .EmitPrincipalDirections = true,
                             },
                             mismatchedStats);
    ASSERT_EQ(mismatchedBatch.Scalars.size(), 1u);
    EXPECT_TRUE(mismatchedBatch.VectorFields.empty());
    EXPECT_EQ(mismatchedBatch.PropertyBuffers.size(), 1u);
    EXPECT_EQ(mismatchedStats.PacketAppendCount, 1u);
    EXPECT_EQ(mismatchedStats.MissingSourceCount, 0u);
    EXPECT_EQ(mismatchedStats.InvalidResourceCount, 1u);
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
    EXPECT_EQ(packet.ScalarBufferSourceKey, "curvature_isolines");
    EXPECT_EQ(packet.ScalarBufferBDA, 0u);
    EXPECT_EQ(packet.Domain, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packet.IsoValueCount, 3u);
    EXPECT_FLOAT_EQ(packet.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(packet.RangeMax, 2.0f);
    EXPECT_FLOAT_EQ(packet.LineWidth, 2.0f);
    EXPECT_EQ(packet.Color, (glm::vec4{0.1f, 0.2f, 0.3f, 1.0f}));
    EXPECT_FALSE(packet.DepthTested);
    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 1u);
    EXPECT_EQ(stats.ScalarValueScanCount, 8u);
    ASSERT_EQ(batch.PropertyBuffers.size(), 1u);
    EXPECT_EQ(batch.PropertyBuffers.front().SourceKey, "curvature_isolines");
    EXPECT_EQ(batch.PropertyBuffers.front().ValueType,
              G::VisualizationValueType::ScalarFloat);

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
    EXPECT_EQ(stats.ScalarValueScanCount, 14u);
}

TEST(VisualizationAdapters, HtexMetadataAdapterAppendsPreviewAndUvBakePackets)
{
    const R::HtexMetadataAdapter adapter{};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .OutputName = "curvature_uv_bake",
                       .DirtyStamp = 99u,
                       .EmitHtexPreview = true,
                       .EmitFragmentBake = true,
                       .SourceAttributeName = "curvature",
                       .FragmentBakeMapping =
                           G::VisualizationFragmentBakeMapping::ExistingTexcoords,
                       .MeshHasTexcoords = true,
                       .PatchCount = 8u,
                       .FaceCount = 24u,
                       .AtlasWidth = 512u,
                       .AtlasHeight = 256u,
                       .TexcoordBufferBDA = 0xCAFEu,
                       .AtlasTextureAsset = Assets::AssetId{800u, 1u},
                       .GeneratedTextureSemantic =
                           G::VisualizationGeneratedTextureSemantic::ScalarAttribute,
                       .SourceAttributeDirtyStamp = 1234u,
                   },
                   stats);

    ASSERT_EQ(batch.HtexAtlases.size(), 1u);
    ASSERT_EQ(batch.FragmentBakeAtlases.size(), 1u);
    const G::HtexPatchPreviewAtlasPacket& htex = batch.HtexAtlases.front();
    EXPECT_EQ(htex.Name, "curvature_uv_bake");
    EXPECT_EQ(htex.PatchCount, 8u);
    EXPECT_EQ(htex.AtlasWidth, 512u);
    EXPECT_EQ(htex.AtlasHeight, 256u);

    const G::FragmentBakeAtlasPacket& bake = batch.FragmentBakeAtlases.front();
    EXPECT_EQ(bake.Name, "curvature_uv_bake");
    EXPECT_EQ(bake.SourceAttributeName, "curvature");
    EXPECT_EQ(bake.Mapping,
              G::VisualizationFragmentBakeMapping::ExistingTexcoords);
    EXPECT_TRUE(bake.MeshHasTexcoords);
    EXPECT_EQ(bake.FaceCount, 24u);
    EXPECT_EQ(bake.AtlasWidth, 512u);
    EXPECT_EQ(bake.AtlasHeight, 256u);
    EXPECT_EQ(bake.TexcoordBufferBDA, 0xCAFEu);
    EXPECT_EQ(bake.TexcoordProvenance,
              G::VisualizationTexcoordProvenance::RuntimeResolved);
    EXPECT_EQ(bake.TexcoordDirtyStamp, 99u);
    EXPECT_EQ(bake.AtlasTextureAsset, (Assets::AssetId{800u, 1u}));
    EXPECT_EQ(bake.GeneratedTextureSemantic,
              G::VisualizationGeneratedTextureSemantic::ScalarAttribute);
    EXPECT_EQ(bake.SourceAttributeDirtyStamp, 1234u);
    EXPECT_EQ(stats.AdapterInvocationCount, 1u);
    EXPECT_EQ(stats.PacketAppendCount, 2u);

    const G::VisualizationDiagnostics diagnostics =
        G::ValidateVisualizationPackets(batch.AsPacketBatch());
    EXPECT_EQ(diagnostics.InputPacketCount, 2u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 2u);
    EXPECT_EQ(diagnostics.TextureResidencyDeferredCount, 2u);
    EXPECT_FALSE(diagnostics.HasErrors);

    const G::VisualizationOverlaySummary summary =
        G::BuildVisualizationOverlaySummary(batch.AsPacketBatch());
    EXPECT_EQ(summary.UvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.RuntimeResolvedUvBakeAtlasDescriptorCount, 1u);
    EXPECT_EQ(summary.FragmentBakeTextureAssetDescriptorCount, 1u);
    EXPECT_EQ(summary.ScalarBakeTextureAssetDescriptorCount, 1u);
    EXPECT_EQ(summary.HtexBakeAtlasDescriptorCount, 0u);
}

TEST(VisualizationAdapters, HtexMetadataAdapterSchedulesRecreateHtexTask)
{
    R::StreamingExecutor executor{};
    const R::HtexMetadataAdapter adapter{&executor};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "curvature",
                       .OutputName = "curvature_htex_bake",
                       .EmitFragmentBake = true,
                       .SourceAttributeName = "curvature",
                       .FragmentBakeMapping =
                           G::VisualizationFragmentBakeMapping::RecreateHtex,
                       .MeshHasTexcoords = true,
                       .FaceCount = 24u,
                       .AtlasWidth = 512u,
                       .AtlasHeight = 512u,
                       .HtexRecreatePayloadToken = 42u,
                   },
                   stats);

    ASSERT_EQ(batch.FragmentBakeAtlases.size(), 1u);
    EXPECT_EQ(batch.FragmentBakeAtlases.front().Mapping,
              G::VisualizationFragmentBakeMapping::RecreateHtex);
    EXPECT_EQ(stats.PacketAppendCount, 1u);
    EXPECT_EQ(stats.HtexRecreateScheduledCount, 1u);
    const R::StreamingTaskHandle task = stats.LastHtexRecreateTask;
    EXPECT_TRUE(task.IsValid());
    EXPECT_EQ(executor.GetState(task), R::StreamingTaskState::Pending);

    executor.PumpBackground(1u);
    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(task), R::StreamingTaskState::Complete);

    const G::VisualizationDiagnostics diagnostics =
        G::ValidateVisualizationPackets(batch.AsPacketBatch());
    EXPECT_EQ(diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(diagnostics.AcceptedPacketCount, 1u);
    EXPECT_EQ(diagnostics.HtexRecreateRequestCount, 1u);
    EXPECT_EQ(diagnostics.TextureResidencyDeferredCount, 1u);
    EXPECT_FALSE(diagnostics.HasErrors);
}

TEST(VisualizationAdapters, HtexMetadataAdapterRejectsInvalidDescriptors)
{
    const R::HtexMetadataAdapter adapter{};

    R::VisualizationAdapterBatch batch{};
    R::VisualizationAdapterStats stats{};
    adapter.Append(batch, R::VisualizationAdapterOptions{}, stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "htex",
                       .EmitHtexPreview = true,
                       .PatchCount = 4u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "uv_bake",
                       .EmitFragmentBake = true,
                       .SourceAttributeName = "curvature",
                       .FragmentBakeMapping =
                           G::VisualizationFragmentBakeMapping::ExistingTexcoords,
                       .MeshHasTexcoords = false,
                       .FaceCount = 24u,
                       .AtlasWidth = 512u,
                       .AtlasHeight = 512u,
                   },
                   stats);
    adapter.Append(batch,
                   R::VisualizationAdapterOptions{
                       .SourceName = "htex_bake",
                       .EmitFragmentBake = true,
                       .SourceAttributeName = "curvature",
                       .FragmentBakeMapping =
                           G::VisualizationFragmentBakeMapping::RecreateHtex,
                       .FaceCount = 24u,
                       .AtlasWidth = 512u,
                       .AtlasHeight = 512u,
                   },
                   stats);

    EXPECT_TRUE(batch.HtexAtlases.empty());
    EXPECT_TRUE(batch.FragmentBakeAtlases.empty());
    EXPECT_EQ(stats.AdapterInvocationCount, 4u);
    EXPECT_EQ(stats.MissingSourceCount, 1u);
    EXPECT_EQ(stats.InvalidResourceCount, 2u);
    EXPECT_EQ(stats.MissingTexcoordCount, 1u);
    EXPECT_EQ(stats.HtexRecreateScheduledCount, 0u);
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
    batch.HtexAtlases.push_back(G::HtexPatchPreviewAtlasPacket{
        .Name = "htex",
        .PatchCount = 2u,
        .AtlasWidth = 8u,
        .AtlasHeight = 8u,
    });
    batch.FragmentBakeAtlases.push_back(G::FragmentBakeAtlasPacket{
        .Name = "bake",
        .SourceAttributeName = "scalar",
        .Mapping = G::VisualizationFragmentBakeMapping::ExistingHtex,
        .FaceCount = 2u,
        .AtlasWidth = 8u,
        .AtlasHeight = 8u,
    });

    const G::VisualizationPacketBatch packetView =
        batch.AsPacketBatch(true, G::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packetView.AttributeBuffers.size(), 1u);
    EXPECT_EQ(packetView.PropertyBuffers.size(), 0u);
    EXPECT_EQ(packetView.Scalars.size(), 1u);
    EXPECT_EQ(packetView.Colors.size(), 1u);
    EXPECT_EQ(packetView.VectorFields.size(), 1u);
    EXPECT_EQ(packetView.Isolines.size(), 1u);
    EXPECT_EQ(packetView.HtexAtlases.size(), 1u);
    EXPECT_EQ(packetView.FragmentBakeAtlases.size(), 1u);
    EXPECT_TRUE(packetView.EnforceDomain);

    batch.Clear();
    EXPECT_TRUE(batch.PropertyBuffers.empty());
    EXPECT_TRUE(batch.PropertyBufferPayloads.empty());
    EXPECT_TRUE(batch.AttributeBuffers.empty());
    EXPECT_TRUE(batch.Scalars.empty());
    EXPECT_TRUE(batch.Colors.empty());
    EXPECT_TRUE(batch.VectorFields.empty());
    EXPECT_TRUE(batch.Isolines.empty());
    EXPECT_TRUE(batch.HtexAtlases.empty());
    EXPECT_TRUE(batch.FragmentBakeAtlases.empty());
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
