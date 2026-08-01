#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.KDTree;
import Geometry.PointCloud.Kernels;

namespace
{
    namespace Kernels = Geometry::PointCloud::Kernels;

    [[nodiscard]] std::vector<glm::vec3> UniformGrid()
    {
        std::vector<glm::vec3> points{};
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                points.emplace_back(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    0.0f);
            }
        }
        return points;
    }
}

TEST(PointCloudKernels, RadialWeightsMatchDocumentedClosedForms)
{
    constexpr double h = 4.0;
    constexpr double quarterSquared = 1.0;

    ASSERT_TRUE(Kernels::Weight(
        0.0, h, Kernels::KernelType::Gaussian));
    EXPECT_DOUBLE_EQ(
        *Kernels::Weight(0.0, h, Kernels::KernelType::Gaussian),
        1.0);
    EXPECT_NEAR(
        *Kernels::Weight(
            quarterSquared, h, Kernels::KernelType::Gaussian),
        std::exp(-0.5),
        1.0e-15);
    EXPECT_NEAR(
        *Kernels::Weight(
            quarterSquared, h, Kernels::KernelType::ThetaLop),
        std::exp(-1.0),
        1.0e-15);
    EXPECT_NEAR(
        *Kernels::Weight(
            quarterSquared, h, Kernels::KernelType::WendlandC2),
        std::pow(0.75, 4.0) * 2.0,
        1.0e-15);

    for (const Kernels::KernelType kernel : {
             Kernels::KernelType::Gaussian,
             Kernels::KernelType::ThetaLop,
             Kernels::KernelType::WendlandC2})
    {
        EXPECT_DOUBLE_EQ(*Kernels::Weight(h * h, h, kernel), 0.0);
        EXPECT_DOUBLE_EQ(
            *Kernels::Weight(1.5 * 1.5 * h * h, h, kernel),
            0.0);

        double previous = 1.0;
        for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0})
        {
            const auto value = Kernels::Weight(
                fraction * fraction * h * h,
                h,
                kernel);
            ASSERT_TRUE(value.has_value());
            EXPECT_LE(*value, previous);
            EXPECT_GE(*value, 0.0);
            previous = *value;
        }
    }
}

TEST(PointCloudKernels, RadialWeightsFailClosed)
{
    EXPECT_FALSE(Kernels::Weight(
        -1.0, 1.0, Kernels::KernelType::ThetaLop));
    EXPECT_FALSE(Kernels::Weight(
        1.0, 0.0, Kernels::KernelType::ThetaLop));
    EXPECT_FALSE(Kernels::Weight(
        std::numeric_limits<double>::infinity(),
        1.0,
        Kernels::KernelType::ThetaLop));
    EXPECT_FALSE(Kernels::Weight(
        1.0,
        1.0,
        static_cast<Kernels::KernelType>(255)));
}

TEST(PointCloudKernels, RadialWeightsPreserveZeroDistanceAtExtremeSupportScales)
{
    for (const double supportRadius : {1.0e-200, 1.0e200})
    {
        for (const Kernels::KernelType kernel : {
                 Kernels::KernelType::Gaussian,
                 Kernels::KernelType::ThetaLop,
                 Kernels::KernelType::WendlandC2})
        {
            const auto value = Kernels::Weight(
                0.0, supportRadius, kernel);
            ASSERT_TRUE(value.has_value());
            EXPECT_DOUBLE_EQ(*value, 1.0);
        }
    }
}

TEST(PointCloudKernels, LinearRepulsionHasFiniteZeroLimit)
{
    const auto atZero = Kernels::Repulsion(0.0, 2.0);
    const auto derivativeAtZero =
        Kernels::RepulsionDerivative(0.0, 2.0);
    ASSERT_TRUE(atZero.has_value());
    ASSERT_TRUE(derivativeAtZero.has_value());
    EXPECT_DOUBLE_EQ(*atZero, 0.0);
    EXPECT_DOUBLE_EQ(*derivativeAtZero, -0.5);
    EXPECT_DOUBLE_EQ(*Kernels::Repulsion(1.0, 2.0), -0.5);
    EXPECT_DOUBLE_EQ(
        *Kernels::RepulsionDerivative(1.0, 2.0),
        -0.5);
    EXPECT_TRUE(std::isfinite(
        *Kernels::Repulsion(
            std::numeric_limits<double>::denorm_min(),
            2.0)));

    EXPECT_FALSE(Kernels::Repulsion(-1.0, 2.0));
    EXPECT_FALSE(Kernels::RepulsionDerivative(1.0, 0.0));
}

TEST(PointCloudKernels, DensityDistinguishesDenseClusterFromSparseTail)
{
    const std::vector<glm::vec3> points{
        {0.0f, 0.0f, 0.0f},
        {0.05f, 0.0f, 0.0f},
        {0.10f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {2.8f, 0.0f, 0.0f},
    };
    const auto direct = Kernels::ComputeDensityWeights(
        points,
        0.9,
        Kernels::KernelType::ThetaLop,
        Kernels::DensityWeightMode::Direct);
    ASSERT_TRUE(direct.Succeeded());
    ASSERT_EQ(direct.Weights.size(), points.size());
    EXPECT_GT(direct.Weights[0], direct.Weights[3]);
    EXPECT_GT(direct.Weights[1], direct.Weights[4]);
    EXPECT_EQ(direct.Diagnostics.QueryCount, points.size());
    EXPECT_EQ(direct.Diagnostics.EmptyNeighborhoodCount, 0u);

    const auto reciprocal = Kernels::ComputeDensityWeights(
        points,
        0.9,
        Kernels::KernelType::ThetaLop,
        Kernels::DensityWeightMode::Reciprocal);
    ASSERT_TRUE(reciprocal.Succeeded());
    for (std::size_t i = 0u; i < points.size(); ++i)
    {
        EXPECT_NEAR(
            reciprocal.Weights[i],
            1.0f / direct.Weights[i],
            2.0e-7f);
    }
}

TEST(PointCloudKernels, UniformGridInteriorHasConstantDensity)
{
    const std::vector<glm::vec3> points = UniformGrid();
    const auto result = Kernels::ComputeDensityWeights(
        points,
        1.5,
        Kernels::KernelType::WendlandC2);
    ASSERT_TRUE(result.Succeeded());

    const float reference = result.Weights[6u];
    for (int y = 1; y <= 3; ++y)
    {
        for (int x = 1; x <= 3; ++x)
        {
            EXPECT_FLOAT_EQ(
                result.Weights[static_cast<std::size_t>(y * 5 + x)],
                reference);
        }
    }
}

TEST(PointCloudKernels, SuppliedIndexAndRepeatedRunsAreBitwiseDeterministic)
{
    const std::vector<glm::vec3> points = UniformGrid();
    Geometry::KDTree index{};
    ASSERT_TRUE(index.BuildFromPoints(points).has_value());

    const auto first = Kernels::ComputeDensityWeights(
        points,
        1.5,
        Kernels::KernelType::Gaussian);
    const auto second = Kernels::ComputeDensityWeights(
        points,
        1.5,
        Kernels::KernelType::Gaussian);
    const auto supplied = Kernels::ComputeDensityWeights(
        points,
        index,
        1.5,
        Kernels::KernelType::Gaussian);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    ASSERT_TRUE(supplied.Succeeded());
    EXPECT_EQ(first.Weights, second.Weights);
    EXPECT_EQ(first.Weights, supplied.Weights);
    EXPECT_FALSE(first.Diagnostics.UsedSuppliedIndex);
    EXPECT_TRUE(supplied.Diagnostics.UsedSuppliedIndex);
}

TEST(PointCloudKernels, DensityBroadPhaseIncludesDoubleSupportBoundary)
{
    constexpr float x = 7.776026636602611e-19f;
    constexpr float y = 9.225870040194345e-19f;
    const double distance = std::hypot(
        static_cast<double>(x), static_cast<double>(y));
    const double supportRadius = std::nextafter(
        distance, std::numeric_limits<double>::infinity());
    ASSERT_LT(distance, supportRadius);
    ASSERT_LT(
        static_cast<double>(static_cast<float>(supportRadius)),
        supportRadius);

    const std::vector<glm::vec3> points{
        {0.0f, 0.0f, 0.0f},
        {x, y, 0.0f},
    };
    const auto result = Kernels::ComputeDensityWeights(
        points,
        supportRadius,
        Kernels::KernelType::ThetaLop);
    ASSERT_TRUE(result.Succeeded())
        << Kernels::DebugName(result.Status);
    ASSERT_EQ(result.Weights.size(), points.size());
    EXPECT_GT(result.Weights[0], 1.0f);
    EXPECT_EQ(result.Diagnostics.EmptyNeighborhoodCount, 0u);
}

TEST(PointCloudKernels, DensityFailuresPublishNoWeights)
{
    const std::vector<glm::vec3> connected{
        {0.0f, 0.0f, 0.0f},
        {0.5f, 0.0f, 0.0f},
    };
    EXPECT_EQ(
        Kernels::ComputeDensityWeights({}, 1.0).Status,
        Kernels::DensityWeightStatus::EmptyInput);
    EXPECT_EQ(
        Kernels::ComputeDensityWeights(connected, 0.0).Status,
        Kernels::DensityWeightStatus::InvalidSupportRadius);
    EXPECT_EQ(
        Kernels::ComputeDensityWeights(
            connected,
            1.0,
            static_cast<Kernels::KernelType>(255)).Status,
        Kernels::DensityWeightStatus::InvalidKernel);
    EXPECT_EQ(
        Kernels::ComputeDensityWeights(
            connected,
            1.0,
            Kernels::KernelType::ThetaLop,
            static_cast<Kernels::DensityWeightMode>(255)).Status,
        Kernels::DensityWeightStatus::InvalidMode);

    std::vector<glm::vec3> nonFinite = connected;
    nonFinite[1].x = std::numeric_limits<float>::quiet_NaN();
    const auto nonFiniteResult =
        Kernels::ComputeDensityWeights(nonFinite, 1.0);
    EXPECT_EQ(
        nonFiniteResult.Status,
        Kernels::DensityWeightStatus::NonFiniteInput);
    EXPECT_TRUE(nonFiniteResult.Weights.empty());

    const std::vector<glm::vec3> isolated{
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
    };
    const auto isolatedResult =
        Kernels::ComputeDensityWeights(isolated, 1.0);
    EXPECT_EQ(
        isolatedResult.Status,
        Kernels::DensityWeightStatus::EmptyNeighborhood);
    EXPECT_TRUE(isolatedResult.Weights.empty());
    EXPECT_EQ(
        isolatedResult.Diagnostics.EmptyNeighborhoodCount,
        1u);

    Geometry::KDTree wrongIndex{};
    ASSERT_TRUE(wrongIndex.BuildFromPoints(connected).has_value());
    std::vector<glm::vec3> reordered = connected;
    std::swap(reordered[0], reordered[1]);
    const auto mismatch = Kernels::ComputeDensityWeights(
        reordered, wrongIndex, 1.0);
    EXPECT_EQ(
        mismatch.Status,
        Kernels::DensityWeightStatus::SpatialIndexMismatch);
    EXPECT_TRUE(mismatch.Weights.empty());
}
