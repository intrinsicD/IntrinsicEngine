#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Culling;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    enum class EventKind
    {
        FillBuffer,
        BufferBarrier,
        BindPipeline,
        PushConstants,
        Dispatch,
    };

    struct Event
    {
        EventKind Kind{};
        RHI::BufferHandle Buffer{};
        RHI::MemoryAccess Before = RHI::MemoryAccess::None;
        RHI::MemoryAccess After = RHI::MemoryAccess::None;
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events;
        std::optional<RHI::GpuCullPushConstants> LastCullPushConstants;

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override { Events.push_back({.Kind = EventKind::BindPipeline}); }
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::PushConstants});
            if (data != nullptr && size == sizeof(RHI::GpuCullPushConstants))
            {
                RHI::GpuCullPushConstants constants{};
                std::memcpy(&constants, data, sizeof(constants));
                LastCullPushConstants = constants;
            }
        }
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override { Events.push_back({.Kind = EventKind::Dispatch}); }
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle buffer, RHI::MemoryAccess before, RHI::MemoryAccess after) override
        {
            Events.push_back({.Kind = EventKind::BufferBarrier, .Buffer = buffer, .Before = before, .After = after});
        }
        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::TextureBarrierDesc& barrier : batch.TextureBarriers)
            {
                TextureBarrier(barrier.Texture, barrier.BeforeLayout, barrier.AfterLayout);
            }
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
            {
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
            }
        }
        void FillBuffer(RHI::BufferHandle buffer, std::uint64_t, std::uint64_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::FillBuffer, .Buffer = buffer});
        }
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    constexpr std::array kIndexedBuckets{
        RHI::GpuDrawBucketKind::SurfaceOpaque,
        RHI::GpuDrawBucketKind::SurfaceAlphaMask,
        RHI::GpuDrawBucketKind::Lines,
        RHI::GpuDrawBucketKind::ShadowOpaque,
        RHI::GpuDrawBucketKind::SelectionSurface,
        RHI::GpuDrawBucketKind::SelectionLines,
    };

    constexpr std::array kNonIndexedBuckets{
        RHI::GpuDrawBucketKind::Points,
        RHI::GpuDrawBucketKind::SelectionPoints,
    };

    Graphics::GpuWorld::InitDesc TinyWorldDesc()
    {
        Graphics::GpuWorld::InitDesc init{};
        init.MaxInstances = 4;
        init.MaxGeometryRecords = 4;
        init.MaxLights = 1;
        init.VertexBufferBytes = 4096;
        init.IndexBufferBytes = 4096;
        return init;
    }
}

static_assert(static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::Count) == 8u);
static_assert(static_cast<std::uint32_t>(RHI::GpuCullPhase::Count) == 2u);
static_assert(sizeof(RHI::GpuCullBucketOutput) == 24u);
static_assert(sizeof(RHI::GpuCullBucketDiagnosticsCounters) == 16u);
static_assert(sizeof(RHI::GpuCullBucketPhases) == 64u);
static_assert(sizeof(RHI::GpuCullBucketTable) == 512u);
static_assert(RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionSurface));
static_assert(RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionLines));
static_assert(!RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionPoints));
static_assert(RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SelectionSurface));
static_assert(RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SelectionLines));
static_assert(RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SelectionPoints));
static_assert(!RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SurfaceOpaque));

TEST(GraphicsCullingContracts, BucketsCoverSurfaceLinePointShadowAndSelectionDomains)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    for (const auto kind : kIndexedBuckets)
    {
        const auto& bucket = culling.GetBucket(kind);
        EXPECT_TRUE(bucket.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.DiagnosticsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_GT(bucket.Capacity, 0u) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.IndexedArgsBuffer, bucket.Phase2.IndexedArgsBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.CountBuffer, bucket.Phase2.CountBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase1).IndexedArgsBuffer,
                  bucket.IndexedArgsBuffer);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase2).IndexedArgsBuffer,
                  bucket.Phase2.IndexedArgsBuffer);
    }

    for (const auto kind : kNonIndexedBuckets)
    {
        const auto& bucket = culling.GetBucket(kind);
        EXPECT_FALSE(bucket.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.DiagnosticsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_GT(bucket.Capacity, 0u) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.Phase1.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.Phase2.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.NonIndexedArgsBuffer, bucket.Phase2.NonIndexedArgsBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.CountBuffer, bucket.Phase2.CountBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase1).NonIndexedArgsBuffer,
                  bucket.NonIndexedArgsBuffer);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase2).NonIndexedArgsBuffer,
                  bucket.Phase2.NonIndexedArgsBuffer);
    }

    culling.Shutdown();
}

TEST(GraphicsCullingContracts, CullingPassResetsDispatchesAndPublishesAllBucketMetadata)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    RecordingCommandContext cmd;
    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};

    Graphics::CullingPass pass{culling};
    pass.Execute(cmd, camera, world);

    constexpr std::size_t bucketCount = static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count);
    const std::size_t expectedEvents = (bucketCount * 6u) + 1u + 3u + (bucketCount * 5u);
    ASSERT_EQ(cmd.Events.size(), expectedEvents);

    std::size_t event = 0;
    for (std::size_t i = 0; i < bucketCount; ++i)
    {
        for (std::size_t phase = 0; phase < 2u; ++phase)
        {
            EXPECT_EQ(cmd.Events[event].Kind, EventKind::FillBuffer);
            EXPECT_TRUE(cmd.Events[event].Buffer.IsValid());
            ++event;

            EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
            EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::TransferWrite);
            EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::ShaderWrite);
            ++event;
        }

        EXPECT_EQ(cmd.Events[event].Kind, EventKind::FillBuffer);
        EXPECT_TRUE(cmd.Events[event].Buffer.IsValid());
        ++event;

        EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
        EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::TransferWrite);
        EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::ShaderWrite);
        ++event;
    }

    EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
    EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::ShaderRead);
    ++event;

    EXPECT_EQ(cmd.Events[event++].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[event++].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[event++].Kind, EventKind::Dispatch);
    ASSERT_TRUE(cmd.LastCullPushConstants.has_value());
    EXPECT_EQ(cmd.LastCullPushConstants->CullPhase,
              static_cast<std::uint32_t>(RHI::GpuCullPhase::Phase1));
    EXPECT_NE(cmd.LastCullPushConstants->CullingFlags &
                  RHI::GpuCullFlag_SelectionBucketOcclusionExempt,
              0u);
    EXPECT_EQ(cmd.LastCullPushConstants->CullingFlags &
                  RHI::GpuCullFlag_HZBStaleSkip,
              0u);

    for (std::size_t i = 0; i < bucketCount; ++i)
    {
        for (std::size_t phase = 0; phase < 2u; ++phase)
        {
            EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
            EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::ShaderWrite);
            EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::IndirectRead);
            ++event;

            EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
            EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::ShaderWrite);
            EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::IndirectRead);
            ++event;
        }

        EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
        EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::ShaderWrite);
        EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::ShaderRead);
        ++event;
    }
    EXPECT_EQ(event, cmd.Events.size());

    bool sawBucketTableWrite = false;
    const std::uint64_t expectedBucketTableBda = cmd.LastCullPushConstants->CullBucketTableBDA;
    ASSERT_NE(expectedBucketTableBda, 0u);
    for (const auto& write : device.BufferWrites)
    {
        if (device.GetBufferDeviceAddress(write.Handle) != expectedBucketTableBda)
        {
            continue;
        }
        ASSERT_EQ(write.Offset, 0u);
        ASSERT_EQ(write.Data.size(), sizeof(RHI::GpuCullBucketTable));

        RHI::GpuCullBucketTable table{};
        std::memcpy(&table, write.Data.data(), sizeof(table));
        EXPECT_GT(table.SurfaceOpaque.Phase1.Capacity, 0u);
        EXPECT_GT(table.SurfaceOpaque.Phase2.Capacity, 0u);
        EXPECT_GT(table.SurfaceAlphaMask.Phase1.Capacity, 0u);
        EXPECT_GT(table.Lines.Phase1.Capacity, 0u);
        EXPECT_GT(table.Points.Phase1.Capacity, 0u);
        EXPECT_GT(table.ShadowOpaque.Phase1.Capacity, 0u);
        EXPECT_GT(table.SelectionSurface.Phase1.Capacity, 0u);
        EXPECT_GT(table.SelectionLines.Phase1.Capacity, 0u);
        EXPECT_GT(table.SelectionPoints.Phase1.Capacity, 0u);
        sawBucketTableWrite = true;
        break;
    }
    EXPECT_TRUE(sawBucketTableWrite);

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsCullingContracts, TwoPhasePartitionIsDeterministicAndConservative)
{
    const Graphics::CullingHZBDepthSample visibleAtEqualDepth{
        .NearestDepth = 0.5f,
        .ConservativeMaxDepth = 0.5f,
        .Valid = true,
    };
    const Graphics::CullingHZBDepthSample rejectedBehindDepth{
        .NearestDepth = 0.6f,
        .ConservativeMaxDepth = 0.5f,
        .Valid = true,
    };
    const Graphics::CullingHZBDepthSample noSample{
        .NearestDepth = 0.9f,
        .ConservativeMaxDepth = 0.0f,
        .Valid = false,
    };

    EXPECT_FALSE(Graphics::HZBRejectsNearestDepth(visibleAtEqualDepth));
    EXPECT_TRUE(Graphics::HZBRejectsNearestDepth(rejectedBehindDepth));
    EXPECT_FALSE(Graphics::HZBRejectsNearestDepth(noSample));

    const std::vector<Graphics::CullingTwoPhaseCandidate> candidates{
        {
            .Bucket = RHI::GpuDrawBucketKind::SurfaceOpaque,
            .FrustumVisible = true,
            .PreviousFrameHZB = visibleAtEqualDepth,
            .CurrentFrameHZB = rejectedBehindDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::SurfaceOpaque,
            .FrustumVisible = true,
            .PreviousFrameHZB = rejectedBehindDepth,
            .CurrentFrameHZB = visibleAtEqualDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::Lines,
            .FrustumVisible = true,
            .PreviousFrameHZB = rejectedBehindDepth,
            .CurrentFrameHZB = rejectedBehindDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::Points,
            .FrustumVisible = false,
            .PreviousFrameHZB = visibleAtEqualDepth,
            .CurrentFrameHZB = visibleAtEqualDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::SelectionSurface,
            .FrustumVisible = true,
            .PreviousFrameHZB = rejectedBehindDepth,
            .CurrentFrameHZB = rejectedBehindDepth,
        },
    };

    const Graphics::CullingTwoPhasePartition first =
        Graphics::ComputeTwoPhaseCullPartition(candidates);
    const Graphics::CullingTwoPhasePartition second =
        Graphics::ComputeTwoPhaseCullPartition(candidates);

    ASSERT_EQ(first.Decisions.size(), candidates.size());
    EXPECT_EQ(first.Decisions, second.Decisions);
    EXPECT_EQ(first.Decisions[0], Graphics::CullingTwoPhaseDecision::Phase1Visible);
    EXPECT_EQ(first.Decisions[1], Graphics::CullingTwoPhaseDecision::Phase2Rescued);
    EXPECT_EQ(first.Decisions[2], Graphics::CullingTwoPhaseDecision::Phase2Rejected);
    EXPECT_EQ(first.Decisions[3], Graphics::CullingTwoPhaseDecision::FrustumRejected);
    EXPECT_EQ(first.Decisions[4], Graphics::CullingTwoPhaseDecision::Phase1Visible);

    const auto& surface = first.Buckets[static_cast<std::size_t>(RHI::GpuDrawBucketKind::SurfaceOpaque)];
    EXPECT_EQ(surface.Phase1VisibleCount, 1u);
    EXPECT_EQ(surface.Phase1RejectedCount, 1u);
    EXPECT_EQ(surface.Phase2RescuedCount, 1u);

    const auto& lines = first.Buckets[static_cast<std::size_t>(RHI::GpuDrawBucketKind::Lines)];
    EXPECT_EQ(lines.Phase1VisibleCount, 0u);
    EXPECT_EQ(lines.Phase1RejectedCount, 1u);
    EXPECT_EQ(lines.Phase2RescuedCount, 0u);

    EXPECT_EQ(first.FrustumRejectedCount, 1u);
    EXPECT_EQ(first.SelectionOcclusionExemptCount, 1u);

    const auto& selection = first.Buckets[static_cast<std::size_t>(RHI::GpuDrawBucketKind::SelectionSurface)];
    EXPECT_EQ(selection.Phase1VisibleCount, 1u);
    EXPECT_EQ(selection.Phase1RejectedCount, 0u);
    EXPECT_EQ(selection.Phase2RescuedCount, 0u);
}

TEST(GraphicsCullingContracts, CameraTransitionDecisionSkipsOnExplicitFlagAndDeltaThresholds)
{
    const Graphics::CullingCameraTransitionThresholds thresholds{
        .PositionDeltaThreshold = 5.0f,
        .DirectionDotThreshold = 0.95f,
    };

    const Graphics::CullingCameraTransitionState previous{
        .Position = {0.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
    };

    const Graphics::CullingCameraTransitionState explicitCurrent{
        .Position = {0.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
        .ExplicitCameraTransition = true,
    };
    const Graphics::CullingCameraTransitionDecision explicitDecision =
        Graphics::EvaluateCameraTransition(previous, explicitCurrent, thresholds);
    EXPECT_TRUE(explicitDecision.SkipHZBPhase1);
    EXPECT_TRUE(explicitDecision.ExplicitTrigger);
    EXPECT_FALSE(explicitDecision.PositionDeltaTrigger);
    EXPECT_FALSE(explicitDecision.DirectionDeltaTrigger);

    const Graphics::CullingCameraTransitionState movedCurrent{
        .Position = {6.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
    };
    const Graphics::CullingCameraTransitionDecision movedDecision =
        Graphics::EvaluateCameraTransition(previous, movedCurrent, thresholds);
    EXPECT_TRUE(movedDecision.SkipHZBPhase1);
    EXPECT_TRUE(movedDecision.PositionDeltaTrigger);

    const Graphics::CullingCameraTransitionState rotatedCurrent{
        .Position = {0.0f, 0.0f, 0.0f},
        .Forward = {1.0f, 0.0f, 0.0f},
        .Valid = true,
    };
    const Graphics::CullingCameraTransitionDecision rotatedDecision =
        Graphics::EvaluateCameraTransition(previous, rotatedCurrent, thresholds);
    EXPECT_TRUE(rotatedDecision.SkipHZBPhase1);
    EXPECT_TRUE(rotatedDecision.DirectionDeltaTrigger);

    const Graphics::CullingCameraTransitionState stableCurrent{
        .Position = {1.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
    };
    const Graphics::CullingCameraTransitionDecision stableDecision =
        Graphics::EvaluateCameraTransition(previous, stableCurrent, thresholds);
    EXPECT_FALSE(stableDecision.SkipHZBPhase1);
}

TEST(GraphicsCullingContracts, CullingDiagnosticsCountExplicitAndDeltaHZBStaleSkips)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};
    camera.CameraPosition = {0.0f, 0.0f, 0.0f, 0.0f};
    camera.CameraDirection = {0.0f, 0.0f, -1.0f, 0.0f};
    camera.CullingFlags = RHI::CameraCulling_ExplicitTransition;

    RecordingCommandContext explicitCmd;
    culling.ResetCounters(explicitCmd);
    culling.DispatchCull(explicitCmd, camera, world);
    Graphics::CullingDiagnostics diagnostics = culling.GetDiagnostics();
    EXPECT_EQ(diagnostics.HzbStaleSkipCount, 1u);
    EXPECT_TRUE(diagnostics.LastHzbStaleSkip);
    EXPECT_TRUE(diagnostics.LastExplicitCameraTransition);
    ASSERT_TRUE(explicitCmd.LastCullPushConstants.has_value());
    EXPECT_NE(explicitCmd.LastCullPushConstants->CullingFlags & RHI::GpuCullFlag_HZBStaleSkip, 0u);

    camera.CullingFlags = RHI::CameraCulling_None;
    RecordingCommandContext stableCmd;
    culling.ResetCounters(stableCmd);
    culling.DispatchCull(stableCmd, camera, world);
    diagnostics = culling.GetDiagnostics();
    EXPECT_EQ(diagnostics.HzbStaleSkipCount, 1u);
    EXPECT_FALSE(diagnostics.LastHzbStaleSkip);
    ASSERT_TRUE(stableCmd.LastCullPushConstants.has_value());
    EXPECT_EQ(stableCmd.LastCullPushConstants->CullingFlags & RHI::GpuCullFlag_HZBStaleSkip, 0u);

    camera.CameraPosition = {20.0f, 0.0f, 0.0f, 0.0f};
    RecordingCommandContext deltaCmd;
    culling.ResetCounters(deltaCmd);
    culling.DispatchCull(deltaCmd, camera, world);
    diagnostics = culling.GetDiagnostics();
    EXPECT_EQ(diagnostics.HzbStaleSkipCount, 2u);
    EXPECT_TRUE(diagnostics.LastHzbStaleSkip);
    EXPECT_TRUE(diagnostics.LastCameraPositionDeltaTransition);
    EXPECT_FALSE(diagnostics.LastExplicitCameraTransition);
    ASSERT_TRUE(deltaCmd.LastCullPushConstants.has_value());
    EXPECT_NE(deltaCmd.LastCullPushConstants->CullingFlags & RHI::GpuCullFlag_HZBStaleSkip, 0u);

    culling.Shutdown();
    world.Shutdown();
}
