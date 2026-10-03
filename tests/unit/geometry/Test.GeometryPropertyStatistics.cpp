#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

import Geometry.Properties.Statistics;

namespace
{
    using Status = Geometry::PropertyStatisticsStatus;
    using Kind = Geometry::PropertyValueKind;

    template <class T>
    Geometry::PropertySet MakeProperty(const std::vector<T>& values, std::string name = "samples")
    {
        Geometry::PropertySet properties;
        properties.Resize(values.size());
        auto property = properties.Add<T>(std::move(name));
        property.Vector() = values;
        return properties;
    }

    Geometry::PropertyStatisticsResult Statistics(const Geometry::PropertySet& properties,
                                                  std::size_t bins = 3)
    {
        return Geometry::ComputePropertyStatistics(Geometry::ConstPropertySet(properties), "samples", {.Bins = bins});
    }

    Geometry::PropertyComparisonResult Compare(const Geometry::PropertySet& a, const Geometry::PropertySet& b)
    {
        return Geometry::ComparePropertyValues(Geometry::ConstPropertySet(a), "samples",
                                               Geometry::ConstPropertySet(b), "samples");
    }
}

TEST(GeometryPropertyStatistics, ScalarMomentsAndHalfOpenHistogram)
{
    const auto properties = MakeProperty<double>({0, 1, 2, 3});
    const auto result = Statistics(properties);
    ASSERT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.Kind, Kind::Double);
    EXPECT_EQ(result.RowCount, 4u);
    EXPECT_EQ(result.Count, 4u);
    EXPECT_EQ(result.FiniteCount, 4u);
    EXPECT_EQ(result.ZeroCount, 1u);
    EXPECT_FALSE(result.Magnitude.has_value());
    ASSERT_EQ(result.Components.size(), 1u);
    const auto& scalar = result.Components[0];
    EXPECT_DOUBLE_EQ(scalar.Min, 0);
    EXPECT_DOUBLE_EQ(scalar.Max, 3);
    EXPECT_DOUBLE_EQ(scalar.Mean, 1.5);
    EXPECT_DOUBLE_EQ(scalar.RMS, std::sqrt(3.5));
    EXPECT_DOUBLE_EQ(scalar.StdDev, std::sqrt(1.25));
    EXPECT_EQ(scalar.Histogram.Edges, (std::vector<double>{0, 1, 2, 3}));
    EXPECT_EQ(scalar.Histogram.Counts, (std::vector<std::size_t>{1, 1, 2}));
}

TEST(GeometryPropertyStatistics, SupportsEveryScalarKindWithoutNameRestrictions)
{
    const auto check = []<class T>(Kind kind)
    {
        const auto properties = MakeProperty<T>({T{0}, T{1}}, "face measurement");
        const auto result = Geometry::ComputePropertyStatistics(
            Geometry::ConstPropertySet(properties), "face measurement", {.Bins = 0});
        ASSERT_EQ(result.Status, Status::Ok);
        EXPECT_EQ(result.Kind, kind);
        ASSERT_EQ(result.Components.size(), 1u);
        EXPECT_DOUBLE_EQ(result.Components[0].Mean, 0.5);
        EXPECT_DOUBLE_EQ(result.Components[0].StdDev, 0.5);
        EXPECT_TRUE(result.Components[0].Histogram.Counts.empty());
    };
    check.operator()<bool>(Kind::Bool);
    check.operator()<std::int32_t>(Kind::Int32);
    check.operator()<std::uint32_t>(Kind::UInt32);
    check.operator()<std::uint64_t>(Kind::UInt64);
    check.operator()<float>(Kind::Float);
    check.operator()<double>(Kind::Double);
}

TEST(GeometryPropertyStatistics, AccountsForNaNInfinityAndSignedZero)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto properties = MakeProperty<double>({nan, inf, -inf, -0.0, 2});
    const auto result = Statistics(properties);
    ASSERT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.Count, 5u);
    EXPECT_EQ(result.FiniteCount, 2u);
    EXPECT_EQ(result.NaNCount, 1u);
    EXPECT_EQ(result.InfCount, 2u);
    EXPECT_EQ(result.ZeroCount, 1u);
    const auto& scalar = result.Components[0];
    EXPECT_EQ(scalar.Count, 5u);
    EXPECT_EQ(scalar.FiniteCount, 2u);
    EXPECT_EQ(scalar.NaNCount, 1u);
    EXPECT_EQ(scalar.InfCount, 2u);
    EXPECT_DOUBLE_EQ(scalar.Mean, 1);
    EXPECT_EQ(std::accumulate(scalar.Histogram.Counts.begin(), scalar.Histogram.Counts.end(), 0u), 2u);
}

TEST(GeometryPropertyStatistics, VectorComponentsAndMagnitudesHaveIndependentFiniteCounts)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto properties = MakeProperty<glm::vec3>({{3, 4, 0}, {0, 0, 0}, {nan, inf, 1}, {2, 0, inf}});
    const auto result = Statistics(properties, 2);
    ASSERT_EQ(result.Status, Status::Ok);
    ASSERT_EQ(result.Components.size(), 3u);
    ASSERT_TRUE(result.Magnitude.has_value());
    EXPECT_EQ(result.FiniteCount, 2u);
    EXPECT_EQ(result.NaNCount, 1u);
    EXPECT_EQ(result.InfCount, 1u);
    EXPECT_EQ(result.ZeroCount, 1u);
    EXPECT_EQ(result.Components[0].FiniteCount, 3u);
    EXPECT_EQ(result.Components[0].NaNCount, 1u);
    EXPECT_EQ(result.Components[1].InfCount, 1u);
    EXPECT_EQ(result.Components[2].ZeroCount, 2u);
    const auto& magnitude = *result.Magnitude;
    EXPECT_EQ(magnitude.FiniteCount, 2u);
    EXPECT_EQ(magnitude.NaNCount, 1u);
    EXPECT_EQ(magnitude.InfCount, 1u);
    EXPECT_DOUBLE_EQ(magnitude.Min, 0);
    EXPECT_DOUBLE_EQ(magnitude.Max, 5);
    EXPECT_DOUBLE_EQ(magnitude.Mean, 2.5);
    EXPECT_DOUBLE_EQ(magnitude.StdDev, 2.5);
    EXPECT_DOUBLE_EQ(magnitude.RMS, std::sqrt(12.5));
    EXPECT_EQ(magnitude.Histogram.Counts, (std::vector<std::size_t>{1, 1}));
}

TEST(GeometryPropertyStatistics, SupportsVec2AndVec4IncludingLargeFiniteMagnitude)
{
    const auto vec2 = MakeProperty<glm::vec2>({{3, 4}});
    const auto result2 = Statistics(vec2);
    ASSERT_EQ(result2.Components.size(), 2u);
    ASSERT_TRUE(result2.Magnitude.has_value());
    EXPECT_DOUBLE_EQ(result2.Magnitude->Mean, 5);

    const auto vec4 = MakeProperty<glm::vec4>({glm::vec4(std::numeric_limits<float>::max())});
    const auto result4 = Statistics(vec4);
    ASSERT_EQ(result4.Status, Status::Ok);
    ASSERT_EQ(result4.Components.size(), 4u);
    ASSERT_TRUE(result4.Magnitude.has_value());
    EXPECT_DOUBLE_EQ(result4.Magnitude->Mean, 2.0 * std::numeric_limits<float>::max());
}

TEST(GeometryPropertyStatistics, UsesUnionOfDomainDeletionMarkers)
{
    auto properties = MakeProperty<float>({1, 2, 100, 200});
    auto vertexDeleted = properties.Add<bool>("v:deleted");
    auto pointDeleted = properties.Add<bool>("p:deleted");
    vertexDeleted[2] = true;
    pointDeleted[3] = true;
    const auto result = Statistics(properties);
    ASSERT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.RowCount, 4u);
    EXPECT_EQ(result.DeletedCount, 2u);
    EXPECT_EQ(result.Count, 2u);
    EXPECT_DOUBLE_EQ(result.Components[0].Mean, 1.5);
    const auto masks = Geometry::ResolvePropertyDeletionMasks(Geometry::ConstPropertySet(properties));
    EXPECT_EQ(masks.Status, Status::Ok);
    EXPECT_FALSE(masks.IsDeleted(0));
    EXPECT_TRUE(masks.IsDeleted(2));
    EXPECT_TRUE(masks.IsDeleted(3));
    EXPECT_FALSE(masks.IsDeleted(100));
    const auto includeDeleted = Geometry::ComputePropertyStatistics(
        Geometry::ConstPropertySet(properties), "samples", {.HonorDeleted = false});
    EXPECT_EQ(includeDeleted.Count, 4u);
    EXPECT_EQ(includeDeleted.DeletedCount, 0u);
}

TEST(GeometryPropertyStatistics, RejectsMalformedDeletionMarkersUnlessExplicitlyIgnored)
{
    auto properties = MakeProperty<float>({1});
    auto wrongType = properties.Add<std::int32_t>("f:deleted");
    EXPECT_EQ(Statistics(properties).Status, Status::InvalidDeletionMask);
    EXPECT_FALSE(Statistics(properties).Diagnostic.empty());
    EXPECT_EQ(Geometry::ComputePropertyStatistics(Geometry::ConstPropertySet(properties), "samples",
                                                 {.HonorDeleted = false}).Status, Status::Ok);
    properties.Remove(wrongType);
    auto wrongSize = properties.Add<bool>("f:deleted");
    wrongSize.Vector().clear();
    const auto masks = Geometry::ResolvePropertyDeletionMasks(Geometry::ConstPropertySet(properties));
    EXPECT_EQ(masks.Status, Status::InvalidDeletionMask);
    EXPECT_TRUE(masks.Masks.empty());
}

TEST(GeometryPropertyStatistics, ConstantHistogramIsBoundedAndDeterministic)
{
    const auto properties = MakeProperty<std::int32_t>({7, 7, 7});
    const auto result = Statistics(properties, 3);
    ASSERT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.Components[0].Histogram.Edges, (std::vector<double>{7, 7, 7, 7}));
    EXPECT_EQ(result.Components[0].Histogram.Counts, (std::vector<std::size_t>{3, 0, 0}));
    EXPECT_EQ(Statistics(properties, 1).Components[0].Histogram.Counts, (std::vector<std::size_t>{3}));
    EXPECT_EQ(Statistics(properties, Geometry::MaxPropertyHistogramBins).Components[0].Histogram.Counts.size(),
              Geometry::MaxPropertyHistogramBins);
    EXPECT_EQ(Statistics(properties, Geometry::MaxPropertyHistogramBins + 1).Status, Status::InvalidParameters);
    EXPECT_EQ(Statistics(properties, 0).Components[0].Histogram.Edges.size(), 0u);
}

TEST(GeometryPropertyStatistics, EmptyAllDeletedAndAllNonFiniteHaveDefinedResults)
{
    const auto empty = MakeProperty<double>({});
    const auto emptyResult = Statistics(empty);
    ASSERT_EQ(emptyResult.Status, Status::Empty);
    ASSERT_EQ(emptyResult.Components.size(), 1u);
    EXPECT_DOUBLE_EQ(emptyResult.Components[0].Mean, 0);
    EXPECT_TRUE(emptyResult.Components[0].Histogram.Edges.empty());

    auto deleted = MakeProperty<double>({10});
    auto marker = deleted.Add<bool>("custom:deleted", true);
    EXPECT_EQ(Statistics(deleted).Status, Status::Empty);
    EXPECT_EQ(Statistics(deleted).DeletedCount, 1u);

    const auto nonFinite = MakeProperty<double>({std::numeric_limits<double>::quiet_NaN(),
                                               std::numeric_limits<double>::infinity()});
    const auto result = Statistics(nonFinite);
    EXPECT_EQ(result.Status, Status::NoFiniteValues);
    EXPECT_EQ(result.Components[0].FiniteCount, 0u);
    EXPECT_TRUE(result.Components[0].Histogram.Counts.empty());
    EXPECT_DOUBLE_EQ(result.Components[0].StdDev, 0);
}

TEST(GeometryPropertyStatistics, NonFiniteVectorRowsRetainUsefulFiniteComponents)
{
    const auto properties = MakeProperty<glm::vec2>({{1, std::numeric_limits<float>::quiet_NaN()},
                                                   {3, std::numeric_limits<float>::infinity()}});
    const auto result = Statistics(properties);
    EXPECT_EQ(result.Status, Status::NoFiniteValues);
    EXPECT_EQ(result.FiniteCount, 0u);
    EXPECT_DOUBLE_EQ(result.Components[0].Mean, 2);
    EXPECT_EQ(result.Components[0].FiniteCount, 2u);
}

TEST(GeometryPropertyStatistics, MissingUnsupportedAndCorruptCardinalityAreExplicit)
{
    const auto unsupported = MakeProperty<std::string>({"word"});
    EXPECT_EQ(Statistics(unsupported).Status, Status::UnsupportedKind);
    EXPECT_EQ(Geometry::ComputePropertyStatistics(Geometry::ConstPropertySet(unsupported), "absent").Status,
              Status::PropertyNotFound);
    EXPECT_EQ(Geometry::ComputePropertyStatistics({}, "samples").Status, Status::PropertyNotFound);
    auto corrupt = MakeProperty<double>({1, 2});
    corrupt.Get<double>("samples").Vector().pop_back();
    EXPECT_EQ(Statistics(corrupt).Status, Status::RowCountMismatch);
}

TEST(GeometryPropertyStatistics, CenteredVarianceAndScaledRMSRemainStable)
{
    const auto largeOffset = MakeProperty<double>({1e12 + 1, 1e12 + 2, 1e12 + 3});
    const auto result = Statistics(largeOffset);
    EXPECT_DOUBLE_EQ(result.Components[0].Mean, 1e12 + 2);
    EXPECT_NEAR(result.Components[0].StdDev, std::sqrt(2.0 / 3.0), 1e-14);

    const double maximum = std::numeric_limits<double>::max();
    const auto extremes = Statistics(MakeProperty<double>({-maximum, maximum}), 2);
    const auto cancellation = Statistics(MakeProperty<double>({maximum, 1, -maximum}));
    if constexpr (std::numeric_limits<long double>::max_exponent <=
                  2 * std::numeric_limits<double>::max_exponent)
    {
        EXPECT_EQ(extremes.Status, Status::NumericOverflow);
        EXPECT_EQ(extremes.FiniteCount, 2u);
        EXPECT_FALSE(extremes.Diagnostic.empty());
        EXPECT_TRUE(extremes.Components[0].Histogram.Counts.empty());
        EXPECT_EQ(cancellation.Status, Status::NumericOverflow);
        return;
    }
    ASSERT_EQ(extremes.Status, Status::Ok);
    EXPECT_DOUBLE_EQ(extremes.Components[0].Mean, 0);
    EXPECT_DOUBLE_EQ(extremes.Components[0].RMS, maximum);
    EXPECT_DOUBLE_EQ(extremes.Components[0].StdDev, maximum);
    EXPECT_EQ(extremes.Components[0].Histogram.Edges, (std::vector<double>{-maximum, 0, maximum}));
    EXPECT_EQ(extremes.Components[0].Histogram.Counts, (std::vector<std::size_t>{1, 1}));
    ASSERT_EQ(cancellation.Status, Status::Ok);
    EXPECT_DOUBLE_EQ(cancellation.Components[0].Mean, 1.0 / 3.0);
}

TEST(GeometryPropertyStatistics, UInt64StatisticsRetainSmallSpreadBeforeOutputRounding)
{
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto result = Statistics(MakeProperty<std::uint64_t>({maximum - 1, maximum}));
    ASSERT_EQ(result.Status, Status::Ok);
    EXPECT_DOUBLE_EQ(result.Components[0].StdDev, 0.5);
    EXPECT_DOUBLE_EQ(result.Components[0].Max, static_cast<double>(maximum));
    EXPECT_EQ(result.Components[0].Histogram.Counts, (std::vector<std::size_t>{2, 0, 0}));
}

TEST(GeometryPropertyStatistics, ComparisonReportsIdenticalOffsetAndFirstMaximumRows)
{
    const auto a = MakeProperty<double>({1, 2, 3});
    const auto identical = Compare(a, a);
    EXPECT_EQ(identical.Status, Status::Ok);
    EXPECT_EQ(identical.ComparableRows, 3u);
    EXPECT_EQ(identical.IdenticalRows, 3u);
    EXPECT_EQ(identical.MaxErrorRow, 0u);
    EXPECT_DOUBLE_EQ(identical.RMSError, 0);

    const auto b = MakeProperty<double>({1, 4, 5});
    const auto result = Compare(a, b);
    EXPECT_EQ(result.IdenticalRows, 1u);
    EXPECT_EQ(result.MaxErrorRow, 1u);
    EXPECT_DOUBLE_EQ(result.MaxAbsError, 2);
    EXPECT_DOUBLE_EQ(result.MeanAbsError, 4.0 / 3.0);
    EXPECT_DOUBLE_EQ(result.RMSError, std::sqrt(8.0 / 3.0));
}

TEST(GeometryPropertyStatistics, ComparisonSupportsMixedScalarKindsAndSignedValues)
{
    const auto ints = MakeProperty<std::int32_t>({-2, 0, 7});
    const auto reals = MakeProperty<double>({-2, 0.5, 7});
    const auto result = Compare(ints, reals);
    EXPECT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.KindA, Kind::Int32);
    EXPECT_EQ(result.KindB, Kind::Double);
    EXPECT_EQ(result.IdenticalRows, 2u);
    EXPECT_DOUBLE_EQ(result.MaxAbsError, 0.5);
    EXPECT_DOUBLE_EQ(result.MeanAbsError, 0.5 / 3);
    EXPECT_DOUBLE_EQ(Compare(reals, ints).MaxAbsError, 0.5);
    const auto signedUnsigned = Compare(MakeProperty<std::int32_t>({-2}), MakeProperty<std::uint32_t>({2}));
    EXPECT_DOUBLE_EQ(signedUnsigned.MaxAbsError, 4);
    EXPECT_EQ(signedUnsigned.IdenticalRows, 0u);
    EXPECT_EQ(Compare(MakeProperty<bool>({false, true}), MakeProperty<float>({-0.0f, 1})).IdenticalRows, 2u);
}

TEST(GeometryPropertyStatistics, ComparisonDoesNotRoundAwayUInt64Differences)
{
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto a = MakeProperty<std::uint64_t>({maximum, maximum - 1, (1ull << 53) + 1});
    const auto b = MakeProperty<std::uint64_t>({maximum - 1, maximum, (1ull << 53)});
    const auto result = Compare(a, b);
    ASSERT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.IdenticalRows, 0u);
    EXPECT_DOUBLE_EQ(result.MaxAbsError, 1);
    EXPECT_DOUBLE_EQ(result.MeanAbsError, 1);
    EXPECT_DOUBLE_EQ(result.RMSError, 1);
    EXPECT_EQ(Compare(a, a).IdenticalRows, 3u);

    const auto realBoundary = MakeProperty<double>({0x1p64});
    const auto integerBoundary = MakeProperty<std::uint64_t>({maximum});
    EXPECT_DOUBLE_EQ(Compare(integerBoundary, realBoundary).MaxAbsError, 1);
    EXPECT_DOUBLE_EQ(Compare(realBoundary, integerBoundary).MaxAbsError, 1);
    EXPECT_EQ(Compare(integerBoundary, realBoundary).IdenticalRows, 0u);
    EXPECT_DOUBLE_EQ(Compare(MakeProperty<std::uint64_t>({(1ull << 53) + 1}),
                            MakeProperty<double>({0x1p53})).MaxAbsError, 1);
}

TEST(GeometryPropertyStatistics, VectorComparisonUsesRowEuclideanDistance)
{
    const auto a = MakeProperty<glm::vec3>({{0, 0, 0}, {1, 2, 3}});
    const auto b = MakeProperty<glm::vec3>({{3, 4, 0}, {1, 2, 3}});
    const auto result = Compare(a, b);
    EXPECT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.IdenticalRows, 1u);
    EXPECT_DOUBLE_EQ(result.MaxAbsError, 5);
    EXPECT_DOUBLE_EQ(result.MeanAbsError, 2.5);
    EXPECT_DOUBLE_EQ(result.RMSError, std::sqrt(12.5));
}

TEST(GeometryPropertyStatistics, ComparisonExcludesDeletedUnionAndNonFiniteRows)
{
    auto a = MakeProperty<double>({1, 2, 3, std::numeric_limits<double>::infinity(), 5});
    auto b = MakeProperty<double>({100, 200, 4, std::numeric_limits<double>::infinity(), 6});
    auto deletedA = a.Add<bool>("e:deleted");
    auto deletedB = b.Add<bool>("e:deleted");
    deletedA[0] = true;
    deletedB[1] = true;
    const auto result = Compare(a, b);
    EXPECT_EQ(result.Status, Status::Ok);
    EXPECT_EQ(result.RowCount, 5u);
    EXPECT_EQ(result.DeletedCount, 2u);
    EXPECT_EQ(result.NonFiniteRows, 1u);
    EXPECT_EQ(result.ComparableRows, 2u);
    EXPECT_EQ(result.MaxErrorRow, 2u);
    EXPECT_DOUBLE_EQ(result.MeanAbsError, 1);
    const auto unfiltered = Geometry::ComparePropertyValues(Geometry::ConstPropertySet(a), "samples",
        Geometry::ConstPropertySet(b), "samples", false);
    EXPECT_EQ(unfiltered.DeletedCount, 0u);
    EXPECT_EQ(unfiltered.ComparableRows, 4u);
    EXPECT_DOUBLE_EQ(unfiltered.MaxAbsError, 198);
}

TEST(GeometryPropertyStatistics, ComparisonRejectsShapeCardinalityAndUnsupportedKinds)
{
    const auto one = MakeProperty<double>({1});
    EXPECT_EQ(Compare(one, MakeProperty<double>({1, 2})).Status, Status::RowCountMismatch);
    EXPECT_EQ(Compare(one, MakeProperty<glm::vec2>({{1, 1}})).Status, Status::KindMismatch);
    EXPECT_EQ(Compare(MakeProperty<glm::vec2>({{1, 1}}), MakeProperty<glm::vec3>({{1, 1, 1}})).Status,
              Status::KindMismatch);
    EXPECT_EQ(Compare(one, MakeProperty<std::string>({"1"})).Status, Status::UnsupportedKind);
    EXPECT_EQ(Geometry::ComparePropertyValues(Geometry::ConstPropertySet(one), "absent",
                                             Geometry::ConstPropertySet(one), "samples").Status,
              Status::PropertyNotFound);
    auto invalidMask = MakeProperty<double>({1});
    auto mask = invalidMask.Add<float>("v:deleted");
    EXPECT_EQ(Compare(one, invalidMask).Status, Status::InvalidDeletionMask);
    auto corrupt = MakeProperty<double>({1});
    corrupt.Get<double>("samples").Vector().clear();
    EXPECT_EQ(Compare(one, corrupt).Status, Status::RowCountMismatch);
}

TEST(GeometryPropertyStatistics, ComparisonEmptyNonFiniteAndOverflowHaveExplicitStatuses)
{
    const auto empty = MakeProperty<float>({});
    EXPECT_EQ(Compare(empty, empty).Status, Status::Empty);
    EXPECT_FALSE(Compare(empty, empty).MaxErrorRow.has_value());
    const auto nan = MakeProperty<float>({std::numeric_limits<float>::quiet_NaN()});
    const auto nonFinite = Compare(nan, nan);
    EXPECT_EQ(nonFinite.Status, Status::NoFiniteValues);
    EXPECT_EQ(nonFinite.NonFiniteRows, 1u);
    EXPECT_EQ(nonFinite.IdenticalRows, 0u);
    const double maximum = std::numeric_limits<double>::max();
    const auto overflow = Compare(MakeProperty<double>({maximum}), MakeProperty<double>({-maximum}));
    EXPECT_EQ(overflow.Status, Status::NumericOverflow);
    EXPECT_EQ(overflow.ComparableRows, 1u);
    EXPECT_EQ(overflow.MaxErrorRow, 0u);
    EXPECT_DOUBLE_EQ(overflow.MaxAbsError, 0);
    EXPECT_FALSE(overflow.Diagnostic.empty());
}
