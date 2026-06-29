#include <gtest/gtest.h>

#include <cstdint>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.RenderExtraction;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    struct SubmissionFixture
    {
        Extrinsic::Tests::MockDevice Device{};
        RHI::BufferManager BufferManager{Device};
        RHI::TextureManager TextureManager{Device, Device.Bindless};
        RHI::SamplerManager SamplerManager{Device};
        Graphics::GpuAssetCache GpuAssets{
            BufferManager,
            TextureManager,
            SamplerManager,
            Device.TransferQueue};
        Runtime::RenderExtractionCache Extraction{};
        Runtime::RuntimeObjectSpaceNormalBakeQueue Queue{};
    };

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeRequest MakeRequest(
        const std::uint64_t entityKey,
        const std::uint64_t entityGeneration,
        const Assets::AssetId generated,
        const std::uint32_t paddingTexels = 0u)
    {
        return Runtime::RuntimeObjectSpaceNormalBakeRequest{
            .EntityScopedGeneratedTextureAsset = generated,
            .SourceKey = Graphics::ObjectSpaceNormalTextureBakeSourceKey{
                .EntityKey = entityKey,
                .GeometryGeneration = 11u,
                .TexcoordGeneration = 12u,
                .NormalGeneration = 13u,
            },
            .EntityGeneration = entityGeneration,
            .Options = Graphics::ObjectSpaceNormalTextureBakeOptions{
                .Width = 64u,
                .Height = 32u,
                .PaddingTexels = paddingTexels,
                .Space = Graphics::NormalTextureSpace::ObjectSpaceNormal,
            },
            .ContentKey = Runtime::RuntimeObjectSpaceNormalBakeContentKey{
                .GeometryKey = 101u,
                .TexcoordKey = 102u,
                .NormalKey = 103u,
                .VertexCount = 3u,
                .IndexCount = 3u,
            },
            .HasStableContentKey = true,
        };
    }

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakePlan MakePlan(
        const Runtime::RuntimeObjectSpaceNormalBakeSubmission& submission,
        const std::uint32_t paddingTexels = 0u)
    {
        return Graphics::BuildObjectSpaceNormalTextureBakePlan(
            Graphics::ObjectSpaceNormalTextureBakePlanRequest{
                .GeneratedTextureAsset = submission.GeneratedTextureAsset,
                .Geometry = Graphics::ObjectSpaceNormalTextureBakeGeometryBuffers{
                    .IndexBuffer = RHI::BufferHandle{40u, 1u},
                    .TexcoordBDA = 0x1000u,
                    .NormalBDA = 0x2000u,
                    .VertexCount = 3u,
                    .IndexCount = 3u,
                },
                .Options = Graphics::ObjectSpaceNormalTextureBakeOptions{
                    .Width = 64u,
                    .Height = 32u,
                    .PaddingTexels = paddingTexels,
                    .Space = Graphics::NormalTextureSpace::ObjectSpaceNormal,
                },
                .SourceKey = submission.StaleKey.Bake.Source,
                .Pipeline = RHI::PipelineHandle{5u, 1u},
                .DebugName = "runtime-object-space-normal-bake-submission",
            });
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeResult ScheduleBake(
        SubmissionFixture& fx,
        const Assets::AssetId generated,
        const std::uint32_t paddingTexels = 0u)
    {
        return fx.Queue.Schedule(
            MakeRequest(17u, 1u, generated, paddingTexels),
            /*graphicsBackendOperational=*/true);
    }
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     BeginsCacheProducedTextureAndBuildsRecordDesc)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{81u, 1u};
    const auto scheduled = ScheduleBake(fx, generated);
    ASSERT_TRUE(scheduled.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(scheduled.Status);
    const auto plan = MakePlan(scheduled.Submission);
    ASSERT_TRUE(plan.Succeeded())
        << Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(plan.Status);

    const auto submitted = Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission,
        plan);

    ASSERT_TRUE(submitted.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeGpuSubmitStatus(
               submitted.Status);
    EXPECT_EQ(submitted.Ticket.StableEntityId, 17u);
    EXPECT_EQ(submitted.Ticket.GeneratedTextureAsset, generated);
    EXPECT_EQ(submitted.Ticket.Completion.Bake.GeneratedTextureAsset, generated);
    EXPECT_EQ(submitted.Ticket.RecordDesc.Pipeline, plan.RecordTemplate.Pipeline);
    EXPECT_EQ(submitted.Ticket.RecordDesc.IndexBuffer,
              plan.RecordTemplate.IndexBuffer);
    EXPECT_EQ(submitted.Ticket.RecordDesc.TexcoordBDA,
              plan.RecordTemplate.TexcoordBDA);
    EXPECT_EQ(submitted.Ticket.RecordDesc.NormalBDA,
              plan.RecordTemplate.NormalBDA);
    EXPECT_EQ(submitted.Ticket.RecordDesc.Width, 64u);
    EXPECT_EQ(submitted.Ticket.RecordDesc.Height, 32u);
    EXPECT_TRUE(submitted.Ticket.RecordDesc.OutputTexture.IsValid());
    EXPECT_GT(submitted.Ticket.CacheGeneration, 0u);
    EXPECT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::GpuUploading);
    EXPECT_FALSE(fx.GpuAssets.GetView(generated).has_value());
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     ReadyFramePromotionAllowsBindingHandoff)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{82u, 1u};
    const auto scheduled = ScheduleBake(fx, generated);
    ASSERT_TRUE(scheduled.Succeeded());
    const auto plan = MakePlan(scheduled.Submission);
    ASSERT_TRUE(plan.Succeeded());
    const auto submitted = Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission,
        plan);
    ASSERT_TRUE(submitted.Succeeded());

    const auto waiting = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        submitted.Ticket.Completion);
    EXPECT_EQ(waiting.Status,
              Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
                  WaitingForGpuTexture);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);

    ASSERT_TRUE(Runtime::MarkObjectSpaceNormalBakeGpuSubmissionReady(
        fx.GpuAssets,
        submitted.Ticket,
        /*readyFrame=*/7u).has_value());
    fx.GpuAssets.Tick(6u, fx.Device.GetFramesInFlight());
    EXPECT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::GpuUploading);
    fx.GpuAssets.Tick(7u, fx.Device.GetFramesInFlight());
    EXPECT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::Ready);

    const auto bound = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        submitted.Ticket.Completion);
    ASSERT_TRUE(bound.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(bound.Status);
    const auto bindings = fx.Extraction.GetMaterialTextureAssetBindings(17u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, generated);
    EXPECT_EQ(bindings->NormalSpace,
              Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);
    EXPECT_EQ(fx.Queue.PendingCount(), 0u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsStalePlanBeforeCacheAllocation)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{83u, 1u};
    const auto scheduled = ScheduleBake(fx, generated);
    ASSERT_TRUE(scheduled.Succeeded());
    auto plan = MakePlan(scheduled.Submission);
    ASSERT_TRUE(plan.Succeeded());
    ++plan.CompletionKey.Source.NormalGeneration;

    const auto rejected = Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission,
        plan);

    EXPECT_FALSE(rejected.Succeeded());
    EXPECT_EQ(rejected.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::StalePlan);
    EXPECT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::NotRequested);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsPaddedPlanBeforeCacheAllocationUntilDilationLands)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{85u, 1u};
    const auto scheduled = ScheduleBake(fx, generated, /*paddingTexels=*/4u);
    ASSERT_TRUE(scheduled.Succeeded());
    const auto plan = MakePlan(scheduled.Submission, /*paddingTexels=*/4u);
    ASSERT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::DilationUnavailable);

    const auto rejected = Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission,
        plan);

    EXPECT_FALSE(rejected.Succeeded());
    EXPECT_EQ(rejected.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidPlan);
    EXPECT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::NotRequested);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsCacheBusyWithoutConsumingQueue)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{86u, 1u};
    const auto scheduled = ScheduleBake(fx, generated);
    ASSERT_TRUE(scheduled.Succeeded());
    const auto plan = MakePlan(scheduled.Submission);
    ASSERT_TRUE(plan.Succeeded());
    ASSERT_TRUE(Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission,
        plan).Succeeded());

    const auto busy = Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission,
        plan);

    EXPECT_FALSE(busy.Succeeded());
    EXPECT_EQ(busy.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::CacheRejected);
    EXPECT_EQ(busy.CacheError, Core::ErrorCode::ResourceBusy);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
    EXPECT_FALSE(fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value());
}
