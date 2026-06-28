#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

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
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    struct BindingFixture
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

    [[nodiscard]] RHI::TextureDesc ProducedTextureDesc()
    {
        return RHI::TextureDesc{
            .Width = 64u,
            .Height = 64u,
            .MipLevels = 1u,
            .Fmt = RHI::Format::RGBA8_UNORM,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::ColorTarget,
            .DebugName = "runtime-object-space-normal-bake-test",
        };
    }

    [[nodiscard]] RHI::SamplerDesc ProducedTextureSamplerDesc()
    {
        return RHI::SamplerDesc{
            .DebugName = "runtime-object-space-normal-bake-test-sampler",
        };
    }

    void BeginReadyProducedTexture(BindingFixture& fx,
                                   const Assets::AssetId asset,
                                   const std::uint64_t readyFrame)
    {
        auto pending = fx.GpuAssets.BeginGpuProducedTexture(
            Graphics::GpuProducedTextureRequest{
                .Id = asset,
                .Desc = ProducedTextureDesc(),
                .SamplerDesc = ProducedTextureSamplerDesc(),
                .ReadyFrame = readyFrame,
                .HasReadyFrame = true,
            });
        ASSERT_TRUE(pending.has_value()) << static_cast<int>(pending.error());
        fx.GpuAssets.Tick(readyFrame, fx.Device.GetFramesInFlight());
        ASSERT_EQ(fx.GpuAssets.GetState(asset), Graphics::GpuAssetState::Ready);
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeContentKey MakeContentKey(
        const std::uint64_t offset = 0u) noexcept
    {
        return Runtime::RuntimeObjectSpaceNormalBakeContentKey{
            .GeometryKey = 0x1000u + offset,
            .TexcoordKey = 0x2000u + offset,
            .NormalKey = 0x3000u + offset,
            .VertexCount = 3u,
            .IndexCount = 3u,
        };
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeRequest MakeRequest(
        const std::uint64_t entityKey = 17u,
        const std::uint64_t entityGeneration = 3u,
        const Assets::AssetId generated = Assets::AssetId{40u, 1u})
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
                .Width = 128u,
                .Height = 256u,
                .PaddingTexels = 4u,
            },
            .ContentKey = MakeContentKey(),
            .HasStableContentKey = true,
        };
    }
}

TEST(RuntimeObjectSpaceNormalBakeQueue, ReusesGeneratedAssetForStableContentKey)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;

    const auto first = queue.Schedule(
        MakeRequest(17u, 1u, Assets::AssetId{40u, 1u}),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(first.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(first.Status);
    EXPECT_EQ(first.Status, Runtime::RuntimeObjectSpaceNormalBakeStatus::Queued);
    EXPECT_EQ(first.Submission.GeneratedTextureAsset, (Assets::AssetId{40u, 1u}));
    EXPECT_EQ(first.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::ContentKeyInserted);
    EXPECT_EQ(first.Submission.StaleKey.EntityGeneration, 1u);
    EXPECT_EQ(first.Submission.StaleKey.Bake.GeneratedTextureAsset,
              (Assets::AssetId{40u, 1u}));
    EXPECT_EQ(first.Submission.StaleKey.Bake.Source.EntityKey, 17u);
    EXPECT_EQ(first.Submission.StaleKey.Bake.Width, 128u);
    EXPECT_EQ(first.Submission.StaleKey.Bake.Height, 256u);
    EXPECT_EQ(first.Submission.StaleKey.Bake.PaddingTexels, 4u);
    EXPECT_EQ(first.Submission.StaleKey.Bake.Space,
              Graphics::NormalTextureSpace::ObjectSpaceNormal);

    const auto second = queue.Schedule(
        MakeRequest(23u, 1u, Assets::AssetId{99u, 1u}),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(second.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(second.Status);
    EXPECT_EQ(second.Submission.GeneratedTextureAsset, (Assets::AssetId{40u, 1u}));
    EXPECT_EQ(second.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::ContentKeyReuse);
    EXPECT_EQ(second.Submission.StaleKey.Bake.Source.EntityKey, 23u);
    EXPECT_EQ(queue.CachedContentKeyCount(), 1u);
    EXPECT_EQ(queue.PendingCount(), 2u);
    EXPECT_EQ(queue.Diagnostics().QueuedRequests, 2u);
    EXPECT_EQ(queue.Diagnostics().ContentKeyReuses, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue, NonOperationalBackendNoOpsWithoutCpuFallback)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;

    const auto result = queue.Schedule(
        MakeRequest(),
        /*graphicsBackendOperational=*/false);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend);
    EXPECT_NE(result.Diagnostic.find("no CPU fallback"), std::string::npos);
    EXPECT_EQ(queue.PendingCount(), 0u);
    EXPECT_EQ(queue.CachedContentKeyCount(), 0u);
    EXPECT_EQ(queue.Diagnostics().NonOperationalNoOps, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue, DiscardsStaleCompletionBeforeBinding)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;

    const auto oldRequest = MakeRequest(17u, 1u, Assets::AssetId{40u, 1u});
    const auto oldJob = queue.Schedule(oldRequest, /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(oldJob.Succeeded());

    auto replacementRequest = MakeRequest(17u, 2u, Assets::AssetId{41u, 1u});
    replacementRequest.SourceKey.GeometryGeneration = 21u;
    replacementRequest.ContentKey = MakeContentKey(1u);
    const auto replacementJob = queue.Schedule(
        replacementRequest,
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(replacementJob.Succeeded());

    const auto stale = queue.Complete(oldJob.Submission.StaleKey);
    EXPECT_FALSE(stale.Succeeded());
    EXPECT_EQ(stale.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::StaleCompletion);
    EXPECT_EQ(queue.PendingCount(), 1u);
    EXPECT_EQ(queue.Diagnostics().StaleCompletions, 1u);

    const auto ready = queue.Complete(replacementJob.Submission.StaleKey);
    ASSERT_TRUE(ready.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(ready.Status);
    EXPECT_EQ(ready.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding);
    EXPECT_EQ(ready.Submission.GeneratedTextureAsset, (Assets::AssetId{41u, 1u}));
    EXPECT_EQ(queue.PendingCount(), 0u);
    EXPECT_EQ(queue.Diagnostics().ReadyCompletions, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue, RejectsInvalidAndUnsupportedRequests)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;

    auto request = MakeRequest();
    request.SourceKey.EntityKey = 0u;
    auto result = queue.Schedule(request, /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status, Runtime::RuntimeObjectSpaceNormalBakeStatus::InvalidRequest);

    request = MakeRequest();
    request.Options.Space = Graphics::NormalTextureSpace::TangentSpaceNormal;
    result = queue.Schedule(request, /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::
                  UnsupportedNormalTextureSpace);

    request = MakeRequest();
    request.EntityScopedGeneratedTextureAsset = {};
    request.HasStableContentKey = false;
    result = queue.Schedule(request, /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::
                  MissingGeneratedTextureAsset);

    EXPECT_EQ(queue.PendingCount(), 0u);
    EXPECT_EQ(queue.Diagnostics().InvalidRequests, 3u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue, BindingWaitsForGpuCacheReadiness)
{
    BindingFixture fx;
    const Assets::AssetId generated{70u, 1u};
    const auto scheduled = fx.Queue.Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(scheduled.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(scheduled.Status);

    auto pending = fx.GpuAssets.BeginGpuProducedTexture(
        Graphics::GpuProducedTextureRequest{
            .Id = generated,
            .Desc = ProducedTextureDesc(),
            .SamplerDesc = ProducedTextureSamplerDesc(),
            .ReadyFrame = 9u,
            .HasReadyFrame = true,
        });
    ASSERT_TRUE(pending.has_value()) << static_cast<int>(pending.error());

    const auto waiting = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission.StaleKey);

    EXPECT_FALSE(waiting.Succeeded());
    EXPECT_EQ(waiting.Status,
              Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
                  WaitingForGpuTexture);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
    EXPECT_FALSE(fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value());
}

TEST(RuntimeObjectSpaceNormalBakeQueue, BindingInstallsReadyObjectSpaceNormalTexture)
{
    BindingFixture fx;
    const Assets::AssetId generated{71u, 1u};
    const auto scheduled = fx.Queue.Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(scheduled.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(scheduled.Status);
    BeginReadyProducedTexture(fx, generated, 1u);

    const auto bound = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        scheduled.Submission.StaleKey);

    ASSERT_TRUE(bound.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(bound.Status);
    EXPECT_EQ(bound.BoundNormalTexture, generated);
    EXPECT_EQ(bound.Completion.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding);
    EXPECT_EQ(fx.Queue.PendingCount(), 0u);
    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        fx.Extraction.GetMaterialTextureAssetBindings(17u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, generated);
    EXPECT_EQ(bindings->NormalSpace,
              Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     BindingRejectsMismatchedStableEntityWithoutMaterialMutation)
{
    BindingFixture fx;
    const Assets::AssetId generated{75u, 1u};
    const auto scheduled = fx.Queue.Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(scheduled.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(scheduled.Status);
    BeginReadyProducedTexture(fx, generated, 1u);

    const auto mismatch = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/23u,
        scheduled.Submission.StaleKey);

    EXPECT_FALSE(mismatch.Succeeded());
    EXPECT_EQ(mismatch.Status,
              Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity);
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);
    EXPECT_FALSE(fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value());
    EXPECT_FALSE(fx.Extraction.GetMaterialTextureAssetBindings(23u).has_value());
}

TEST(RuntimeObjectSpaceNormalBakeQueue, BindingReusesReadyContentKeyTexture)
{
    BindingFixture fx;
    const Assets::AssetId generated{72u, 1u};
    const auto first = fx.Queue.Schedule(
        MakeRequest(17u, 1u, generated),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(first.Succeeded());
    BeginReadyProducedTexture(fx, generated, 1u);
    ASSERT_TRUE(Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        first.Submission.StaleKey).Succeeded());

    const auto reuse = fx.Queue.Schedule(
        MakeRequest(23u, 1u, Assets::AssetId{99u, 1u}),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(reuse.Succeeded());
    EXPECT_EQ(reuse.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::ContentKeyReuse);
    EXPECT_EQ(reuse.Submission.GeneratedTextureAsset, generated);

    const auto bound = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/23u,
        reuse.Submission.StaleKey);

    ASSERT_TRUE(bound.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(bound.Status);
    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        fx.Extraction.GetMaterialTextureAssetBindings(23u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, generated);
    EXPECT_EQ(fx.Queue.Diagnostics().ContentKeyReuses, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue, BindingRejectsStaleCompletionWithoutMaterialMutation)
{
    BindingFixture fx;

    const auto oldJob = fx.Queue.Schedule(
        MakeRequest(17u, 1u, Assets::AssetId{73u, 1u}),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(oldJob.Succeeded());
    BeginReadyProducedTexture(fx, oldJob.Submission.GeneratedTextureAsset, 1u);

    auto replacementRequest = MakeRequest(17u, 2u, Assets::AssetId{74u, 1u});
    replacementRequest.SourceKey.GeometryGeneration = 22u;
    replacementRequest.ContentKey = MakeContentKey(2u);
    const auto replacementJob = fx.Queue.Schedule(
        replacementRequest,
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(replacementJob.Succeeded());
    BeginReadyProducedTexture(fx, replacementJob.Submission.GeneratedTextureAsset, 2u);

    const auto stale = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        oldJob.Submission.StaleKey);

    EXPECT_FALSE(stale.Succeeded());
    EXPECT_EQ(stale.Status,
              Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion);
    EXPECT_FALSE(fx.Extraction.GetMaterialTextureAssetBindings(17u).has_value());
    EXPECT_EQ(fx.Queue.PendingCount(), 1u);

    const auto bound = Runtime::TryBindReadyObjectSpaceNormalBake(
        fx.Queue,
        fx.Extraction,
        fx.GpuAssets,
        /*stableEntityId=*/17u,
        replacementJob.Submission.StaleKey);
    ASSERT_TRUE(bound.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(bound.Status);
    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        fx.Extraction.GetMaterialTextureAssetBindings(17u);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->Normal, replacementJob.Submission.GeneratedTextureAsset);
}
