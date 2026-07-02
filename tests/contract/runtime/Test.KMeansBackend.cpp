#include <gtest/gtest.h>

#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Runtime.KMeansBackend;
import Geometry.KMeans;

#include "MockRHI.hpp"

namespace
{
    namespace GK = Geometry::KMeans;
    namespace Runtime = Extrinsic::Runtime;
    namespace Tests = Extrinsic::Tests;

    [[nodiscard]] std::vector<glm::vec3> MakeSeparatedPoints()
    {
        return {
            {-1.0f, 0.0f, 0.0f},
            {-0.8f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {1.2f, 0.0f, 0.0f},
        };
    }

    [[nodiscard]] GK::KMeansParams MakeGpuRequest()
    {
        GK::KMeansParams params{};
        params.ClusterCount = 2u;
        params.MaxIterations = 8u;
        params.Compute = GK::Backend::GPU;
        return params;
    }
}

TEST(RuntimeKMeansBackend, NonOperationalGpuRequestFallsBackToCpu)
{
    const std::vector<glm::vec3> points = MakeSeparatedPoints();
    GK::KMeansParams params = MakeGpuRequest();

    Tests::MockDevice device;
    device.Operational = false;

    const auto result = Runtime::ClusterKMeans(points, params, device);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->RequestedBackend, GK::Backend::GPU);
    EXPECT_EQ(result->ActualBackend, GK::Backend::CPU);
    EXPECT_TRUE(result->FellBackToCPU);
    EXPECT_EQ(result->Labels.size(), points.size());
    EXPECT_EQ(result->Centroids.size(), 2u);
    EXPECT_EQ(device.CreateBufferCount, 0);
    EXPECT_EQ(device.CreatePipelineCount, 0);
}

TEST(RuntimeKMeansBackend, OperationalThinGpuRequestFallsBackWithoutExecutionDependencies)
{
    const std::vector<glm::vec3> points = MakeSeparatedPoints();
    GK::KMeansParams params = MakeGpuRequest();

    Tests::MockDevice device;
    device.Operational = true;

    const auto result = Runtime::ClusterKMeans(points, params, device);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->RequestedBackend, GK::Backend::GPU);
    EXPECT_EQ(result->ActualBackend, GK::Backend::CPU);
    EXPECT_TRUE(result->FellBackToCPU);
    EXPECT_EQ(result->Centroids.size(), 2u);
    EXPECT_EQ(device.CreateBufferCount, 0);
    EXPECT_EQ(device.CreatePipelineCount, 0);
}

TEST(RuntimeKMeansBackend, CpuRequestKeepsCpuTelemetry)
{
    const std::vector<glm::vec3> points = MakeSeparatedPoints();
    GK::KMeansParams params{};
    params.ClusterCount = 2u;
    params.MaxIterations = 8u;
    params.Compute = GK::Backend::CPU;

    Tests::MockDevice device;
    device.Operational = false;

    const auto result = Runtime::ClusterKMeans(points, params, device);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->RequestedBackend, GK::Backend::CPU);
    EXPECT_EQ(result->ActualBackend, GK::Backend::CPU);
    EXPECT_FALSE(result->FellBackToCPU);
    EXPECT_EQ(result->Centroids.size(), 2u);
    EXPECT_EQ(device.CreateBufferCount, 0);
    EXPECT_EQ(device.CreatePipelineCount, 0);
}
