#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
    namespace Graphics = Extrinsic::Graphics;

    struct Fixture
    {
        Extrinsic::Tests::MockDevice Device{};
        Extrinsic::RHI::BufferManager Buffers{Device};
        Graphics::GpuWorld World{};
        Graphics::GeometryResidencyCoordinator Coordinator;

        Fixture() : Coordinator(World)
        {
            Graphics::GpuWorld::InitDesc init{};
            init.MaxInstances = 1u;
            init.MaxGeometryRecords = 16u;
            init.MaxLights = 1u;
            init.VertexBufferBytes = 1u << 16u;
            init.IndexBufferBytes = 1u << 16u;
            init.DeferredFreeFrames = 0u;
            EXPECT_TRUE(World.Initialize(Device, Buffers, init));
        }
    };

    const std::array<glm::vec3, 3> kPositions{{
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.0f,  0.5f, 0.0f},
    }};
    const std::array<glm::vec3, 3> kNormals{{
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    }};
    const std::array<std::uint32_t, 3> kIndices{{0u, 1u, 2u}};

    [[nodiscard]] Graphics::GeometryUploadPlan TrianglePlan(
        const Graphics::GeometryResidencyKey key,
        const std::uint64_t generation,
        const Graphics::GeometryUploadUpdateClass updateClass =
            Graphics::GeometryUploadUpdateClass::FullReplacement,
        const Graphics::GpuWorld::GeometryChannelUpdateMask channels = {})
    {
        const Graphics::GpuWorld::GeometryUploadDesc desc{
            .PositionBytes = std::as_bytes(std::span<const glm::vec3>{kPositions}),
            .NormalBytes = std::as_bytes(std::span<const glm::vec3>{kNormals}),
            .SurfaceIndices = kIndices,
            .VertexCount = 3u,
            .DebugName = "geometry-residency-contract",
        };
        return Graphics::MakeGeometryUploadPlan(
            key, generation, desc, updateClass, channels);
    }
}

TEST(GeometryResidencyContract, ValidationFailsClosedWithDeterministicReasons)
{
    const Graphics::GeometryResidencyKey key{1u, 7u, 0u};
    auto plan = TrianglePlan(key, 1u);
    EXPECT_TRUE(Graphics::ValidateGeometryUploadPlan(plan).Valid());

    struct Case
    {
        Graphics::GeometryUploadPlanStatus Expected;
        void (*Mutate)(Graphics::GeometryUploadPlan&);
    };
    const std::array cases{
        Case{Graphics::GeometryUploadPlanStatus::InvalidKey,
             [](auto& value) { value.Key = {}; }},
        Case{Graphics::GeometryUploadPlanStatus::MissingGeneration,
             [](auto& value) { value.Generation = 0u; }},
        Case{Graphics::GeometryUploadPlanStatus::MissingPositions,
             [](auto& value) { value.PositionBytes.clear(); }},
        Case{Graphics::GeometryUploadPlanStatus::InvalidIndex,
             [](auto& value) { value.SurfaceIndices.back() = value.VertexCount; }},
        Case{Graphics::GeometryUploadPlanStatus::InvalidPartialUpdate,
             [](auto& value) {
                 value.UpdateClass = Graphics::GeometryUploadUpdateClass::PartialPreferred;
                 value.UpdateChannels = {};
             }},
    };

    for (const Case& test : cases)
    {
        auto candidate = plan;
        test.Mutate(candidate);
        const auto validation = Graphics::ValidateGeometryUploadPlan(candidate);
        EXPECT_EQ(validation.Status, test.Expected);
        EXPECT_FALSE(validation.Diagnostic.empty());
    }
}

TEST(GeometryResidencyContract, ReconcileCoversReuseStalePartialAndFullReplacement)
{
    Fixture fixture;
    const Graphics::GeometryResidencyKey key{2u, 42u, 0u};

    const auto initial = fixture.Coordinator.Reconcile(TrianglePlan(key, 1u));
    ASSERT_EQ(initial.Status, Graphics::GeometryResidencyStatus::Uploaded);
    ASSERT_TRUE(initial.Handle.IsValid());

    const auto reused = fixture.Coordinator.Reconcile(TrianglePlan(key, 1u));
    EXPECT_EQ(reused.Status, Graphics::GeometryResidencyStatus::Reused);
    EXPECT_EQ(reused.Handle, initial.Handle);
    ASSERT_EQ(fixture.Coordinator.Find(key)->RefCount, 1u);

    auto partial = TrianglePlan(
        key,
        2u,
        Graphics::GeometryUploadUpdateClass::PartialPreferred,
        Graphics::GpuWorld::GeometryChannelUpdateMask{.Normal = true});
    const float changedNormalX = 1.0f;
    std::memcpy(partial.NormalBytes.data(),
                &changedNormalX,
                sizeof(changedNormalX));
    const auto updated = fixture.Coordinator.Reconcile(partial);
    EXPECT_EQ(updated.Status, Graphics::GeometryResidencyStatus::PartiallyUpdated);
    EXPECT_EQ(updated.Handle, initial.Handle);

    const auto stale = fixture.Coordinator.Reconcile(TrianglePlan(key, 1u));
    EXPECT_EQ(stale.Status, Graphics::GeometryResidencyStatus::StalePlan);
    EXPECT_EQ(stale.Handle, initial.Handle);
    EXPECT_FALSE(stale.Succeeded());

    const auto replaced = fixture.Coordinator.Reconcile(TrianglePlan(key, 3u));
    ASSERT_EQ(replaced.Status, Graphics::GeometryResidencyStatus::FullyReuploaded);
    EXPECT_NE(replaced.Handle, initial.Handle);
    EXPECT_EQ(replaced.RetiringHandle, initial.Handle);
    EXPECT_EQ(fixture.World.GetLiveGeometryCount(), 2u);

    fixture.Coordinator.Tick(10u, 2u);
    EXPECT_EQ(fixture.World.GetLiveGeometryCount(), 2u);
    fixture.Coordinator.Tick(12u, 2u);
    EXPECT_EQ(fixture.World.GetLiveGeometryCount(), 1u);

    const auto& stats = fixture.Coordinator.Stats();
    EXPECT_EQ(stats.Uploads, 1u);
    EXPECT_EQ(stats.ReuseHits, 1u);
    EXPECT_EQ(stats.PartialUpdates, 1u);
    EXPECT_EQ(stats.FullReuploads, 1u);
    EXPECT_EQ(stats.StalePlans, 1u);
    EXPECT_EQ(stats.FreeRetires, 1u);
}

TEST(GeometryResidencyContract, SharedAcquireReleaseAndResurrectionUseOneRetirePath)
{
    Fixture fixture;
    const Graphics::GeometryResidencyKey key{3u, 9u, 1u};
    const auto plan = TrianglePlan(key, 1u);

    const auto first = fixture.Coordinator.Acquire(plan);
    const auto second = fixture.Coordinator.Acquire(plan);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(first.Handle, second.Handle);
    EXPECT_EQ(fixture.Coordinator.Find(key)->RefCount, 2u);

    EXPECT_FALSE(fixture.Coordinator.Release(key));
    EXPECT_TRUE(fixture.Coordinator.Release(key));
    EXPECT_EQ(fixture.Coordinator.PendingRetireCount(), 1u);
    fixture.Coordinator.Tick(100u, 3u);

    const auto resurrected = fixture.Coordinator.Acquire(plan);
    EXPECT_EQ(resurrected.Status, Graphics::GeometryResidencyStatus::Reused);
    EXPECT_EQ(resurrected.Handle, first.Handle);
    EXPECT_EQ(fixture.Coordinator.PendingRetireCount(), 0u);
    EXPECT_EQ(fixture.Coordinator.Stats().RetireCancellations, 1u);

    EXPECT_TRUE(fixture.Coordinator.Release(key));
    fixture.Coordinator.Tick(200u, 1u);
    fixture.Coordinator.Tick(201u, 1u);
    EXPECT_FALSE(fixture.Coordinator.Find(key).has_value());
    EXPECT_EQ(fixture.World.GetLiveGeometryCount(), 0u);
}

TEST(GeometryResidencyContract, AcquireReportsSaturationWithoutDroppingResidentHandle)
{
    Fixture fixture;
    const Graphics::GeometryResidencyKey key{4u, 11u, 0u};
    const auto plan = TrianglePlan(key, 1u);
    const auto initial = fixture.Coordinator.Acquire(plan);
    ASSERT_TRUE(initial.Succeeded());
    ASSERT_TRUE(fixture.Coordinator.PrimeRefCountForTest(
        key, std::numeric_limits<std::uint32_t>::max()));

    const auto saturated = fixture.Coordinator.Acquire(plan);
    EXPECT_EQ(saturated.Status, Graphics::GeometryResidencyStatus::RefCountSaturated);
    EXPECT_TRUE(saturated.Succeeded());
    EXPECT_EQ(saturated.Handle, initial.Handle);
    EXPECT_EQ(fixture.Coordinator.Stats().RefCountSaturated, 1u);
}

TEST(GeometryResidencyContract, CopiedPlanDoesNotBorrowSubmissionStorage)
{
    std::array<glm::vec3, 3> positions = kPositions;
    const Graphics::GpuWorld::GeometryUploadDesc desc{
        .PositionBytes = std::as_bytes(std::span<const glm::vec3>{positions}),
        .SurfaceIndices = kIndices,
        .VertexCount = 3u,
        .DebugName = "copied-plan",
    };
    const auto plan = Graphics::MakeGeometryUploadPlan({5u, 13u, 0u}, 1u, desc);
    const std::byte first = plan.PositionBytes.front();
    positions[0].x = 99.0f;
    EXPECT_EQ(plan.PositionBytes.front(), first);
    EXPECT_EQ(plan.DebugName, "copied-plan");
}
