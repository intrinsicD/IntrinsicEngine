#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
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
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.WorldHandle;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace ECS = Extrinsic::ECS;
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    template <typename T, std::size_t N>
    [[nodiscard]] std::span<const std::byte> AsBytes(
        const std::array<T, N>& values) noexcept
    {
        return std::as_bytes(std::span<const T>{values});
    }

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
        Runtime::RuntimeObjectSpaceNormalBakeQueue Queue{};
    };

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeIdentity BuildIdentity(
        const std::uint32_t paddingTexels = 0u)
    {
        const std::array<float, 9u> positions{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
        };
        const std::array<std::uint32_t, 3u> indices{0u, 1u, 2u};
        const std::array<float, 6u> texcoords{
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
        };
        const std::array<float, 9u> normals{
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
        };
        auto result = Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(
            Runtime::RuntimeObjectSpaceNormalBakeIdentityInput{
                .PackedPositionBytes = AsBytes(positions),
                .SurfaceIndexBytes = AsBytes(indices),
                .ResolvedTexcoordBytes = AsBytes(texcoords),
                .ResolvedNormalBytes = AsBytes(normals),
                .VertexCount = 3u,
                .SurfaceIndexCount = 3u,
                .Options =
                    Graphics::ObjectSpaceNormalTextureBakeOptions{
                        .Width = 64u,
                        .Height = 32u,
                        .PaddingTexels = paddingTexels,
                        .Space =
                            Graphics::NormalTextureSpace::ObjectSpaceNormal,
                    },
            });
        EXPECT_TRUE(result.Succeeded()) << result.Diagnostic;
        if (!result.Identity.has_value())
            return {};
        return std::move(*result.Identity);
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeRequest MakeRequest(
        const std::uint32_t paddingTexels = 0u)
    {
        return Runtime::RuntimeObjectSpaceNormalBakeRequest{
            .Identity = BuildIdentity(paddingTexels),
            .Target =
                Runtime::RuntimeObjectSpaceNormalBakeTarget{
                    .World = Runtime::DefaultWorldHandle,
                    .BindingEpoch = 3u,
                    .Entity = static_cast<ECS::EntityHandle>(117u),
                    .StableEntityId = 17u,
                    .PresentationKey = "submission-normal",
                    .Semantic = Runtime::ProgressiveSlotSemantic::Normal,
                    .ExpectedProgressiveBindingGeneration = 4u,
                },
        };
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeSubmission
    TakeServiceAllocatedSubmission(
        SubmissionFixture& fx,
        const Assets::AssetId generated,
        const std::uint32_t paddingTexels = 0u)
    {
        const auto scheduled = fx.Queue.Schedule(
            MakeRequest(paddingTexels),
            /*graphicsBackendOperational=*/true);
        EXPECT_TRUE(scheduled.Succeeded())
            << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(
                   scheduled.Status);
        auto pending = fx.Queue.TakePendingSubmissions(1u);
        EXPECT_EQ(pending.size(), 1u);
        if (pending.empty())
            return {};

        Runtime::RuntimeObjectSpaceNormalBakeSubmission submission =
            std::move(pending.front());
        submission.GeneratedTextureAsset = generated;
        submission.AssetSelection =
            Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::
                IdentityInserted;
        return submission;
    }

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakeOptions
    OptionsFromIdentity(
        const Runtime::RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
    {
        return Graphics::ObjectSpaceNormalTextureBakeOptions{
            .Width = identity.Width,
            .Height = identity.Height,
            .PaddingTexels = identity.PaddingTexels,
            .AtlasUvEpsilon =
                std::bit_cast<float>(identity.AtlasUvEpsilonBits),
            .DegenerateUvAreaEpsilon =
                std::bit_cast<float>(
                    identity.DegenerateUvAreaEpsilonBits),
            .DegenerateNormalLengthEpsilon =
                std::bit_cast<float>(
                    identity.DegenerateNormalLengthEpsilonBits),
            .Space = identity.Space,
        };
    }

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakePlan MakePlan(
        const Runtime::RuntimeObjectSpaceNormalBakeSubmission& submission)
    {
        if (!submission.Identity.has_value())
            return {};
        const auto completion =
            Runtime::MakeObjectSpaceNormalBakeGraphicsCompletionKey(
                submission);
        return Graphics::BuildObjectSpaceNormalTextureBakePlan(
            Graphics::ObjectSpaceNormalTextureBakePlanRequest{
                .GeneratedTextureAsset =
                    submission.GeneratedTextureAsset,
                .Geometry =
                    Graphics::ObjectSpaceNormalTextureBakeGeometryBuffers{
                        .IndexBuffer = RHI::BufferHandle{40u, 1u},
                        .TexcoordBDA = 0x1000u,
                        .NormalBDA = 0x2000u,
                        .VertexCount =
                            submission.Identity->VertexCount,
                        .FirstIndex = 7u,
                        .IndexCount =
                            submission.Identity->SurfaceIndexCount,
                    },
                .Options = OptionsFromIdentity(*submission.Identity),
                .SourceKey = completion.Source,
                .Pipeline = RHI::PipelineHandle{5u, 1u},
                .DebugName =
                    "runtime-object-space-normal-bake-submission",
            });
    }

    [[nodiscard]] Graphics::GpuGeometryResidencyView MakeResidency(
        const Runtime::RuntimeObjectSpaceNormalBakeIdentity& identity)
    {
        return Graphics::GpuGeometryResidencyView{
            .Record =
                RHI::GpuGeometryRecord{
                    .TexcoordBufferBDA = 0x1000u,
                    .NormalBufferBDA = 0x2000u,
                    .VertexCount = identity.VertexCount,
                    .SurfaceFirstIndex = 7u,
                    .SurfaceIndexCount =
                        identity.SurfaceIndexCount,
                },
            .IndexBuffer = RHI::BufferHandle{40u, 1u},
            .ContentRevision = 91u,
            .PositionFingerprint = identity.PositionFingerprint,
            .SurfaceIndexFingerprint =
                identity.SurfaceIndexFingerprint,
            .TexcoordFingerprint = identity.TexcoordFingerprint,
            .NormalFingerprint = identity.NormalFingerprint,
            .PositionByteCount = identity.PackedPositionBytes.size(),
            .SurfaceIndexByteCount =
                identity.SurfaceIndexBytes.size(),
            .TexcoordByteCount =
                identity.ResolvedTexcoordBytes.size(),
            .NormalByteCount = identity.ResolvedNormalBytes.size(),
            .VertexCount = identity.VertexCount,
            .SurfaceIndexCount = identity.SurfaceIndexCount,
            .StorageLane =
                Graphics::GpuWorld::GeometryStorageLane::UniformSoA,
            .PositionFormat = RHI::Format::RGB32_FLOAT,
            .TexcoordFormat = RHI::Format::RG32_FLOAT,
            .NormalFormat = RHI::Format::RGB32_FLOAT,
            .SurfaceIndexFormat = RHI::Format::R32_UINT,
            .PositionElementBytes = 12u,
            .PositionStrideBytes = 12u,
            .TexcoordElementBytes = 8u,
            .TexcoordStrideBytes = 8u,
            .NormalElementBytes = 12u,
            .NormalStrideBytes = 12u,
            .SurfaceIndexElementBytes = 4u,
            .SurfaceIndexStrideBytes = 4u,
        };
    }
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     CompletionKeyDerivesExactIdentityGenerationsAndTarget)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{80u, 1u};
    const auto submission =
        TakeServiceAllocatedSubmission(fx, generated);
    ASSERT_TRUE(submission.Identity.has_value());

    const auto key =
        Runtime::MakeObjectSpaceNormalBakeGraphicsCompletionKey(
            submission);
    EXPECT_EQ(key.GeneratedTextureAsset, generated);
    EXPECT_EQ(key.Source.EntityKey, submission.Target.StableEntityId);
    EXPECT_EQ(key.Source.GeometryGeneration,
              submission.Identity->GeometryFingerprint);
    EXPECT_EQ(key.Source.TexcoordGeneration,
              submission.Identity->TexcoordFingerprint);
    EXPECT_EQ(key.Source.NormalGeneration,
              submission.Identity->NormalFingerprint);
    EXPECT_EQ(key.Width, submission.Identity->Width);
    EXPECT_EQ(key.Height, submission.Identity->Height);
    EXPECT_EQ(key.PaddingTexels, submission.Identity->PaddingTexels);
    EXPECT_EQ(key.Space, submission.Identity->Space);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     BeginsExactCacheGenerationAndBuildsRecordDesc)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{81u, 1u};
    const auto submission =
        TakeServiceAllocatedSubmission(fx, generated);
    const auto plan = MakePlan(submission);
    ASSERT_TRUE(plan.Succeeded())
        << Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(
               plan.Status);

    const auto submitted =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            submission,
            plan,
            /*geometryContentRevision=*/91u);

    ASSERT_TRUE(submitted.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeGpuSubmitStatus(
               submitted.Status);
    EXPECT_EQ(submitted.Ticket.StableEntityId, 17u);
    EXPECT_EQ(submitted.Ticket.Completion.RequestGeneration,
              submission.StaleKey.RequestGeneration);
    EXPECT_EQ(submitted.Ticket.Identity, *submission.Identity);
    EXPECT_EQ(submitted.Ticket.GeneratedTextureAsset, generated);
    EXPECT_EQ(submitted.Ticket.RecordDesc.Pipeline,
              plan.RecordTemplate.Pipeline);
    EXPECT_EQ(submitted.Ticket.RecordDesc.IndexBuffer,
              plan.RecordTemplate.IndexBuffer);
    EXPECT_EQ(submitted.Ticket.RecordDesc.TexcoordBDA,
              plan.RecordTemplate.TexcoordBDA);
    EXPECT_EQ(submitted.Ticket.RecordDesc.NormalBDA,
              plan.RecordTemplate.NormalBDA);
    EXPECT_EQ(submitted.Ticket.RecordDesc.FirstIndex, 7u);
    EXPECT_EQ(submitted.Ticket.RecordDesc.IndexCount, 3u);
    EXPECT_EQ(submitted.Ticket.RecordDesc.Width, 64u);
    EXPECT_EQ(submitted.Ticket.RecordDesc.Height, 32u);
    EXPECT_TRUE(submitted.Ticket.RecordDesc.OutputTexture.IsValid());
    EXPECT_GT(submitted.Ticket.CacheGeneration, 0u);
    EXPECT_EQ(submitted.Ticket.GeometryContentRevision, 91u);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);
    EXPECT_FALSE(fx.GpuAssets.GetView(generated).has_value());
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
    EXPECT_EQ(fx.Queue.PendingSubmissionCount(), 0u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     ReadyPublicationRequiresExactPendingCacheGeneration)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{82u, 1u};
    const auto submission =
        TakeServiceAllocatedSubmission(fx, generated);
    const auto plan = MakePlan(submission);
    ASSERT_TRUE(plan.Succeeded());
    const auto submitted =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            submission,
            plan,
            /*geometryContentRevision=*/91u);
    ASSERT_TRUE(submitted.Succeeded());

    auto wrongGeneration = submitted.Ticket;
    ++wrongGeneration.CacheGeneration;
    const Core::Result rejected =
        Runtime::MarkObjectSpaceNormalBakeGpuSubmissionReady(
            fx.GpuAssets,
            wrongGeneration,
            /*readyFrame=*/7u);
    EXPECT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), Core::ErrorCode::InvalidState);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);

    ASSERT_TRUE(Runtime::MarkObjectSpaceNormalBakeGpuSubmissionReady(
        fx.GpuAssets,
        submitted.Ticket,
        /*readyFrame=*/7u).has_value());
    fx.GpuAssets.Tick(6u, fx.Device.GetFramesInFlight());
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::GpuUploading);
    fx.GpuAssets.Tick(7u, fx.Device.GetFramesInFlight());
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::Ready);
    const auto view = fx.GpuAssets.GetView(generated);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->Generation, submitted.Ticket.CacheGeneration);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsStalePlanBeforeCacheAllocation)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{83u, 1u};
    const auto submission =
        TakeServiceAllocatedSubmission(fx, generated);
    auto plan = MakePlan(submission);
    ASSERT_TRUE(plan.Succeeded());
    ++plan.CompletionKey.Source.NormalGeneration;

    const auto rejected =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            submission,
            plan,
            /*geometryContentRevision=*/91u);

    EXPECT_FALSE(rejected.Succeeded());
    EXPECT_EQ(rejected.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                  StalePlan);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::NotRequested);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsUnallocatedOrInvalidQueueSubmission)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{84u, 1u};
    const auto allocated =
        TakeServiceAllocatedSubmission(fx, generated);
    const auto plan = MakePlan(allocated);
    ASSERT_TRUE(plan.Succeeded());

    auto unallocated = allocated;
    unallocated.GeneratedTextureAsset = {};
    const auto missingAsset =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            unallocated,
            plan,
            /*geometryContentRevision=*/91u);
    EXPECT_EQ(missingAsset.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                  InvalidQueueSubmission);

    auto invalidTarget = allocated;
    invalidTarget.Target.StableEntityId = 0u;
    const auto targetRejected =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            invalidTarget,
            plan,
            /*geometryContentRevision=*/91u);
    EXPECT_EQ(targetRejected.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                  InvalidStableEntity);

    const auto missingRevision =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            allocated,
            plan,
            /*geometryContentRevision=*/0u);
    EXPECT_EQ(missingRevision.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                  InvalidQueueSubmission);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::NotRequested);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsPaddedPlanWithoutDilationBeforeCacheAllocation)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{85u, 1u};
    const auto submission =
        TakeServiceAllocatedSubmission(
            fx,
            generated,
            /*paddingTexels=*/4u);
    const auto plan = MakePlan(submission);
    ASSERT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::
                  DilationUnavailable);

    const auto rejected =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            submission,
            plan,
            /*geometryContentRevision=*/91u);

    EXPECT_EQ(rejected.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                  InvalidPlan);
    EXPECT_EQ(fx.GpuAssets.GetState(generated),
              Graphics::GpuAssetState::NotRequested);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     RejectsCacheBusyWithoutChangingQueueLifetime)
{
    SubmissionFixture fx;
    const Assets::AssetId generated{86u, 1u};
    const auto submission =
        TakeServiceAllocatedSubmission(fx, generated);
    const auto plan = MakePlan(submission);
    ASSERT_TRUE(plan.Succeeded());
    ASSERT_TRUE(Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
        fx.GpuAssets,
        submission,
        plan,
        /*geometryContentRevision=*/91u).Succeeded());

    const auto busy =
        Runtime::BeginObjectSpaceNormalBakeGpuSubmission(
            fx.GpuAssets,
            submission,
            plan,
            /*geometryContentRevision=*/91u);

    EXPECT_FALSE(busy.Succeeded());
    EXPECT_EQ(busy.Status,
              Runtime::RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                  CacheRejected);
    EXPECT_EQ(busy.CacheError, Core::ErrorCode::ResourceBusy);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     ResidencyValidatorAcceptsExactReadableUniformSoA)
{
    const auto identity = BuildIdentity();
    const auto residency = MakeResidency(identity);

    const auto result =
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            residency,
            /*expectedContentRevision=*/91u);

    ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
    EXPECT_EQ(result.Status,
              Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
                  Compatible);
    EXPECT_EQ(residency.Record.SurfaceFirstIndex, 7u);
}

TEST(RuntimeObjectSpaceNormalBakeSubmission,
     ResidencyValidatorRejectsRevisionIdentityLaneAndLayoutMismatches)
{
    const auto identity = BuildIdentity();
    const auto exact = MakeResidency(identity);

    auto changed = exact;
    changed.ContentRevision = 0u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed,
            91u).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidContentRevision);

    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            exact,
            92u).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            StaleContentRevision);

    changed = exact;
    changed.SurfaceIndexFingerprint ^= 1u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            IdentityMismatch);

    changed = exact;
    changed.StorageLane =
        Graphics::GpuWorld::GeometryStorageLane::StaticInterleavedAoS;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            UnsupportedStorageLane);

    changed = exact;
    changed.PositionStrideBytes = 16u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidPositionLayout);

    changed = exact;
    changed.TexcoordFormat = RHI::Format::Undefined;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidTexcoordLayout);

    changed = exact;
    changed.Record.TexcoordBufferBDA += 4u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidTexcoordLayout);

    changed = exact;
    changed.NormalElementBytes = 16u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidNormalLayout);

    changed = exact;
    changed.Record.NormalBufferBDA += 2u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidNormalLayout);

    changed = exact;
    changed.SurfaceIndexStrideBytes = 2u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidSurfaceIndexLayout);

    changed = exact;
    changed.IndexBuffer = {};
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            MissingManagedIndexBuffer);

    changed = exact;
    changed.Record.TexcoordBufferBDA = 0u;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            MissingChannelAddress);

    changed = exact;
    --changed.Record.SurfaceIndexCount;
    EXPECT_EQ(
        Runtime::ValidateObjectSpaceNormalBakeResidency(
            identity,
            changed).Status,
        Runtime::RuntimeObjectSpaceNormalBakeResidencyStatus::
            InvalidRecordCounts);
}
