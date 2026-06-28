#include <gtest/gtest.h>

#include <cstdint>
#include <string>

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace Graphics = Extrinsic::Graphics;
    namespace Runtime = Extrinsic::Runtime;

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
