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
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
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
    EXPECT_NE(first.PositionFingerprint,
              FingerprintFloats(fixture.Positions));

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
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(geometry, residency));
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

    // Tangent space is intentionally rejected by the builder. Mutating a
    // copy verifies that the value hash still covers the stored space field.
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
