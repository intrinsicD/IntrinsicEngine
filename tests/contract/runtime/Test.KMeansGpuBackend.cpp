#include <gtest/gtest.h>

#include <cstdint>

import Extrinsic.Runtime.KMeansGpuBackend;

#include "MockRHI.hpp"

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace Tests = Extrinsic::Tests;

    [[nodiscard]] Runtime::KMeansGpuPlanDesc MakePlanDesc(
        const std::uint32_t points,
        const std::uint32_t clusters,
        const std::uint32_t iterations,
        const std::uint32_t groupSize = Runtime::kKMeansGpuGroupSize)
    {
        return Runtime::KMeansGpuPlanDesc{
            .PointCount = points,
            .ClusterCount = clusters,
            .MaxIterations = iterations,
            .GroupSize = groupSize,
            .ConvergenceTolerance = 1.0e-4f,
        };
    }

    [[nodiscard]] const Runtime::KMeansGpuBufferSpan* FindSpan(
        const Runtime::KMeansGpuBufferLayout& layout,
        const Runtime::KMeansGpuBufferRole role)
    {
        for (const Runtime::KMeansGpuBufferSpan& span : layout.Spans)
        {
            if (span.Role == role)
                return &span;
        }
        return nullptr;
    }
}

TEST(KMeansGpuBackend, StructLayoutsMatchShaderContract)
{
    EXPECT_EQ(sizeof(Runtime::KMeansGpuStateBufferRecord), 96u);
    EXPECT_EQ(sizeof(Runtime::KMeansGpuPassPushConstants), 32u);
}

TEST(KMeansGpuBackend, BufferLayoutIsPackedAlignedAndSized)
{
    const Runtime::KMeansGpuBufferLayout layout =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(4u, 2u, 8u));

    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.PointCount, 4u);
    EXPECT_EQ(layout.ClusterCount, 2u);
    EXPECT_EQ(layout.StateBytes, sizeof(Runtime::KMeansGpuStateBufferRecord));

    // Readback sizes: labels/dist per point, centroids packed vec3 per cluster.
    EXPECT_EQ(layout.LabelsReadbackBytes, 4u * sizeof(std::uint32_t));
    EXPECT_EQ(layout.SquaredDistancesReadbackBytes, 4u * sizeof(float));
    EXPECT_EQ(layout.CentroidsReadbackBytes, 2u * 3u * sizeof(float));

    ASSERT_EQ(layout.Spans.size(), 12u);

    const Runtime::KMeansGpuBufferSpan* positionX =
        FindSpan(layout, Runtime::KMeansGpuBufferRole::PositionX);
    ASSERT_NE(positionX, nullptr);
    EXPECT_EQ(positionX->OffsetBytes, 0u);
    EXPECT_EQ(positionX->SizeBytes, 4u * sizeof(float));

    // Spans are 16-byte aligned, monotonic, and inside the work buffer.
    std::uint64_t previousEnd = 0u;
    for (const Runtime::KMeansGpuBufferSpan& span : layout.Spans)
    {
        EXPECT_EQ(span.OffsetBytes % 16u, 0u);
        EXPECT_GE(span.OffsetBytes, previousEnd);
        EXPECT_LE(span.OffsetBytes + span.SizeBytes, layout.WorkBufferBytes);
        previousEnd = span.OffsetBytes + span.SizeBytes;
    }
    EXPECT_GT(layout.WorkBufferBytes, 0u);
}

TEST(KMeansGpuBackend, LayoutClampsClustersToPointCountAndRejectsEmpty)
{
    // k requested above n clamps to n.
    const Runtime::KMeansGpuBufferLayout clamped =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(3u, 8u, 4u));
    ASSERT_TRUE(clamped.IsValid());
    EXPECT_EQ(clamped.ClusterCount, 3u);

    // Empty input fails closed.
    const Runtime::KMeansGpuBufferLayout empty =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(0u, 4u, 4u));
    EXPECT_FALSE(empty.IsValid());
    EXPECT_EQ(empty.Status, Runtime::KMeansGpuStatus::InvalidInput);

    const Runtime::KMeansGpuBufferLayout zeroClusters =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(4u, 0u, 4u));
    EXPECT_FALSE(zeroClusters.IsValid());
    EXPECT_EQ(zeroClusters.Status, Runtime::KMeansGpuStatus::InvalidInput);
}

TEST(KMeansGpuBackend, DispatchPlanEmitsResetAssignUpdatePerIteration)
{
    const Runtime::KMeansGpuDispatchPlan plan =
        Runtime::ComputeKMeansGpuDispatchPlan(MakePlanDesc(4u, 2u, 8u));

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.MaxIterations, 8u);
    ASSERT_EQ(plan.Dispatches.size(), 8u * 3u);

    for (std::uint32_t iteration = 0u; iteration < 8u; ++iteration)
    {
        const Runtime::KMeansGpuDispatchDesc& reset = plan.Dispatches[iteration * 3u + 0u];
        const Runtime::KMeansGpuDispatchDesc& assign = plan.Dispatches[iteration * 3u + 1u];
        const Runtime::KMeansGpuDispatchDesc& update = plan.Dispatches[iteration * 3u + 2u];

        EXPECT_EQ(reset.Kind, Runtime::KMeansGpuPassKind::Reset);
        EXPECT_EQ(assign.Kind, Runtime::KMeansGpuPassKind::Assign);
        EXPECT_EQ(update.Kind, Runtime::KMeansGpuPassKind::Update);
        EXPECT_EQ(reset.Iteration, iteration);
        EXPECT_EQ(assign.Iteration, iteration);
        EXPECT_EQ(update.Iteration, iteration);

        // ceil(k/G) and ceil(n/G) with the default 256 group size collapse to 1.
        EXPECT_EQ(reset.GroupCountX, 1u);
        EXPECT_EQ(assign.GroupCountX, 1u);
        EXPECT_EQ(update.GroupCountX, 1u);
        EXPECT_EQ(assign.ElementCount, 4u);
        EXPECT_EQ(update.ElementCount, 2u);
    }
}

TEST(KMeansGpuBackend, DispatchPlanGroupCountsUseCeilDivision)
{
    // Small group size exercises the ceil rounding: 5 points / 2 = 3 groups.
    const Runtime::KMeansGpuDispatchPlan plan =
        Runtime::ComputeKMeansGpuDispatchPlan(MakePlanDesc(5u, 3u, 1u, /*groupSize=*/2u));

    ASSERT_TRUE(plan.IsValid());
    ASSERT_EQ(plan.Dispatches.size(), 3u);
    EXPECT_EQ(plan.Dispatches[0].Kind, Runtime::KMeansGpuPassKind::Reset);
    EXPECT_EQ(plan.Dispatches[0].GroupCountX, 2u); // ceil(3/2)
    EXPECT_EQ(plan.Dispatches[1].Kind, Runtime::KMeansGpuPassKind::Assign);
    EXPECT_EQ(plan.Dispatches[1].GroupCountX, 3u); // ceil(5/2)
    EXPECT_EQ(plan.Dispatches[2].Kind, Runtime::KMeansGpuPassKind::Update);
    EXPECT_EQ(plan.Dispatches[2].GroupCountX, 2u); // ceil(3/2)
}

TEST(KMeansGpuBackend, ResolveFailsClosedOnNonOperationalDevice)
{
    Tests::MockDevice device;
    device.Operational = false;

    const Runtime::KMeansGpuResolveResult resolved =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = &device, .Plan = MakePlanDesc(4u, 2u, 8u)});

    EXPECT_EQ(resolved.Status, Runtime::KMeansGpuStatus::DeviceUnavailable);
    EXPECT_FALSE(resolved.GpuExecutionAvailable);
    EXPECT_TRUE(resolved.CpuFallbackRecommended);
    // The plan is still computed so later slices can record against it.
    EXPECT_TRUE(resolved.Plan.IsValid());
}

TEST(KMeansGpuBackend, ResolveOnOperationalDeviceIsPlanningOnlyForNow)
{
    Tests::MockDevice device;
    device.Operational = true;

    const Runtime::KMeansGpuResolveResult resolved =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = &device, .Plan = MakePlanDesc(4u, 2u, 8u)});

    EXPECT_EQ(resolved.Status, Runtime::KMeansGpuStatus::PlanningOnly);
    EXPECT_FALSE(resolved.GpuExecutionAvailable);
    EXPECT_TRUE(resolved.CpuFallbackRecommended);
    EXPECT_TRUE(resolved.Plan.IsValid());
    EXPECT_EQ(resolved.Plan.Dispatches.size(), 8u * 3u);
}

TEST(KMeansGpuBackend, ResolveReportsMissingDeviceAndInvalidPlan)
{
    const Runtime::KMeansGpuResolveResult missing =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = nullptr, .Plan = MakePlanDesc(4u, 2u, 8u)});
    EXPECT_EQ(missing.Status, Runtime::KMeansGpuStatus::MissingDevice);
    EXPECT_FALSE(missing.GpuExecutionAvailable);
    EXPECT_TRUE(missing.CpuFallbackRecommended);

    Tests::MockDevice device;
    device.Operational = true;
    const Runtime::KMeansGpuResolveResult invalid =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = &device, .Plan = MakePlanDesc(0u, 2u, 8u)});
    EXPECT_EQ(invalid.Status, Runtime::KMeansGpuStatus::InvalidInput);
    EXPECT_FALSE(invalid.GpuExecutionAvailable);
    EXPECT_TRUE(invalid.CpuFallbackRecommended);
}
