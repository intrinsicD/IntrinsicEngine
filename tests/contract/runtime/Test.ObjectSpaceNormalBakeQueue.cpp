#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Scene.Handle;
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
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.WorldHandle;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace ECS = Extrinsic::ECS;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    template <typename T, std::size_t N>
    [[nodiscard]] std::span<const std::byte> AsBytes(
        const std::array<T, N>& values) noexcept
    {
        return std::as_bytes(std::span<const T>{values});
    }

    void FnvUpdateU32(std::uint64_t& fingerprint,
                      const std::uint32_t word) noexcept
    {
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
        {
            fingerprint ^=
                static_cast<std::uint8_t>((word >> shift) & 0xffu);
            fingerprint *= kFnvPrime;
        }
    }

    template <std::size_t N>
    [[nodiscard]] std::uint64_t FingerprintFloats(
        const std::array<float, N>& values) noexcept
    {
        std::uint64_t fingerprint = 14695981039346656037ull;
        for (const float value : values)
        {
            const float canonical = value == 0.0f ? 0.0f : value;
            FnvUpdateU32(
                fingerprint,
                std::bit_cast<std::uint32_t>(canonical));
        }
        return fingerprint == 0u ? 1u : fingerprint;
    }

    template <std::size_t N>
    [[nodiscard]] std::uint64_t FingerprintIndices(
        const std::array<std::uint32_t, N>& values) noexcept
    {
        std::uint64_t fingerprint = 14695981039346656037ull;
        for (const std::uint32_t value : values)
            FnvUpdateU32(fingerprint, value);
        return fingerprint == 0u ? 1u : fingerprint;
    }

    struct IdentityInputFixture
    {
        std::array<float, 9u> Positions{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
        };
        std::array<std::uint32_t, 3u> SurfaceIndices{0u, 1u, 2u};
        std::array<float, 6u> Texcoords{
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
        };
        std::array<float, 9u> Normals{
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
        };

        [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeIdentityInput Input(
            const Graphics::ObjectSpaceNormalTextureBakeOptions& options = {})
            const noexcept
        {
            return Runtime::RuntimeObjectSpaceNormalBakeIdentityInput{
                .PackedPositionBytes = AsBytes(Positions),
                .SurfaceIndexBytes = AsBytes(SurfaceIndices),
                .ResolvedTexcoordBytes = AsBytes(Texcoords),
                .ResolvedNormalBytes = AsBytes(Normals),
                .VertexCount = 3u,
                .SurfaceIndexCount = 3u,
                .Options = options,
            };
        }
    };

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakeOptions
    IdentityOptions() noexcept
    {
        return Graphics::ObjectSpaceNormalTextureBakeOptions{
            .Width = 128u,
            .Height = 256u,
            .PaddingTexels = 4u,
            .AtlasUvEpsilon = 1.0e-4f,
            .DegenerateUvAreaEpsilon = 1.0e-10f,
            .DegenerateNormalLengthEpsilon = 1.0e-6f,
            .Space = Graphics::NormalTextureSpace::ObjectSpaceNormal,
        };
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeIdentity BuildIdentity(
        const IdentityInputFixture& fixture,
        const Graphics::ObjectSpaceNormalTextureBakeOptions& options =
            IdentityOptions())
    {
        auto result = Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(
            fixture.Input(options));
        EXPECT_TRUE(result.Succeeded()) << result.Diagnostic;
        if (!result.Identity.has_value())
            return {};
        return std::move(*result.Identity);
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeTarget MakeTarget(
        const std::uint32_t stableEntityId = 17u,
        const std::uint64_t bindingEpoch = 3u,
        const std::uint64_t expectedBindingGeneration = 1u,
        const Runtime::WorldHandle world = Runtime::DefaultWorldHandle)
    {
        return Runtime::RuntimeObjectSpaceNormalBakeTarget{
            .World = world,
            .BindingEpoch = bindingEpoch,
            .Entity = static_cast<ECS::EntityHandle>(stableEntityId + 100u),
            .StableEntityId = stableEntityId,
            .PresentationKey =
                "normal-presentation-" + std::to_string(stableEntityId),
            .Semantic = Runtime::ProgressiveSlotSemantic::Normal,
            .ExpectedProgressiveBindingGeneration =
                expectedBindingGeneration,
        };
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeRequest MakeRequest(
        const IdentityInputFixture& fixture,
        const std::uint32_t stableEntityId = 17u,
        const std::uint64_t bindingEpoch = 3u,
        const std::uint64_t expectedBindingGeneration = 1u)
    {
        return Runtime::RuntimeObjectSpaceNormalBakeRequest{
            .Identity = BuildIdentity(fixture),
            .Target = MakeTarget(
                stableEntityId,
                bindingEpoch,
                expectedBindingGeneration),
        };
    }

    [[nodiscard]] std::size_t IdentityByteCount(
        const Runtime::RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
    {
        return identity.PackedPositionBytes.size() +
               identity.SurfaceIndexBytes.size() +
               identity.ResolvedTexcoordBytes.size() +
               identity.ResolvedNormalBytes.size();
    }
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ExactIdentityOwnsDataAndDoesNotUseFingerprintsForEquality)
{
    IdentityInputFixture fixture{};
    const auto first = BuildIdentity(fixture);
    auto duplicate = BuildIdentity(fixture);
    const Runtime::RuntimeObjectSpaceNormalBakeIdentityHash hash{};

    EXPECT_EQ(first.SchemaVersion,
              Runtime::kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion);
    EXPECT_EQ(first.VertexCount, 3u);
    EXPECT_EQ(first.SurfaceIndexCount, 3u);
    EXPECT_EQ(first, duplicate);
    EXPECT_EQ(hash(first), hash(duplicate));
    EXPECT_EQ(hash(first),
              static_cast<std::size_t>(first.CombinedFingerprint));
    EXPECT_NE(first.PositionFingerprint, 0u);
    EXPECT_NE(first.SurfaceIndexFingerprint, 0u);
    EXPECT_NE(first.GeometryFingerprint, 0u);
    EXPECT_NE(first.TexcoordFingerprint, 0u);
    EXPECT_NE(first.NormalFingerprint, 0u);
    EXPECT_NE(first.CombinedFingerprint, 0u);
    EXPECT_EQ(first.PositionFingerprint,
              FingerprintFloats(fixture.Positions));
    EXPECT_EQ(first.SurfaceIndexFingerprint,
              FingerprintIndices(fixture.SurfaceIndices));
    EXPECT_EQ(first.TexcoordFingerprint,
              FingerprintFloats(fixture.Texcoords));
    EXPECT_EQ(first.NormalFingerprint,
              FingerprintFloats(fixture.Normals));

    duplicate.PositionFingerprint ^= 0x11u;
    duplicate.SurfaceIndexFingerprint ^= 0x22u;
    duplicate.GeometryFingerprint ^= 0x33u;
    duplicate.TexcoordFingerprint ^= 0x44u;
    duplicate.NormalFingerprint ^= 0x55u;
    duplicate.CombinedFingerprint ^= 0x66u;
    EXPECT_EQ(first, duplicate);
    EXPECT_EQ(hash(first), hash(duplicate));

    const auto ownedPositionBytes = first.PackedPositionBytes;
    const auto ownedSurfaceIndexBytes = first.SurfaceIndexBytes;
    const auto ownedTexcoordBytes = first.ResolvedTexcoordBytes;
    const auto ownedNormalBytes = first.ResolvedNormalBytes;
    fixture.Positions[3] = 42.0f;
    fixture.SurfaceIndices[1] = 2u;
    fixture.Texcoords[2] = 0.25f;
    fixture.Normals[2] = 0.5f;
    EXPECT_EQ(first.PackedPositionBytes, ownedPositionBytes);
    EXPECT_EQ(first.SurfaceIndexBytes, ownedSurfaceIndexBytes);
    EXPECT_EQ(first.ResolvedTexcoordBytes, ownedTexcoordBytes);
    EXPECT_EQ(first.ResolvedNormalBytes, ownedNormalBytes);

    auto changedSchema = first;
    ++changedSchema.SchemaVersion;
    EXPECT_NE(first, changedSchema);
    EXPECT_NE(hash(first), hash(changedSchema));

    auto changedVertexCount = first;
    ++changedVertexCount.VertexCount;
    EXPECT_NE(first, changedVertexCount);
    EXPECT_NE(hash(first), hash(changedVertexCount));

    auto changedSurfaceIndexCount = first;
    changedSurfaceIndexCount.SurfaceIndexCount += 3u;
    EXPECT_NE(first, changedSurfaceIndexCount);
    EXPECT_NE(hash(first), hash(changedSurfaceIndexCount));
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ExactIdentityDistinguishesEveryOwnedVertexStream)
{
    const IdentityInputFixture baseFixture{};
    const auto base = BuildIdentity(baseFixture);
    const Runtime::RuntimeObjectSpaceNormalBakeIdentityHash hash{};

    auto changedFixture = baseFixture;
    changedFixture.Positions[3] = 2.0f;
    const auto changedPosition = BuildIdentity(changedFixture);
    EXPECT_NE(base, changedPosition);
    EXPECT_NE(hash(base), hash(changedPosition));
    EXPECT_NE(base.PositionFingerprint,
              changedPosition.PositionFingerprint);
    EXPECT_NE(base.GeometryFingerprint,
              changedPosition.GeometryFingerprint);

    changedFixture = baseFixture;
    changedFixture.Texcoords[2] = 0.75f;
    const auto changedTexcoord = BuildIdentity(changedFixture);
    EXPECT_NE(base, changedTexcoord);
    EXPECT_NE(hash(base), hash(changedTexcoord));
    EXPECT_NE(base.TexcoordFingerprint,
              changedTexcoord.TexcoordFingerprint);

    changedFixture = baseFixture;
    changedFixture.Normals[2] = 0.5f;
    const auto changedNormal = BuildIdentity(changedFixture);
    EXPECT_NE(base, changedNormal);
    EXPECT_NE(hash(base), hash(changedNormal));
    EXPECT_NE(base.NormalFingerprint,
              changedNormal.NormalFingerprint);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ExactIdentityFingerprintsMatchLiveGpuWorldResidency)
{
    IdentityInputFixture fixture{};
    fixture.Positions[0] = -0.0f;
    fixture.Texcoords[0] = -0.0f;
    fixture.Normals[0] = -0.0f;
    const auto identity = BuildIdentity(fixture);

    Extrinsic::Tests::MockDevice device;
    RHI::BufferManager buffers{device};
    Graphics::GpuWorld world;
    Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u << 10;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto geometry = world.UploadGeometry(
        Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = {},
            .PositionBytes = AsBytes(fixture.Positions),
            .TexcoordBytes = AsBytes(fixture.Texcoords),
            .NormalBytes = AsBytes(fixture.Normals),
            .SurfaceIndices =
                std::span<const std::uint32_t>{fixture.SurfaceIndices},
            .LineIndices = {},
            .VertexCount = identity.VertexCount,
            .LocalBounds = {},
            .DebugName = "runtime-normal-bake-identity-parity",
            .PackedVertexColors = {},
        });
    ASSERT_TRUE(geometry.IsValid());

    Graphics::GpuGeometryResidencyView residency{};
    ASSERT_TRUE(world.TryGetGeometryResidencyView(geometry, residency));
    EXPECT_EQ(residency.PositionFingerprint,
              identity.PositionFingerprint);
    EXPECT_EQ(residency.SurfaceIndexFingerprint,
              identity.SurfaceIndexFingerprint);
    EXPECT_EQ(residency.TexcoordFingerprint,
              identity.TexcoordFingerprint);
    EXPECT_EQ(residency.NormalFingerprint,
              identity.NormalFingerprint);
    EXPECT_EQ(residency.VertexCount, identity.VertexCount);
    EXPECT_EQ(residency.SurfaceIndexCount,
              identity.SurfaceIndexCount);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ExactIdentityDistinguishesSurfaceTopologyOrder)
{
    IdentityInputFixture firstFixture{};
    IdentityInputFixture reorderedFixture{};
    reorderedFixture.SurfaceIndices = {0u, 2u, 1u};

    const auto first = BuildIdentity(firstFixture);
    const auto reordered = BuildIdentity(reorderedFixture);
    const Runtime::RuntimeObjectSpaceNormalBakeIdentityHash hash{};

    EXPECT_NE(first, reordered);
    EXPECT_NE(hash(first), hash(reordered));
    EXPECT_EQ(first.PositionFingerprint, reordered.PositionFingerprint);
    EXPECT_NE(first.SurfaceIndexFingerprint,
              reordered.SurfaceIndexFingerprint);
    EXPECT_NE(first.GeometryFingerprint, reordered.GeometryFingerprint);
    EXPECT_NE(first.CombinedFingerprint, reordered.CombinedFingerprint);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ExactIdentityIncludesEveryResolvedBakeOption)
{
    const IdentityInputFixture fixture{};
    const auto baseOptions = IdentityOptions();
    const auto base = BuildIdentity(fixture, baseOptions);
    const Runtime::RuntimeObjectSpaceNormalBakeIdentityHash hash{};

    const auto expectChanged =
        [&](const Graphics::ObjectSpaceNormalTextureBakeOptions& options)
    {
        const auto changed = BuildIdentity(fixture, options);
        EXPECT_NE(base, changed);
        EXPECT_NE(hash(base), hash(changed));
        EXPECT_NE(base.CombinedFingerprint, changed.CombinedFingerprint);
    };

    auto options = baseOptions;
    options.Width = 256u;
    expectChanged(options);
    options = baseOptions;
    options.Height = 512u;
    expectChanged(options);
    options = baseOptions;
    options.PaddingTexels = 8u;
    expectChanged(options);
    options = baseOptions;
    options.AtlasUvEpsilon = 2.0e-4f;
    expectChanged(options);
    options = baseOptions;
    options.DegenerateUvAreaEpsilon = 2.0e-10f;
    expectChanged(options);
    options = baseOptions;
    options.DegenerateNormalLengthEpsilon = 2.0e-6f;
    expectChanged(options);

    auto unresolvedDefaults =
        Graphics::ObjectSpaceNormalTextureBakeOptions{};
    unresolvedDefaults.Width = 0u;
    unresolvedDefaults.Height = 0u;
    const auto resolvedDefaults = BuildIdentity(
        fixture,
        Graphics::ObjectSpaceNormalTextureBakeOptions{
            .Width = Graphics::kObjectSpaceNormalBakeDefaultExtent,
            .Height = Graphics::kObjectSpaceNormalBakeDefaultExtent,
        });
    EXPECT_EQ(BuildIdentity(fixture, unresolvedDefaults),
              resolvedDefaults);

    auto differentSpace = base;
    differentSpace.Space =
        Graphics::NormalTextureSpace::TangentSpaceNormal;
    EXPECT_NE(base, differentSpace);
    EXPECT_NE(hash(base), hash(differentSpace));
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ExactIdentityCanonicalizesNegativeZeroStreamsAndEpsilonBits)
{
    IdentityInputFixture positiveFixture{};
    IdentityInputFixture negativeFixture{};
    negativeFixture.Positions[0] = -0.0f;
    negativeFixture.Texcoords[0] = -0.0f;
    negativeFixture.Normals[0] = -0.0f;
    auto positiveZero = IdentityOptions();
    positiveZero.AtlasUvEpsilon = 0.0f;
    positiveZero.DegenerateUvAreaEpsilon = 0.0f;
    positiveZero.DegenerateNormalLengthEpsilon = 0.0f;
    auto negativeZero = positiveZero;
    negativeZero.AtlasUvEpsilon = -0.0f;
    negativeZero.DegenerateUvAreaEpsilon = -0.0f;
    negativeZero.DegenerateNormalLengthEpsilon = -0.0f;

    const auto positive = BuildIdentity(positiveFixture, positiveZero);
    const auto negative = BuildIdentity(negativeFixture, negativeZero);
    const Runtime::RuntimeObjectSpaceNormalBakeIdentityHash hash{};

    EXPECT_EQ(positive, negative);
    EXPECT_EQ(hash(positive), hash(negative));
    EXPECT_EQ(positive.AtlasUvEpsilonBits,
              std::bit_cast<std::uint32_t>(0.0f));
    EXPECT_EQ(negative.AtlasUvEpsilonBits,
              std::bit_cast<std::uint32_t>(0.0f));
    EXPECT_EQ(positive.PackedPositionBytes,
              negative.PackedPositionBytes);
    EXPECT_EQ(positive.ResolvedTexcoordBytes,
              negative.ResolvedTexcoordBytes);
    EXPECT_EQ(positive.ResolvedNormalBytes,
              negative.ResolvedNormalBytes);
    EXPECT_EQ(positive.PositionFingerprint,
              negative.PositionFingerprint);
    EXPECT_EQ(positive.TexcoordFingerprint,
              negative.TexcoordFingerprint);
    EXPECT_EQ(positive.NormalFingerprint,
              negative.NormalFingerprint);
}

TEST(RuntimeObjectSpaceNormalBakeQueue, ExactIdentityRejectsMalformedInput)
{
    IdentityInputFixture fixture{};

    auto input = fixture.Input(IdentityOptions());
    input.VertexCount = 0u;
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::EmptyInput);

    input = fixture.Input(IdentityOptions());
    input.PackedPositionBytes =
        input.PackedPositionBytes.first(input.PackedPositionBytes.size() - 1u);
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            InvalidPositionByteCount);

    input = fixture.Input(IdentityOptions());
    input.SurfaceIndexCount = 2u;
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            InvalidSurfaceIndexCount);

    input = fixture.Input(IdentityOptions());
    input.SurfaceIndexBytes =
        input.SurfaceIndexBytes.first(input.SurfaceIndexBytes.size() - 1u);
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            InvalidSurfaceIndexByteCount);

    input = fixture.Input(IdentityOptions());
    input.ResolvedTexcoordBytes =
        input.ResolvedTexcoordBytes.first(
            input.ResolvedTexcoordBytes.size() - 1u);
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            InvalidTexcoordByteCount);

    input = fixture.Input(IdentityOptions());
    input.ResolvedNormalBytes =
        input.ResolvedNormalBytes.first(input.ResolvedNormalBytes.size() - 1u);
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            InvalidNormalByteCount);

    fixture.SurfaceIndices[2] = 3u;
    input = fixture.Input(IdentityOptions());
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            InvalidSurfaceIndex);

    fixture.SurfaceIndices[2] = 2u;
    auto tangentOptions = IdentityOptions();
    tangentOptions.Space =
        Graphics::NormalTextureSpace::TangentSpaceNormal;
    input = fixture.Input(tangentOptions);
    EXPECT_EQ(
        Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(input).Status,
        Runtime::RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
            UnsupportedNormalTextureSpace);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     ScheduleCarriesExactRequestWithoutAllocatingAsset)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const IdentityInputFixture fixture{};
    const auto request = MakeRequest(fixture);
    const auto result = queue.Schedule(
        request,
        /*graphicsBackendOperational=*/true);

    ASSERT_TRUE(result.Succeeded())
        << Runtime::DebugNameForRuntimeObjectSpaceNormalBakeStatus(result.Status);
    EXPECT_EQ(result.Status, Runtime::RuntimeObjectSpaceNormalBakeStatus::Queued);
    ASSERT_TRUE(result.Submission.Identity.has_value());
    EXPECT_EQ(*result.Submission.Identity, *request.Identity);
    EXPECT_EQ(result.Submission.Target, request.Target);
    EXPECT_EQ(result.Submission.StaleKey.Target, request.Target);
    EXPECT_GT(result.Submission.StaleKey.RequestGeneration, 0u);
    EXPECT_FALSE(result.Submission.GeneratedTextureAsset.IsValid());
    EXPECT_EQ(result.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::None);
    EXPECT_TRUE(queue.IsLatest(result.Submission.StaleKey));
    EXPECT_EQ(queue.PendingCount(), 1u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 1u);
    EXPECT_EQ(queue.PendingIdentityByteCount(),
              IdentityByteCount(*request.Identity));
    EXPECT_EQ(queue.Diagnostics().QueuedRequests, 1u);
    EXPECT_EQ(queue.Diagnostics().IdentityRequests, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     IdentitylessRequestIsQueuedAsNonReusableWithoutAsset)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const auto request = Runtime::RuntimeObjectSpaceNormalBakeRequest{
        .Identity = std::nullopt,
        .Target = MakeTarget(),
    };

    const auto result = queue.Schedule(
        request,
        /*graphicsBackendOperational=*/true);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_FALSE(result.Submission.Identity.has_value());
    EXPECT_FALSE(result.Submission.GeneratedTextureAsset.IsValid());
    EXPECT_EQ(result.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::None);
    EXPECT_EQ(queue.PendingIdentityByteCount(), 0u);
    EXPECT_EQ(queue.Diagnostics().IdentitylessRequests, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     IdenticalContentForDifferentTargetsDoesNotAllocateOrReuseAsset)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const IdentityInputFixture fixture{};
    const auto first = queue.Schedule(
        MakeRequest(fixture, 17u),
        /*graphicsBackendOperational=*/true);
    const auto second = queue.Schedule(
        MakeRequest(fixture, 23u),
        /*graphicsBackendOperational=*/true);

    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    ASSERT_TRUE(first.Submission.Identity.has_value());
    ASSERT_TRUE(second.Submission.Identity.has_value());
    EXPECT_EQ(*first.Submission.Identity, *second.Submission.Identity);
    EXPECT_FALSE(first.Submission.GeneratedTextureAsset.IsValid());
    EXPECT_FALSE(second.Submission.GeneratedTextureAsset.IsValid());
    EXPECT_EQ(first.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::None);
    EXPECT_EQ(second.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::None);
    EXPECT_EQ(queue.PendingCount(), 2u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 2u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     SupersedingTargetInvalidatesOldGenerationAndCompletesWithAllocatedAsset)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    IdentityInputFixture fixture{};
    const auto first = queue.Schedule(
        MakeRequest(fixture, 17u, 3u, 1u),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(first.Succeeded());

    fixture.Normals[2] = 0.5f;
    const auto replacement = queue.Schedule(
        MakeRequest(fixture, 17u, 3u, 2u),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(replacement.Succeeded());

    EXPECT_FALSE(queue.IsLatest(first.Submission.StaleKey));
    EXPECT_TRUE(queue.IsLatest(replacement.Submission.StaleKey));
    EXPECT_EQ(queue.PendingCount(), 1u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 1u);
    EXPECT_EQ(queue.Diagnostics().SupersededQueuedRequests, 1u);
    EXPECT_EQ(
        queue.Complete(
            first.Submission.StaleKey,
            Assets::AssetId{40u, 1u}).Status,
        Runtime::RuntimeObjectSpaceNormalBakeStatus::StaleCompletion);

    const Assets::AssetId generated{41u, 1u};
    const auto completed = queue.Complete(
        replacement.Submission.StaleKey,
        generated,
        Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::IdentityInserted);
    ASSERT_TRUE(completed.Succeeded());
    EXPECT_EQ(completed.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding);
    EXPECT_EQ(completed.Submission.Target, replacement.Submission.Target);
    EXPECT_EQ(completed.Submission.GeneratedTextureAsset, generated);
    EXPECT_EQ(completed.Submission.AssetSelection,
              Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::
                  IdentityInserted);
    EXPECT_EQ(queue.PendingCount(), 0u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 0u);
    EXPECT_EQ(queue.PendingIdentityByteCount(), 0u);
    EXPECT_EQ(queue.Diagnostics().ReadyCompletions, 1u);
    EXPECT_EQ(queue.Diagnostics().StaleCompletions, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     PendingDrainAndRequeuePreserveLatestRequest)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const IdentityInputFixture fixture{};
    const auto first = queue.Schedule(
        MakeRequest(fixture, 17u),
        /*graphicsBackendOperational=*/true);
    const auto second = queue.Schedule(
        MakeRequest(fixture, 23u),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    const std::size_t identityBytes =
        IdentityByteCount(*first.Submission.Identity);

    auto taken = queue.TakePendingSubmissions(1u);
    ASSERT_EQ(taken.size(), 1u);
    EXPECT_TRUE(Runtime::RuntimeObjectSpaceNormalBakeStaleKeyMatches(
        taken.front().StaleKey,
        first.Submission.StaleKey));
    EXPECT_FALSE(taken.front().GeneratedTextureAsset.IsValid());
    EXPECT_EQ(queue.PendingSubmissionCount(), 1u);
    EXPECT_EQ(queue.PendingCount(), 2u);
    EXPECT_EQ(queue.PendingIdentityByteCount(), identityBytes);

    queue.RequeuePendingSubmission(std::move(taken.front()));
    EXPECT_EQ(queue.PendingSubmissionCount(), 2u);
    EXPECT_EQ(queue.PendingIdentityByteCount(), identityBytes * 2u);
    EXPECT_TRUE(queue.Discard(first.Submission.StaleKey));
    EXPECT_FALSE(queue.IsLatest(first.Submission.StaleKey));
    EXPECT_TRUE(queue.IsLatest(second.Submission.StaleKey));
    EXPECT_EQ(queue.PendingCount(), 1u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     DetachTargetsRemovesOnlyMatchingWorldEpoch)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const IdentityInputFixture fixture{};
    const auto first = queue.Schedule(
        MakeRequest(fixture, 17u, 7u),
        /*graphicsBackendOperational=*/true);
    const auto second = queue.Schedule(
        MakeRequest(fixture, 23u, 7u),
        /*graphicsBackendOperational=*/true);
    const auto otherEpoch = queue.Schedule(
        MakeRequest(fixture, 31u, 8u),
        /*graphicsBackendOperational=*/true);
    auto otherWorldRequest = MakeRequest(fixture, 47u, 7u);
    otherWorldRequest.Target.World = Runtime::WorldHandle{9u, 1u};
    const auto otherWorld = queue.Schedule(
        otherWorldRequest,
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    ASSERT_TRUE(otherEpoch.Succeeded());
    ASSERT_TRUE(otherWorld.Succeeded());

    EXPECT_EQ(queue.DetachTargets(Runtime::DefaultWorldHandle, 7u), 2u);
    EXPECT_FALSE(queue.IsLatest(first.Submission.StaleKey));
    EXPECT_FALSE(queue.IsLatest(second.Submission.StaleKey));
    EXPECT_TRUE(queue.IsLatest(otherEpoch.Submission.StaleKey));
    EXPECT_TRUE(queue.IsLatest(otherWorld.Submission.StaleKey));
    EXPECT_EQ(queue.PendingCount(), 2u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 2u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     TargetCapacityFailsDeterministicallyWithoutEvictingQueuedWork)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    for (std::size_t index = 0u;
         index < Runtime::kRuntimeObjectSpaceNormalBakeMaxQueuedTargets;
         ++index)
    {
        const auto result = queue.Schedule(
            Runtime::RuntimeObjectSpaceNormalBakeRequest{
                .Identity = std::nullopt,
                .Target = MakeTarget(
                    static_cast<std::uint32_t>(index + 1u),
                    1u),
            },
            /*graphicsBackendOperational=*/true);
        ASSERT_TRUE(result.Succeeded()) << index;
    }

    const auto rejected = queue.Schedule(
        Runtime::RuntimeObjectSpaceNormalBakeRequest{
            .Identity = std::nullopt,
            .Target = MakeTarget(
                static_cast<std::uint32_t>(
                    Runtime::kRuntimeObjectSpaceNormalBakeMaxQueuedTargets + 1u),
                1u),
        },
        /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(rejected.Succeeded());
    EXPECT_EQ(rejected.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::CapacityExceeded);
    EXPECT_EQ(queue.PendingCount(),
              Runtime::kRuntimeObjectSpaceNormalBakeMaxQueuedTargets);
    EXPECT_EQ(queue.PendingSubmissionCount(),
              Runtime::kRuntimeObjectSpaceNormalBakeMaxQueuedTargets);
    EXPECT_EQ(queue.Diagnostics().CapacityRejected, 1u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     NonOperationalAndInvalidRequestsLeaveQueueUnchanged)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const IdentityInputFixture fixture{};

    const auto nonOperational = queue.Schedule(
        MakeRequest(fixture),
        /*graphicsBackendOperational=*/false);
    EXPECT_FALSE(nonOperational.Succeeded());
    EXPECT_EQ(nonOperational.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::
                  NonOperationalBackend);
    EXPECT_NE(nonOperational.Diagnostic.find("no CPU fallback"),
              std::string::npos);

    auto invalidTarget = MakeRequest(fixture);
    invalidTarget.Target.Entity = ECS::InvalidEntityHandle;
    const auto invalid = queue.Schedule(
        invalidTarget,
        /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(invalid.Succeeded());
    EXPECT_EQ(invalid.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::InvalidRequest);

    auto unsupported = MakeRequest(fixture);
    unsupported.Identity->Space =
        Graphics::NormalTextureSpace::TangentSpaceNormal;
    const auto tangent = queue.Schedule(
        unsupported,
        /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(tangent.Succeeded());
    EXPECT_EQ(tangent.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::
                  UnsupportedNormalTextureSpace);

    auto malformed = MakeRequest(fixture);
    malformed.Identity->PackedPositionBytes.pop_back();
    const auto malformedResult = queue.Schedule(
        malformed,
        /*graphicsBackendOperational=*/true);
    EXPECT_FALSE(malformedResult.Succeeded());
    EXPECT_EQ(malformedResult.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::InvalidRequest);

    EXPECT_EQ(queue.PendingCount(), 0u);
    EXPECT_EQ(queue.PendingSubmissionCount(), 0u);
    EXPECT_EQ(queue.Diagnostics().NonOperationalNoOps, 1u);
    EXPECT_EQ(queue.Diagnostics().InvalidRequests, 3u);
}

TEST(RuntimeObjectSpaceNormalBakeQueue,
     CompletionRequiresServiceAllocatedAssetAndExactStaleKey)
{
    Runtime::RuntimeObjectSpaceNormalBakeQueue queue;
    const IdentityInputFixture fixture{};
    const auto scheduled = queue.Schedule(
        MakeRequest(fixture),
        /*graphicsBackendOperational=*/true);
    ASSERT_TRUE(scheduled.Succeeded());

    const auto missingAsset = queue.Complete(
        scheduled.Submission.StaleKey,
        {});
    EXPECT_FALSE(missingAsset.Succeeded());
    EXPECT_EQ(missingAsset.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::InvalidRequest);
    EXPECT_TRUE(queue.IsLatest(scheduled.Submission.StaleKey));

    auto mismatched = scheduled.Submission.StaleKey;
    ++mismatched.Target.ExpectedProgressiveBindingGeneration;
    EXPECT_FALSE(Runtime::RuntimeObjectSpaceNormalBakeStaleKeyMatches(
        scheduled.Submission.StaleKey,
        mismatched));
    const auto stale = queue.Complete(
        mismatched,
        Assets::AssetId{50u, 1u});
    EXPECT_FALSE(stale.Succeeded());
    EXPECT_EQ(stale.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::StaleCompletion);
    EXPECT_TRUE(queue.IsLatest(scheduled.Submission.StaleKey));
}
