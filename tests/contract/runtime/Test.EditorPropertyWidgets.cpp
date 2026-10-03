#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Runtime.EditorPropertyWidgets;
import Geometry.Properties;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
}

TEST(EditorPropertyWidgets, EmptySourceProducesAnEmptySafeModel)
{
    const Runtime::EditorScalarPropertyPlotModel model =
        Runtime::BuildEditorScalarPropertyPlotModel(
            Geometry::ConstPropertySet{});

    EXPECT_TRUE(model.Options.empty());
    EXPECT_TRUE(model.SelectedProperty.empty());
    EXPECT_EQ(model.Statistics.FiniteCount, 0u);
    EXPECT_EQ(model.SourceSampleCount, 0u);
    EXPECT_EQ(model.FilteredNonFiniteSampleCount, 0u);
    EXPECT_FALSE(model.HasFiniteRange);
}

TEST(EditorPropertyWidgets, SelectorExcludesVectorProperties)
{
    Geometry::PropertySet properties;
    properties.Resize(3u);
    [[maybe_unused]] Geometry::Property<glm::vec3> vectors =
        properties.Add<glm::vec3>("v:normal");
    [[maybe_unused]] Geometry::Property<std::uint32_t> labels =
        properties.Add<std::uint32_t>("v:label");

    const Runtime::EditorScalarPropertyPlotModel model =
        Runtime::BuildEditorScalarPropertyPlotModel(
            Geometry::ConstPropertySet{properties},
            "v:normal");

    ASSERT_EQ(model.Options.size(), 1u);
    EXPECT_EQ(model.Options.front().Name, "v:label");
    EXPECT_EQ(model.SelectedProperty, "v:label");
    EXPECT_EQ(model.Statistics.FiniteCount, 3u);
}

TEST(EditorPropertyWidgets, PlotModelFiltersNonFiniteSamplesAndReportsRange)
{
    Geometry::PropertySet properties;
    properties.Resize(5u);
    Geometry::Property<float> scalar = properties.Add<float>("v:quality");
    ASSERT_TRUE(scalar);
    scalar.Vector() = {
        1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        3.0f,
        std::numeric_limits<float>::infinity(),
        -2.0f,
    };

    const Runtime::EditorScalarPropertyPlotModel model =
        Runtime::BuildEditorScalarPropertyPlotModel(
            Geometry::ConstPropertySet{properties},
            "v:quality");

    EXPECT_EQ(model.SelectedProperty, "v:quality");
    EXPECT_EQ(model.SourceSampleCount, 5u);
    EXPECT_EQ(model.FilteredNonFiniteSampleCount, 2u);
    EXPECT_EQ(model.Statistics.FiniteCount, 3u);
    EXPECT_DOUBLE_EQ(model.Statistics.Mean, 2.0 / 3.0);
    ASSERT_TRUE(model.HasFiniteRange);
    EXPECT_DOUBLE_EQ(model.Minimum, -2.0);
    EXPECT_DOUBLE_EQ(model.Maximum, 3.0);
}

TEST(EditorPropertyWidgets, HistogramUsesSharedStatisticsAndExcludesDeletedRows)
{
    Geometry::PropertySet properties;
    properties.Resize(4);
    properties.Add<double>("scalar").Vector() = {0, 2, 4, 100};
    properties.Add<bool>("v:deleted")[3] = true;
    const auto model = Runtime::BuildEditorScalarPropertyPlotModel(
        Geometry::ConstPropertySet{properties}, "scalar", 2);
    EXPECT_EQ(model.SourceSampleCount, 3u);
    EXPECT_EQ(model.Statistics.FiniteCount, 3u);
    EXPECT_DOUBLE_EQ(model.Maximum, 4);
    EXPECT_EQ(model.Statistics.Histogram.Counts, (std::vector<std::size_t>{1, 2}));
}
