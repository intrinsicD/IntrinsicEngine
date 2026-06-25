#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.BufferManager;

#include "MockRHI.hpp"

namespace
{
    struct PackedVertex
    {
        float Px, Py, Pz;
        float U, V;
    };

    constexpr std::array<PackedVertex, 3> kTriangleVerts{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f},
    }};

    constexpr std::array<std::uint32_t, 3> kTriangleIndices{{0u, 1u, 2u}};
    constexpr std::uint64_t kManagedVertexBlockAlignment = 16u;

    [[nodiscard]] constexpr std::uint64_t AlignUp(
        const std::uint64_t value,
        const std::uint64_t alignment) noexcept
    {
        const std::uint64_t remainder = value % alignment;
        return remainder == 0u ? value : value + alignment - remainder;
    }

    [[nodiscard]] std::span<const std::byte> VertexBytes()
    {
        return std::as_bytes(std::span<const PackedVertex>{kTriangleVerts});
    }

    [[nodiscard]] std::uint64_t TriangleVertexByteCount()
    {
        return VertexBytes().size_bytes();
    }

    [[nodiscard]] std::uint64_t TriangleVertexOffset(const std::uint32_t allocationIndex)
    {
        std::uint64_t offset = 0u;
        for (std::uint32_t i = 0u; i < allocationIndex; ++i)
        {
            offset = AlignUp(offset + TriangleVertexByteCount(), kManagedVertexBlockAlignment);
        }
        return offset;
    }

    [[nodiscard]] std::uint64_t SingleFreedMiddleVertexFragmentation()
    {
        const std::uint64_t thirdEnd = TriangleVertexOffset(2u) + TriangleVertexByteCount();
        const std::uint64_t liveBytes = TriangleVertexByteCount() * 2u;
        return thirdEnd - liveBytes;
    }

    [[nodiscard]] std::uint64_t TwoLiveCompactedVertexHighWater()
    {
        return TriangleVertexOffset(1u) + TriangleVertexByteCount();
    }

    [[nodiscard]] Extrinsic::Graphics::GpuWorld::GeometryUploadDesc TriangleUpload()
    {
        return Extrinsic::Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = VertexBytes(),
            .PositionBytes = {},
            .TexcoordBytes = {},
            .NormalBytes = {},
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kTriangleVerts.size()),
            .LocalBounds = {},
            .DebugName = "compaction-contract-triangle",
            .PackedVertexColors = {},
        };
    }

    struct CompactionFixture
    {
        Extrinsic::Tests::MockDevice Device{};
        Extrinsic::RHI::BufferManager Buffers{Device};
        Extrinsic::Graphics::GpuWorld World{};

        CompactionFixture()
        {
            Device.Operational = false;
            Extrinsic::Graphics::GpuWorld::InitDesc init{};
            init.MaxInstances = 1u;
            init.MaxGeometryRecords = 4u;
            init.MaxLights = 1u;
            init.DeferredFreeFrames = 0u;
            init.VertexBufferBytes = 16u * 1024u;
            init.IndexBufferBytes = 16u * 1024u;
            EXPECT_TRUE(World.Initialize(Device, Buffers, init));
        }
    };
}

TEST(GpuWorldCompactionContract, PlanReportsFragmentationAndBlocksWhileFreesArePending)
{
    CompactionFixture fixture;

    const auto a = fixture.World.UploadGeometry(TriangleUpload());
    const auto b = fixture.World.UploadGeometry(TriangleUpload());
    const auto c = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());
    ASSERT_TRUE(c.IsValid());

    fixture.World.FreeGeometry(b);

    const auto blocked = fixture.World.PlanManagedBufferCompaction();
    EXPECT_TRUE(blocked.Enabled);
    EXPECT_TRUE(blocked.BlockedByPendingFrees);
    EXPECT_FALSE(blocked.ShouldCompact);
    EXPECT_EQ(blocked.Vertex.FragmentedBytes, SingleFreedMiddleVertexFragmentation());
    EXPECT_EQ(blocked.Index.FragmentedBytes, std::span<const std::uint32_t>{kTriangleIndices}.size_bytes());

    fixture.World.SyncFrame();
    const auto plan = fixture.World.PlanManagedBufferCompaction();
    ASSERT_FALSE(plan.BlockedByPendingFrees);
    ASSERT_TRUE(plan.ShouldCompact);
    ASSERT_EQ(plan.Relocations.size(), 1u);
    EXPECT_EQ(plan.Relocations[0].Geometry, c);
    EXPECT_EQ(plan.Relocations[0].OldVertexByteOffset, TriangleVertexOffset(2u));
    EXPECT_EQ(plan.Relocations[0].NewVertexByteOffset, TriangleVertexOffset(1u));
    EXPECT_GT(plan.BytesToMove, 0u);
}

TEST(GpuWorldCompactionContract, ApplyRelocationCompactsManagedBufferDiagnostics)
{
    CompactionFixture fixture;

    ASSERT_TRUE(fixture.World.UploadGeometry(TriangleUpload()).IsValid());
    const auto freed = fixture.World.UploadGeometry(TriangleUpload());
    const auto moved = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(freed.IsValid());
    ASSERT_TRUE(moved.IsValid());

    fixture.World.FreeGeometry(freed);
    fixture.World.SyncFrame();

    const auto managedBefore = fixture.World.GetManagedBufferDiagnostics();
    ASSERT_EQ(managedBefore.Vertex.FragmentedBytes, SingleFreedMiddleVertexFragmentation());
    ASSERT_EQ(managedBefore.CompactionCount, 0u);

    const auto plan = fixture.World.PlanManagedBufferCompaction();
    ASSERT_TRUE(plan.ShouldCompact);
    ASSERT_EQ(plan.Relocations.size(), 1u);

    const auto result = fixture.World.ApplyManagedBufferCompaction(plan);
    EXPECT_TRUE(result.Applied);
    EXPECT_FALSE(result.RejectedStaleRelocations);
    EXPECT_EQ(result.RelocationCount, 1u);
    EXPECT_EQ(result.StaleRelocationCount, 0u);
    EXPECT_EQ(result.BytesMoved, plan.BytesToMove);

    const auto after = fixture.World.GetDiagnostics();
    const auto managedAfter = fixture.World.GetManagedBufferDiagnostics();
    EXPECT_EQ(after.VertexBytesUsed, TwoLiveCompactedVertexHighWater());
    EXPECT_EQ(after.IndexBytesUsed, managedBefore.Index.LiveBytes);
    EXPECT_EQ(managedAfter.Vertex.FragmentedBytes,
              TwoLiveCompactedVertexHighWater() - managedBefore.Vertex.LiveBytes);
    EXPECT_EQ(managedAfter.Index.FragmentedBytes, 0u);
    EXPECT_EQ(managedAfter.CompactionCount, 1u);
    EXPECT_EQ(managedAfter.CompactionBytesMoved, plan.BytesToMove);

    const auto stable = fixture.World.PlanManagedBufferCompaction();
    EXPECT_FALSE(stable.ShouldCompact);
    EXPECT_TRUE(stable.Relocations.empty());
}

TEST(GpuWorldCompactionContract, DisabledOrBelowThresholdCompactionIsSkipped)
{
    CompactionFixture fixture;

    ASSERT_TRUE(fixture.World.UploadGeometry(TriangleUpload()).IsValid());
    const auto freed = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(fixture.World.UploadGeometry(TriangleUpload()).IsValid());
    ASSERT_TRUE(freed.IsValid());

    fixture.World.FreeGeometry(freed);
    fixture.World.SyncFrame();

    Extrinsic::Graphics::GpuWorld::CompactionPlanDesc disabled{};
    disabled.Enabled = false;
    const auto disabledPlan = fixture.World.PlanManagedBufferCompaction(disabled);
    EXPECT_FALSE(disabledPlan.Enabled);
    EXPECT_FALSE(disabledPlan.ShouldCompact);
    EXPECT_TRUE(disabledPlan.Relocations.empty());
    const auto disabledResult = fixture.World.ApplyManagedBufferCompaction(disabledPlan);
    EXPECT_TRUE(disabledResult.Skipped);
    EXPECT_FALSE(disabledResult.Applied);

    Extrinsic::Graphics::GpuWorld::CompactionPlanDesc threshold{};
    threshold.MinRecoverableBytes = 1u << 20u;
    const auto thresholdPlan = fixture.World.PlanManagedBufferCompaction(threshold);
    EXPECT_FALSE(thresholdPlan.ShouldCompact);
    EXPECT_FALSE(thresholdPlan.Relocations.empty());
    const auto thresholdResult = fixture.World.ApplyManagedBufferCompaction(thresholdPlan);
    EXPECT_TRUE(thresholdResult.Skipped);
    EXPECT_FALSE(thresholdResult.Applied);
}

TEST(GpuWorldCompactionContract, StaleRelocationPlanIsRejectedWithoutMutation)
{
    CompactionFixture fixture;

    ASSERT_TRUE(fixture.World.UploadGeometry(TriangleUpload()).IsValid());
    const auto freed = fixture.World.UploadGeometry(TriangleUpload());
    const auto stale = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(freed.IsValid());
    ASSERT_TRUE(stale.IsValid());

    fixture.World.FreeGeometry(freed);
    fixture.World.SyncFrame();

    const auto plan = fixture.World.PlanManagedBufferCompaction();
    ASSERT_TRUE(plan.ShouldCompact);
    ASSERT_EQ(plan.Relocations.size(), 1u);
    ASSERT_EQ(plan.Relocations[0].Geometry, stale);

    const auto before = fixture.World.GetDiagnostics();
    fixture.World.FreeGeometry(stale);

    const auto result = fixture.World.ApplyManagedBufferCompaction(plan);
    EXPECT_FALSE(result.Applied);
    EXPECT_FALSE(result.Skipped);
    EXPECT_TRUE(result.RejectedStaleRelocations);
    EXPECT_EQ(result.StaleRelocationCount, 1u);

    const auto after = fixture.World.GetDiagnostics();
    EXPECT_EQ(after.VertexBytesUsed, before.VertexBytesUsed);
    EXPECT_EQ(after.IndexBytesUsed, before.IndexBytesUsed);
    const auto managedAfter = fixture.World.GetManagedBufferDiagnostics();
    EXPECT_EQ(managedAfter.CompactionCount, 0u);
    EXPECT_EQ(managedAfter.StaleRelocationCount, 1u);
}
