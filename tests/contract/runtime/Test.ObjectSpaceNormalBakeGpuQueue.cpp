#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>

import Extrinsic.Asset.Registry;
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
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeService;
import Extrinsic.Runtime.RenderExtraction;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    struct GpuQueueFixture
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
        Runtime::RuntimeObjectSpaceNormalBakeGpuQueue Queue{};
        Runtime::JobService Jobs{};
    };

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeRequest MakeRequest(
        const std::uint64_t entityKey,
        const std::uint64_t entityGeneration,
        const Assets::AssetId generated)
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
                .PaddingTexels = 0u,
                .Space = Graphics::NormalTextureSpace::ObjectSpaceNormal,
            },
            .ContentKey = Runtime::RuntimeObjectSpaceNormalBakeContentKey{
                .GeometryKey = 101u + entityGeneration,
                .TexcoordKey = 102u,
                .NormalKey = 103u,
                .VertexCount = 3u,
                .IndexCount = 3u,
            },
            .HasStableContentKey = true,
        };
    }

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakePlan MakePlan(
        const Runtime::RuntimeObjectSpaceNormalBakeSubmission& submission)
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
                    .PaddingTexels = 0u,
                    .Space = Graphics::NormalTextureSpace::ObjectSpaceNormal,
                },
                .SourceKey = submission.StaleKey.Bake.Source,
                .Pipeline = RHI::PipelineHandle{5u, 1u},
                .DebugName = "runtime-object-space-normal-bake-gpu-queue",
            });
    }

    void ConfigureQueue(GpuQueueFixture& fx, const std::uint64_t readyFrame = 2u)
    {
        fx.Queue.SetDependencies(
            Runtime::RuntimeObjectSpaceNormalBakeGpuQueueDependencies{
                .GpuAssets = &fx.GpuAssets,
                .RenderExtraction = &fx.Extraction,
                .BuildPlan =
                    [](const Runtime::RuntimeObjectSpaceNormalBakeSubmission&
                           submission)
                    {
                        Graphics::ObjectSpaceNormalTextureBakePlan plan =
                            MakePlan(submission);
                        return Runtime::
                            RuntimeObjectSpaceNormalBakeGpuQueuePlanResult{
                                .Status = plan.Succeeded()
                                    ? Runtime::
                                          RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::
                                              Ready
                                    : Runtime::
                                          RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::
                                              Invalid,
                                .Plan = std::move(plan),
                            };
                    },
                .ReadyFrame = [readyFrame] { return readyFrame; },
                .MaxSubmissionsPerFrame = 2u,
                .MaxBindingsPerDrain = 2u,
            });
    }
}

TEST(RuntimeObjectSpaceNormalBakeGpuQueue,
     ReadyFrameAccountingWaitsForEveryInFlightFrameBeforeBinding)
{
    GpuQueueFixture fx;
    fx.Device.GlobalFrameNumber = 40u;
    fx.Device.FramesInFlight = 3u;
    const std::uint64_t readyFrame = Runtime::ObjectSpaceNormalBakeReadyFrame(
        fx.Device.GetGlobalFrameNumber(),
        fx.Device.GetFramesInFlight());
    ASSERT_EQ(readyFrame, 43u);
    ConfigureQueue(fx, readyFrame);
    Extrinsic::Tests::MockCommandContext commandContext;

    const Runtime::GpuQueueParticipantHandle participant =
        fx.Jobs.RegisterGpuQueueParticipant(fx.Queue.MakeGpuQueueParticipantDesc());
    ASSERT_TRUE(participant.IsValid());

    const Assets::AssetId generated{90u, 1u};
    const auto scheduled = fx.Queue.Queue().Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(scheduled.Succeeded());

    fx.Jobs.RecordGpuQueueFrameCommands(commandContext);
    ASSERT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);

    for (std::uint64_t frame = 41u; frame < readyFrame; ++frame)
    {
        fx.GpuAssets.Tick(frame, fx.Device.GetFramesInFlight());
        EXPECT_EQ(fx.GpuAssets.GetState(generated),
                  Graphics::GpuAssetState::GpuUploading)
            << "frame " << frame;
        EXPECT_EQ(fx.Jobs.DrainGpuQueueCompletedTransfers(), 1u);
        EXPECT_FALSE(
            fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value())
            << "frame " << frame;
    }

    fx.GpuAssets.Tick(readyFrame, fx.Device.GetFramesInFlight());
    ASSERT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::Ready);
    EXPECT_EQ(fx.Jobs.DrainGpuQueueCompletedTransfers(), 1u);

    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        fx.Extraction.GetMaterialTextureAssetBindings(17u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, generated);
}

TEST(RuntimeObjectSpaceNormalBakeGpuQueue,
     JobServiceParticipantRecordsPromotesAndBindsReadyTexture)
{
    GpuQueueFixture fx;
    ConfigureQueue(fx);
    Extrinsic::Tests::MockCommandContext commandContext;

    const Runtime::GpuQueueParticipantHandle participant =
        fx.Jobs.RegisterGpuQueueParticipant(fx.Queue.MakeGpuQueueParticipantDesc());
    ASSERT_TRUE(participant.IsValid());

    const Assets::AssetId generated{91u, 1u};
    const auto scheduled = fx.Queue.Queue().Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(scheduled.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(
               scheduled.Status);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 1u);

    fx.Jobs.RecordGpuQueueFrameCommands(commandContext);

    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);
    EXPECT_EQ(fx.Queue.Diagnostics().LastRecordAttempted, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().LastRecordSubmitted, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 1u);
    EXPECT_EQ(commandContext.BindPipelineCalls, 1);
    EXPECT_EQ(commandContext.DrawIndexedCalls, 1);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);

    EXPECT_EQ(fx.Jobs.DrainGpuQueueCompletedTransfers(), 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().LastDrainProcessed, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().WaitingBindings, 1u);
    EXPECT_FALSE(fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value());

    fx.GpuAssets.Tick(2u, fx.Device.GetFramesInFlight());
    ASSERT_EQ(fx.GpuAssets.GetState(generated), Graphics::GpuAssetState::Ready);

    EXPECT_EQ(fx.Jobs.DrainGpuQueueCompletedTransfers(), 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().LastDrainProcessed, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().LastDrainBound, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().BoundCompletions, 1u);
    EXPECT_EQ(fx.Queue.Queue().PendingCount(), 0u);

    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        fx.Extraction.GetMaterialTextureAssetBindings(17u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, generated);
    EXPECT_EQ(bindings->NormalSpace,
              Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);
}

TEST(RuntimeObjectSpaceNormalBakeGpuQueue,
     SupersededPendingSubmissionIsDiscardedBeforeRecording)
{
    GpuQueueFixture fx;
    ConfigureQueue(fx);
    Extrinsic::Tests::MockCommandContext commandContext;

    const Assets::AssetId oldTexture{92u, 1u};
    const auto oldJob = fx.Queue.Queue().Schedule(
        MakeRequest(17u, 1u, oldTexture),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(oldJob.Succeeded());

    const Assets::AssetId replacementTexture{93u, 1u};
    const auto replacementJob = fx.Queue.Queue().Schedule(
        MakeRequest(17u, 2u, replacementTexture),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(replacementJob.Succeeded());
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 2u);

    fx.Queue.RecordFrameCommands(commandContext);

    EXPECT_EQ(fx.Queue.Diagnostics().LastRecordAttempted, 2u);
    EXPECT_EQ(fx.Queue.Diagnostics().PendingStaleDiscards, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 1u);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);
    EXPECT_EQ(fx.GpuAssets.GetState(oldTexture),
              Graphics::GpuAssetState::NotRequested);
    EXPECT_EQ(fx.GpuAssets.GetState(replacementTexture),
              Graphics::GpuAssetState::GpuUploading);
}

TEST(RuntimeObjectSpaceNormalBakeGpuQueue,
     ReadyContentKeyReuseBindsWithoutRecordingDuplicateBake)
{
    GpuQueueFixture fx;
    ConfigureQueue(fx);
    Extrinsic::Tests::MockCommandContext commandContext;

    const Assets::AssetId generated{94u, 1u};
    const auto first = fx.Queue.Queue().Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(first.Succeeded());

    fx.Queue.RecordFrameCommands(commandContext);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 1u);
    EXPECT_EQ(commandContext.DrawIndexedCalls, 1);

    fx.GpuAssets.Tick(2u, fx.Device.GetFramesInFlight());
    ASSERT_EQ(fx.Queue.DrainCompletedTransfers(), 1u);
    ASSERT_TRUE(fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value());

    const Assets::AssetId unusedEntityScopedFallback{95u, 1u};
    const auto reuse = fx.Queue.Queue().Schedule(
        MakeRequest(23u, 1u, unusedEntityScopedFallback),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(reuse.Succeeded());
    EXPECT_EQ(reuse.Submission.GeneratedTextureAsset, generated);
    ASSERT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 1u);

    fx.Queue.RecordFrameCommands(commandContext);

    EXPECT_EQ(commandContext.DrawIndexedCalls, 1);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().BoundCompletions, 2u);
    EXPECT_EQ(fx.GpuAssets.GetState(unusedEntityScopedFallback),
              Graphics::GpuAssetState::NotRequested);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);

    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        fx.Extraction.GetMaterialTextureAssetBindings(23u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, generated);
}

TEST(RuntimeObjectSpaceNormalBakeGpuQueue,
     RecordFailureReleasesCacheSlotAndExplicitRetrySucceeds)
{
    GpuQueueFixture fx;
    Extrinsic::Tests::MockCommandContext commandContext;
    std::uint32_t planCalls = 0u;
    fx.Queue.SetDependencies(
        Runtime::RuntimeObjectSpaceNormalBakeGpuQueueDependencies{
            .GpuAssets = &fx.GpuAssets,
            .RenderExtraction = &fx.Extraction,
            .BuildPlan =
                [&planCalls](
                    const Runtime::RuntimeObjectSpaceNormalBakeSubmission&
                        submission)
                {
                    Graphics::ObjectSpaceNormalTextureBakePlan plan =
                        MakePlan(submission);
                    if (planCalls++ == 0u)
                    {
                        // Keep the cache request valid so BeginGpuProducedTexture
                        // opens a slot, then invalidate only the record template.
                        plan.RecordTemplate.Pipeline = {};
                    }
                    return Runtime::RuntimeObjectSpaceNormalBakeGpuQueuePlanResult{
                        .Status = Runtime::
                            RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::Ready,
                        .Plan = std::move(plan),
                    };
                },
            .ReadyFrame = [] { return 2u; },
            .MaxSubmissionsPerFrame = 1u,
            .MaxBindingsPerDrain = 1u,
        });

    const Assets::AssetId generated{96u, 1u};
    const Runtime::RuntimeObjectSpaceNormalBakeRequest request =
        MakeRequest(17u, 1u, generated);
    ASSERT_TRUE(fx.Queue.Queue()
                    .Schedule(request, /*graphicsBackendOperational=*/true)
                    .Succeeded());

    fx.Queue.RecordFrameCommands(commandContext);

    EXPECT_EQ(planCalls, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordFailures, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().CacheRejected, 0u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 0u);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);
    EXPECT_FALSE(fx.Queue.HasInFlightWork());
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::Failed);

    // Retrying is an explicit scheduling decision. The failed command is not
    // silently requeued, and the retired cache slot cannot turn this retry into
    // ResourceBusy -> CacheRejected -> requeue forever.
    ASSERT_TRUE(fx.Queue.Queue()
                    .Schedule(request, /*graphicsBackendOperational=*/true)
                    .Succeeded());
    fx.Queue.RecordFrameCommands(commandContext);

    EXPECT_EQ(planCalls, 2u);
    EXPECT_EQ(fx.Queue.Diagnostics().CacheRejected, 0u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 1u);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);
    EXPECT_TRUE(fx.Queue.HasInFlightWork());
    EXPECT_EQ(commandContext.DrawIndexedCalls, 1);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);

    fx.GpuAssets.Tick(2u, fx.Device.GetFramesInFlight());
    ASSERT_EQ(fx.Queue.DrainCompletedTransfers(), 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().BoundCompletions, 1u);
    EXPECT_FALSE(fx.Queue.HasInFlightWork());
}

TEST(RuntimeObjectSpaceNormalBakeGpuQueue,
     ReadyFrameFailureMarksSlotFailedAndExplicitRetrySucceeds)
{
    GpuQueueFixture fx;
    Extrinsic::Tests::MockCommandContext commandContext;
    const Assets::AssetId generated{97u, 1u};
    bool forceReadyFrameFailure = true;
    fx.Queue.SetDependencies(
        Runtime::RuntimeObjectSpaceNormalBakeGpuQueueDependencies{
            .GpuAssets = &fx.GpuAssets,
            .RenderExtraction = &fx.Extraction,
            .BuildPlan =
                [](const Runtime::RuntimeObjectSpaceNormalBakeSubmission&
                       submission)
                {
                    Graphics::ObjectSpaceNormalTextureBakePlan plan =
                        MakePlan(submission);
                    return Runtime::RuntimeObjectSpaceNormalBakeGpuQueuePlanResult{
                        .Status = plan.Succeeded()
                            ? Runtime::
                                  RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::
                                      Ready
                            : Runtime::
                                  RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::
                                      Invalid,
                        .Plan = std::move(plan),
                    };
                },
            .ReadyFrame =
                [&fx, generated, &forceReadyFrameFailure]
                {
                    if (forceReadyFrameFailure)
                    {
                        forceReadyFrameFailure = false;
                        // Model the asset disappearing between command
                        // recording and ready-frame publication.
                        fx.GpuAssets.NotifyDestroyed(generated);
                    }
                    return 2u;
                },
            .MaxSubmissionsPerFrame = 1u,
            .MaxBindingsPerDrain = 1u,
        });

    const Runtime::RuntimeObjectSpaceNormalBakeRequest request =
        MakeRequest(23u, 1u, generated);
    ASSERT_TRUE(fx.Queue.Queue()
                    .Schedule(request, /*graphicsBackendOperational=*/true)
                    .Succeeded());
    fx.Queue.RecordFrameCommands(commandContext);

    EXPECT_EQ(fx.Queue.Diagnostics().ReadyFrameFailures, 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 0u);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);
    EXPECT_FALSE(fx.Queue.HasInFlightWork());
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::Failed);

    ASSERT_TRUE(fx.Queue.Queue()
                    .Schedule(request, /*graphicsBackendOperational=*/true)
                    .Succeeded());
    fx.Queue.RecordFrameCommands(commandContext);

    EXPECT_EQ(fx.Queue.Diagnostics().CacheRejected, 0u);
    EXPECT_EQ(fx.Queue.Diagnostics().RecordedSubmissions, 1u);
    EXPECT_EQ(fx.Queue.Queue().PendingSubmissionCount(), 0u);
    EXPECT_TRUE(fx.Queue.HasInFlightWork());
    EXPECT_EQ(commandContext.DrawIndexedCalls, 2);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);

    fx.GpuAssets.Tick(2u, fx.Device.GetFramesInFlight());
    ASSERT_EQ(fx.Queue.DrainCompletedTransfers(), 1u);
    EXPECT_EQ(fx.Queue.Diagnostics().BoundCompletions, 1u);
    EXPECT_FALSE(fx.Queue.HasInFlightWork());
}
