module;

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    namespace
    {
        inline constexpr std::uint64_t kFingerprintOffsetBasis =
            0xcbf29ce484222325ull;
        inline constexpr std::uint64_t kFingerprintPrime =
            0x100000001b3ull;

        enum class IdentityFingerprintDomain : std::uint32_t
        {
            Geometry = 1u,
            Combined = 2u,
        };

        void FingerprintByte(std::uint64_t& fingerprint,
                             const std::uint8_t value) noexcept
        {
            fingerprint ^= value;
            fingerprint *= kFingerprintPrime;
        }

        void FingerprintU32(std::uint64_t& fingerprint,
                            const std::uint32_t value) noexcept
        {
            for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
            {
                FingerprintByte(
                    fingerprint,
                    static_cast<std::uint8_t>((value >> shift) & 0xffu));
            }
        }

        void FingerprintU64(std::uint64_t& fingerprint,
                            const std::uint64_t value) noexcept
        {
            for (std::uint32_t shift = 0u; shift < 64u; shift += 8u)
            {
                FingerprintByte(
                    fingerprint,
                    static_cast<std::uint8_t>((value >> shift) & 0xffu));
            }
        }

        void FingerprintSizedU32Stream(
            std::uint64_t& fingerprint,
            const std::span<const std::byte> bytes) noexcept
        {
            FingerprintU64(fingerprint, bytes.size());
            const std::size_t alignedSize =
                bytes.size() - (bytes.size() % sizeof(std::uint32_t));
            for (std::size_t offset = 0u;
                 offset < alignedSize;
                 offset += sizeof(std::uint32_t))
            {
                std::uint32_t word = 0u;
                std::memcpy(
                    &word,
                    bytes.data() + offset,
                    sizeof(word));
                FingerprintU32(fingerprint, word);
            }
            for (std::size_t offset = alignedSize;
                 offset < bytes.size();
                 ++offset)
            {
                FingerprintByte(
                    fingerprint,
                    std::to_integer<std::uint8_t>(bytes[offset]));
            }
        }

        [[nodiscard]] std::uint64_t FinishFingerprint(
            const std::uint64_t fingerprint) noexcept
        {
            return fingerprint == 0u ? 1u : fingerprint;
        }

        [[nodiscard]] std::uint64_t BeginIdentityFingerprint(
            const IdentityFingerprintDomain domain) noexcept
        {
            std::uint64_t fingerprint = kFingerprintOffsetBasis;
            FingerprintU32(
                fingerprint,
                kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion);
            FingerprintU32(fingerprint, static_cast<std::uint32_t>(domain));
            return fingerprint;
        }

        [[nodiscard]] std::uint64_t FingerprintCanonicalU32Stream(
            const std::span<const std::byte> bytes) noexcept
        {
            if (bytes.empty())
                return 0u;

            std::uint64_t fingerprint = kFingerprintOffsetBasis;
            for (std::size_t offset = 0u;
                 offset < bytes.size();
                 offset += sizeof(std::uint32_t))
            {
                std::uint32_t word = 0u;
                std::memcpy(
                    &word,
                    bytes.data() + offset,
                    sizeof(word));
                FingerprintU32(fingerprint, word);
            }
            return FinishFingerprint(fingerprint);
        }

        [[nodiscard]] std::uint64_t FingerprintGeometry(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
        {
            std::uint64_t fingerprint =
                BeginIdentityFingerprint(IdentityFingerprintDomain::Geometry);
            FingerprintU32(fingerprint, identity.VertexCount);
            FingerprintU32(fingerprint, identity.SurfaceIndexCount);
            FingerprintSizedU32Stream(
                fingerprint,
                identity.PackedPositionBytes);
            FingerprintSizedU32Stream(
                fingerprint,
                identity.SurfaceIndexBytes);
            return FinishFingerprint(fingerprint);
        }

        [[nodiscard]] std::uint64_t FingerprintIdentity(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
        {
            std::uint64_t fingerprint =
                BeginIdentityFingerprint(IdentityFingerprintDomain::Combined);
            FingerprintU32(fingerprint, identity.SchemaVersion);
            FingerprintU32(fingerprint, identity.VertexCount);
            FingerprintU32(fingerprint, identity.SurfaceIndexCount);
            FingerprintSizedU32Stream(
                fingerprint,
                identity.PackedPositionBytes);
            FingerprintSizedU32Stream(
                fingerprint,
                identity.SurfaceIndexBytes);
            FingerprintSizedU32Stream(
                fingerprint,
                identity.ResolvedTexcoordBytes);
            FingerprintSizedU32Stream(
                fingerprint,
                identity.ResolvedNormalBytes);
            FingerprintU32(fingerprint, identity.Width);
            FingerprintU32(fingerprint, identity.Height);
            FingerprintU32(fingerprint, identity.PaddingTexels);
            FingerprintU32(
                fingerprint,
                static_cast<std::uint32_t>(identity.Space));
            FingerprintU32(fingerprint, identity.AtlasUvEpsilonBits);
            FingerprintU32(
                fingerprint,
                identity.DegenerateUvAreaEpsilonBits);
            FingerprintU32(
                fingerprint,
                identity.DegenerateNormalLengthEpsilonBits);
            return FinishFingerprint(fingerprint);
        }

        [[nodiscard]] std::uint32_t CanonicalFloatBits(
            const float value) noexcept
        {
            const float canonical = value == 0.0f ? 0.0f : value;
            return std::bit_cast<std::uint32_t>(canonical);
        }

        [[nodiscard]] bool ByteCountMatches(
            const std::span<const std::byte> bytes,
            const std::uint32_t elementCount,
            const std::size_t bytesPerElement) noexcept
        {
            if (static_cast<std::size_t>(elementCount) >
                std::numeric_limits<std::size_t>::max() / bytesPerElement)
            {
                return false;
            }
            return bytes.size() ==
                   static_cast<std::size_t>(elementCount) * bytesPerElement;
        }

        [[nodiscard]] std::vector<std::byte> CopyCanonicalFloatBytes(
            const std::span<const std::byte> source)
        {
            std::vector<std::byte> canonical{
                source.begin(),
                source.end()};
            for (std::size_t offset = 0u;
                 offset < canonical.size();
                 offset += sizeof(float))
            {
                float value = 0.0f;
                std::memcpy(
                    &value,
                    canonical.data() + offset,
                    sizeof(value));
                if (value == 0.0f)
                {
                    constexpr float kPositiveZero = 0.0f;
                    std::memcpy(
                        canonical.data() + offset,
                        &kPositiveZero,
                        sizeof(kPositiveZero));
                }
            }
            return canonical;
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeIdentityBuildResult
        FailIdentityBuild(
            const RuntimeObjectSpaceNormalBakeIdentityBuildStatus status,
            std::string diagnostic)
        {
            return RuntimeObjectSpaceNormalBakeIdentityBuildResult{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeResult Fail(
            RuntimeObjectSpaceNormalBakeQueueDiagnostics& diagnostics,
            const RuntimeObjectSpaceNormalBakeStatus status,
            std::string diagnostic)
        {
            switch (status)
            {
            case RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend:
                ++diagnostics.NonOperationalNoOps;
                break;
            case RuntimeObjectSpaceNormalBakeStatus::StaleCompletion:
                ++diagnostics.StaleCompletions;
                break;
            case RuntimeObjectSpaceNormalBakeStatus::CapacityExceeded:
                ++diagnostics.CapacityRejected;
                break;
            case RuntimeObjectSpaceNormalBakeStatus::InvalidRequest:
            case RuntimeObjectSpaceNormalBakeStatus::UnsupportedNormalTextureSpace:
                ++diagnostics.InvalidRequests;
                break;
            case RuntimeObjectSpaceNormalBakeStatus::Queued:
            case RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding:
                break;
            }

            return RuntimeObjectSpaceNormalBakeResult{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] bool SubmissionMatches(
            const RuntimeObjectSpaceNormalBakeSubmission& submission,
            const RuntimeObjectSpaceNormalBakeStaleKey& key) noexcept
        {
            return RuntimeObjectSpaceNormalBakeStaleKeyMatches(
                submission.StaleKey,
                key);
        }

        [[nodiscard]] bool TargetAddressMatches(
            const RuntimeObjectSpaceNormalBakeTarget& lhs,
            const RuntimeObjectSpaceNormalBakeTarget& rhs) noexcept
        {
            return lhs.World == rhs.World &&
                   lhs.BindingEpoch == rhs.BindingEpoch &&
                   lhs.Entity == rhs.Entity &&
                   lhs.StableEntityId == rhs.StableEntityId &&
                   lhs.PresentationKey == rhs.PresentationKey &&
                   lhs.Semantic == rhs.Semantic;
        }

        [[nodiscard]] std::size_t IdentityByteCount(
            const std::optional<RuntimeObjectSpaceNormalBakeIdentity>& identity)
            noexcept
        {
            if (!identity.has_value())
                return 0u;
            return identity->PackedPositionBytes.size() +
                   identity->SurfaceIndexBytes.size() +
                   identity->ResolvedTexcoordBytes.size() +
                   identity->ResolvedNormalBytes.size();
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeRequestBuildResult
        FailRequestBuild(
            const RuntimeObjectSpaceNormalBakeRequestBuildStatus status,
            std::string diagnostic,
            const MeshPackStatus packStatus = MeshPackStatus::WrongDomain,
            const RuntimeObjectSpaceNormalBakeIdentityBuildStatus identityStatus =
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::EmptyInput)
        {
            return RuntimeObjectSpaceNormalBakeRequestBuildResult{
                .Status = status,
                .PackStatus = packStatus,
                .IdentityStatus = identityStatus,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] std::optional<RuntimeObjectSpaceNormalBakeIdentity>
        CanonicalizeIdentity(
            const RuntimeObjectSpaceNormalBakeIdentity& source)
        {
            if (source.Space !=
                Graphics::NormalTextureSpace::ObjectSpaceNormal)
            {
                return std::nullopt;
            }

            const auto rebuilt = BuildRuntimeObjectSpaceNormalBakeIdentity(
                RuntimeObjectSpaceNormalBakeIdentityInput{
                    .PackedPositionBytes = source.PackedPositionBytes,
                    .SurfaceIndexBytes = source.SurfaceIndexBytes,
                    .ResolvedTexcoordBytes =
                        source.ResolvedTexcoordBytes,
                    .ResolvedNormalBytes = source.ResolvedNormalBytes,
                    .VertexCount = source.VertexCount,
                    .SurfaceIndexCount = source.SurfaceIndexCount,
                    .Options =
                        Graphics::ObjectSpaceNormalTextureBakeOptions{
                            .Width = source.Width,
                            .Height = source.Height,
                            .PaddingTexels = source.PaddingTexels,
                            .AtlasUvEpsilon =
                                std::bit_cast<float>(
                                    source.AtlasUvEpsilonBits),
                            .DegenerateUvAreaEpsilon =
                                std::bit_cast<float>(
                                    source.DegenerateUvAreaEpsilonBits),
                            .DegenerateNormalLengthEpsilon =
                                std::bit_cast<float>(
                                    source
                                        .DegenerateNormalLengthEpsilonBits),
                            .Space = source.Space,
                        },
                });
            if (!rebuilt.Succeeded() ||
                rebuilt.Identity->SchemaVersion != source.SchemaVersion ||
                *rebuilt.Identity != source)
            {
                return std::nullopt;
            }
            return rebuilt.Identity;
        }
    }

    bool operator==(const RuntimeObjectSpaceNormalBakeIdentity& lhs,
                    const RuntimeObjectSpaceNormalBakeIdentity& rhs) noexcept
    {
        return lhs.SchemaVersion == rhs.SchemaVersion &&
               lhs.PackedPositionBytes == rhs.PackedPositionBytes &&
               lhs.SurfaceIndexBytes == rhs.SurfaceIndexBytes &&
               lhs.ResolvedTexcoordBytes == rhs.ResolvedTexcoordBytes &&
               lhs.ResolvedNormalBytes == rhs.ResolvedNormalBytes &&
               lhs.VertexCount == rhs.VertexCount &&
               lhs.SurfaceIndexCount == rhs.SurfaceIndexCount &&
               lhs.Width == rhs.Width &&
               lhs.Height == rhs.Height &&
               lhs.PaddingTexels == rhs.PaddingTexels &&
               lhs.Space == rhs.Space &&
               lhs.AtlasUvEpsilonBits == rhs.AtlasUvEpsilonBits &&
               lhs.DegenerateUvAreaEpsilonBits ==
                   rhs.DegenerateUvAreaEpsilonBits &&
               lhs.DegenerateNormalLengthEpsilonBits ==
                   rhs.DegenerateNormalLengthEpsilonBits;
    }

    std::size_t RuntimeObjectSpaceNormalBakeIdentityHash::operator()(
        const RuntimeObjectSpaceNormalBakeIdentity& identity) const noexcept
    {
        return static_cast<std::size_t>(FingerprintIdentity(identity));
    }

    std::uint64_t ComputeRuntimeObjectSpaceNormalBakeIdentityDigest(
        const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
    {
        return FingerprintIdentity(identity);
    }

    RuntimeObjectSpaceNormalBakeIdentityBuildResult
    BuildRuntimeObjectSpaceNormalBakeIdentity(
        const RuntimeObjectSpaceNormalBakeIdentityInput& input)
    {
        static_assert(sizeof(float) == 4u);
        static_assert(sizeof(std::uint32_t) == 4u);

        const auto options =
            Graphics::ResolveObjectSpaceNormalTextureBakeOptions(input.Options);
        if (options.Space != Graphics::NormalTextureSpace::ObjectSpaceNormal)
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                    UnsupportedNormalTextureSpace,
                "only ObjectSpaceNormal bake identities are supported");
        }

        if (input.VertexCount == 0u ||
            input.SurfaceIndexCount == 0u)
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::EmptyInput,
                "object-space normal bake identity input is empty");
        }

        if (input.SurfaceIndexCount % 3u != 0u)
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                    InvalidSurfaceIndexCount,
                "object-space normal bake surface index count is not a triangle list");
        }

        constexpr std::size_t kPackedFloat2Bytes = sizeof(float) * 2u;
        constexpr std::size_t kPackedFloat3Bytes = sizeof(float) * 3u;
        if (!ByteCountMatches(
                input.PackedPositionBytes,
                input.VertexCount,
                kPackedFloat3Bytes))
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                    InvalidPositionByteCount,
                "object-space normal bake position bytes do not match vertex count");
        }

        if (input.SurfaceIndexBytes.size() % sizeof(std::uint32_t) != 0u ||
            !ByteCountMatches(
                input.SurfaceIndexBytes,
                input.SurfaceIndexCount,
                sizeof(std::uint32_t)))
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                    InvalidSurfaceIndexByteCount,
                "object-space normal bake topology bytes do not match index count");
        }

        if (!ByteCountMatches(
                input.ResolvedTexcoordBytes,
                input.VertexCount,
                kPackedFloat2Bytes))
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                    InvalidTexcoordByteCount,
                "object-space normal bake texcoord bytes do not match vertex count");
        }

        if (!ByteCountMatches(
                input.ResolvedNormalBytes,
                input.VertexCount,
                kPackedFloat3Bytes))
        {
            return FailIdentityBuild(
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                    InvalidNormalByteCount,
                "object-space normal bake normal bytes do not match vertex count");
        }

        for (std::size_t offset = 0u;
             offset < input.SurfaceIndexBytes.size();
             offset += sizeof(std::uint32_t))
        {
            std::uint32_t index = 0u;
            std::memcpy(
                &index,
                input.SurfaceIndexBytes.data() + offset,
                sizeof(index));
            if (index >= input.VertexCount)
            {
                return FailIdentityBuild(
                    RuntimeObjectSpaceNormalBakeIdentityBuildStatus::
                        InvalidSurfaceIndex,
                    "object-space normal bake topology references an invalid vertex");
            }
        }

        RuntimeObjectSpaceNormalBakeIdentity identity{
            .SchemaVersion =
                kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion,
            .PackedPositionBytes =
                CopyCanonicalFloatBytes(input.PackedPositionBytes),
            .SurfaceIndexBytes = {
                input.SurfaceIndexBytes.begin(),
                input.SurfaceIndexBytes.end()},
            .ResolvedTexcoordBytes =
                CopyCanonicalFloatBytes(input.ResolvedTexcoordBytes),
            .ResolvedNormalBytes =
                CopyCanonicalFloatBytes(input.ResolvedNormalBytes),
            .VertexCount = input.VertexCount,
            .SurfaceIndexCount = input.SurfaceIndexCount,
            .Width = options.Width,
            .Height = options.Height,
            .PaddingTexels = options.PaddingTexels,
            .Space = options.Space,
            .AtlasUvEpsilonBits =
                CanonicalFloatBits(options.AtlasUvEpsilon),
            .DegenerateUvAreaEpsilonBits =
                CanonicalFloatBits(options.DegenerateUvAreaEpsilon),
            .DegenerateNormalLengthEpsilonBits =
                CanonicalFloatBits(options.DegenerateNormalLengthEpsilon),
        };
        identity.PositionFingerprint =
            FingerprintCanonicalU32Stream(identity.PackedPositionBytes);
        identity.SurfaceIndexFingerprint =
            FingerprintCanonicalU32Stream(identity.SurfaceIndexBytes);
        identity.GeometryFingerprint = FingerprintGeometry(identity);
        identity.TexcoordFingerprint =
            FingerprintCanonicalU32Stream(identity.ResolvedTexcoordBytes);
        identity.NormalFingerprint =
            FingerprintCanonicalU32Stream(identity.ResolvedNormalBytes);
        identity.CombinedFingerprint = FingerprintIdentity(identity);

        return RuntimeObjectSpaceNormalBakeIdentityBuildResult{
            .Status =
                RuntimeObjectSpaceNormalBakeIdentityBuildStatus::Success,
            .Identity = std::move(identity),
            .Diagnostic =
                "built exact object-space normal bake identity",
        };
    }

    bool IsValidRuntimeObjectSpaceNormalBakeTarget(
        const RuntimeObjectSpaceNormalBakeTarget& target) noexcept
    {
        return target.World.IsValid() &&
               target.BindingEpoch != 0u &&
               target.Entity != ECS::InvalidEntityHandle &&
               target.StableEntityId != 0u &&
               target.Semantic == ProgressiveSlotSemantic::Normal;
    }

    RuntimeObjectSpaceNormalBakeRequestBuildResult
    BuildRuntimeObjectSpaceNormalBakeRequest(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RuntimeObjectSpaceNormalBakeTarget target,
        const Graphics::ObjectSpaceNormalTextureBakeOptions& options,
        const VertexChannelBindingSet* const channelBindings)
    {
        if (!IsValidRuntimeObjectSpaceNormalBakeTarget(target))
        {
            return FailRequestBuild(
                RuntimeObjectSpaceNormalBakeRequestBuildStatus::InvalidTarget,
                "object-space normal bake target lifetime is invalid");
        }

        MeshPackBuffer packed{};
        const MeshPackResult pack =
            PackMesh(view, channelBindings, packed);
        if (pack.Status != MeshPackStatus::Success ||
            !pack.Upload.has_value())
        {
            return FailRequestBuild(
                RuntimeObjectSpaceNormalBakeRequestBuildStatus::
                    MeshPackRejected,
                std::string{
                    "object-space normal bake mesh packing failed: "} +
                    DebugNameForMeshPackStatus(pack.Status),
                pack.Status);
        }

        const Graphics::GpuWorld::GeometryUploadDesc& upload =
            *pack.Upload;
        const RuntimeObjectSpaceNormalBakeIdentityBuildResult identity =
            BuildRuntimeObjectSpaceNormalBakeIdentity(
                RuntimeObjectSpaceNormalBakeIdentityInput{
                    .PackedPositionBytes = upload.PositionBytes,
                    .SurfaceIndexBytes =
                        std::as_bytes(upload.SurfaceIndices),
                    .ResolvedTexcoordBytes = upload.TexcoordBytes,
                    .ResolvedNormalBytes = upload.NormalBytes,
                    .VertexCount = upload.VertexCount,
                    .SurfaceIndexCount =
                        static_cast<std::uint32_t>(
                            upload.SurfaceIndices.size()),
                    .Options = options,
                });
        if (!identity.Succeeded())
        {
            return FailRequestBuild(
                RuntimeObjectSpaceNormalBakeRequestBuildStatus::
                    IdentityRejected,
                identity.Diagnostic,
                pack.Status,
                identity.Status);
        }

        return RuntimeObjectSpaceNormalBakeRequestBuildResult{
            .Status =
                RuntimeObjectSpaceNormalBakeRequestBuildStatus::Success,
            .PackStatus = pack.Status,
            .IdentityStatus = identity.Status,
            .Request =
                RuntimeObjectSpaceNormalBakeRequest{
                    .Identity = std::move(*identity.Identity),
                    .Target = std::move(target),
                },
            .Diagnostic =
                "built exact object-space normal bake request",
        };
    }

    const char* DebugNameForRuntimeObjectSpaceNormalBakeStatus(
        const RuntimeObjectSpaceNormalBakeStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeObjectSpaceNormalBakeStatus::Queued:
            return "Queued";
        case RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding:
            return "ReadyForBinding";
        case RuntimeObjectSpaceNormalBakeStatus::InvalidRequest:
            return "InvalidRequest";
        case RuntimeObjectSpaceNormalBakeStatus::UnsupportedNormalTextureSpace:
            return "UnsupportedNormalTextureSpace";
        case RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend:
            return "NonOperationalBackend";
        case RuntimeObjectSpaceNormalBakeStatus::StaleCompletion:
            return "StaleCompletion";
        case RuntimeObjectSpaceNormalBakeStatus::CapacityExceeded:
            return "CapacityExceeded";
        }
        return "Unknown";
    }

    bool RuntimeObjectSpaceNormalBakeStaleKeyMatches(
        const RuntimeObjectSpaceNormalBakeStaleKey& expected,
        const RuntimeObjectSpaceNormalBakeStaleKey& actual) noexcept
    {
        return expected.RequestGeneration != 0u &&
               expected.RequestGeneration == actual.RequestGeneration &&
               expected.Target == actual.Target;
    }

    RuntimeObjectSpaceNormalBakeResult RuntimeObjectSpaceNormalBakeQueue::Schedule(
        const RuntimeObjectSpaceNormalBakeRequest& request,
        const bool graphicsBackendOperational)
    {
        if (!IsValidRuntimeObjectSpaceNormalBakeTarget(request.Target))
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::InvalidRequest,
                "object-space normal bake request has an invalid target lifetime");
        }

        std::optional<RuntimeObjectSpaceNormalBakeIdentity> identity{};
        if (request.Identity.has_value())
        {
            if (request.Identity->Space !=
                Graphics::NormalTextureSpace::ObjectSpaceNormal)
            {
                return Fail(
                    m_Diagnostics,
                    RuntimeObjectSpaceNormalBakeStatus::
                        UnsupportedNormalTextureSpace,
                    "only ObjectSpaceNormal runtime bake identities are supported");
            }
            identity = CanonicalizeIdentity(*request.Identity);
            if (!identity.has_value())
            {
                return Fail(
                    m_Diagnostics,
                    RuntimeObjectSpaceNormalBakeStatus::InvalidRequest,
                    "object-space normal bake request identity is malformed or non-canonical");
            }
        }

        if (!graphicsBackendOperational)
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend,
                "graphics backend is non-operational; no CPU fallback was scheduled");
        }

        const auto latest = std::find_if(
            m_LatestTargets.begin(),
            m_LatestTargets.end(),
            [&request](const RuntimeObjectSpaceNormalBakeStaleKey& key)
            {
                return TargetAddressMatches(
                    key.Target,
                    request.Target);
            });

        std::size_t supersededBytes = 0u;
        std::size_t supersededCount = 0u;
        for (const RuntimeObjectSpaceNormalBakeSubmission& pending :
             m_PendingSubmissions)
        {
            if (TargetAddressMatches(
                    pending.Target,
                    request.Target))
            {
                supersededBytes += IdentityByteCount(pending.Identity);
                ++supersededCount;
            }
        }

        const std::size_t identityBytes = IdentityByteCount(identity);
        const std::size_t retainedBytes =
            m_PendingIdentityBytes >= supersededBytes
                ? m_PendingIdentityBytes - supersededBytes
                : 0u;
        const bool addsTarget = latest == m_LatestTargets.end();
        if ((addsTarget &&
             m_LatestTargets.size() >=
                 kRuntimeObjectSpaceNormalBakeMaxQueuedTargets) ||
            identityBytes >
                kRuntimeObjectSpaceNormalBakeMaxQueuedIdentityBytes ||
            retainedBytes >
                kRuntimeObjectSpaceNormalBakeMaxQueuedIdentityBytes -
                    identityBytes)
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::CapacityExceeded,
                "object-space normal bake queued target or identity-byte capacity exceeded");
        }

        if (supersededCount != 0u)
        {
            std::erase_if(
                m_PendingSubmissions,
                [&request](
                    const RuntimeObjectSpaceNormalBakeSubmission& pending)
                {
                    return TargetAddressMatches(
                        pending.Target,
                        request.Target);
                });
            m_Diagnostics.SupersededQueuedRequests += supersededCount;
        }
        m_PendingIdentityBytes = retainedBytes;

        std::uint64_t requestGeneration = m_NextRequestGeneration++;
        if (requestGeneration == 0u)
        {
            requestGeneration = m_NextRequestGeneration++;
            assert(requestGeneration != 0u);
        }

        const RuntimeObjectSpaceNormalBakeStaleKey staleKey{
            .Target = request.Target,
            .RequestGeneration = requestGeneration,
        };
        RuntimeObjectSpaceNormalBakeSubmission submission{
            .Identity = std::move(identity),
            .Target = request.Target,
            .StaleKey = staleKey,
        };

        if (latest != m_LatestTargets.end())
            *latest = staleKey;
        else
            m_LatestTargets.push_back(staleKey);
        m_PendingSubmissions.push_back(submission);
        m_PendingIdentityBytes += identityBytes;
        ++m_Diagnostics.QueuedRequests;
        if (submission.Identity.has_value())
            ++m_Diagnostics.IdentityRequests;
        else
            ++m_Diagnostics.IdentitylessRequests;

        return RuntimeObjectSpaceNormalBakeResult{
            .Status = RuntimeObjectSpaceNormalBakeStatus::Queued,
            .Submission = submission,
            .Diagnostic =
                submission.Identity.has_value()
                    ? "queued object-space normal GPU bake request with exact identity"
                    : "queued object-space normal GPU bake request as non-reusable identity-less work",
        };
    }

    RuntimeObjectSpaceNormalBakeResult RuntimeObjectSpaceNormalBakeQueue::Complete(
        const RuntimeObjectSpaceNormalBakeStaleKey& completion,
        const Assets::AssetId generatedTextureAsset,
        const RuntimeObjectSpaceNormalBakeAssetSelection selection)
    {
        if (!IsValidRuntimeObjectSpaceNormalBakeTarget(completion.Target) ||
            completion.RequestGeneration == 0u ||
            !generatedTextureAsset.IsValid())
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::InvalidRequest,
                "object-space normal bake completion has invalid target, request generation, or generated asset");
        }

        const auto latest = std::find_if(
            m_LatestTargets.begin(),
            m_LatestTargets.end(),
            [&completion](const RuntimeObjectSpaceNormalBakeStaleKey& key)
            {
                return TargetAddressMatches(
                           key.Target,
                           completion.Target) &&
                       RuntimeObjectSpaceNormalBakeStaleKeyMatches(
                           key,
                           completion);
            });
        if (latest == m_LatestTargets.end())
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::StaleCompletion,
                "stale object-space normal bake completion discarded");
        }

        RuntimeObjectSpaceNormalBakeSubmission submission{
            .Target = completion.Target,
            .GeneratedTextureAsset = generatedTextureAsset,
            .AssetSelection = selection,
            .StaleKey = completion,
        };
        m_LatestTargets.erase(latest);
        std::erase_if(
            m_PendingSubmissions,
            [this, &completion](
                const RuntimeObjectSpaceNormalBakeSubmission& pending)
            {
                if (!SubmissionMatches(pending, completion))
                    return false;
                const std::size_t bytes =
                    IdentityByteCount(pending.Identity);
                m_PendingIdentityBytes =
                    m_PendingIdentityBytes >= bytes
                        ? m_PendingIdentityBytes - bytes
                        : 0u;
                return true;
            });
        ++m_Diagnostics.ReadyCompletions;

        return RuntimeObjectSpaceNormalBakeResult{
            .Status = RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding,
            .Submission = submission,
            .Diagnostic = "object-space normal bake ready for material binding",
        };
    }

    bool RuntimeObjectSpaceNormalBakeQueue::IsLatest(
        const RuntimeObjectSpaceNormalBakeStaleKey& key) const noexcept
    {
        if (!IsValidRuntimeObjectSpaceNormalBakeTarget(key.Target) ||
            key.RequestGeneration == 0u)
            return false;

        return std::ranges::any_of(
            m_LatestTargets,
            [&key](const RuntimeObjectSpaceNormalBakeStaleKey& latest)
            {
                return TargetAddressMatches(latest.Target, key.Target) &&
                       RuntimeObjectSpaceNormalBakeStaleKeyMatches(
                           latest,
                           key);
            });
    }

    bool RuntimeObjectSpaceNormalBakeQueue::Discard(
        const RuntimeObjectSpaceNormalBakeStaleKey& key)
    {
        if (!IsLatest(key))
            return false;

        std::erase_if(
            m_LatestTargets,
            [&key](const RuntimeObjectSpaceNormalBakeStaleKey& latest)
            {
                return TargetAddressMatches(latest.Target, key.Target) &&
                       RuntimeObjectSpaceNormalBakeStaleKeyMatches(latest, key);
            });
        std::erase_if(
            m_PendingSubmissions,
            [this, &key](
                const RuntimeObjectSpaceNormalBakeSubmission& pending)
            {
                if (!SubmissionMatches(pending, key))
                    return false;
                const std::size_t bytes = IdentityByteCount(pending.Identity);
                m_PendingIdentityBytes =
                    m_PendingIdentityBytes >= bytes
                        ? m_PendingIdentityBytes - bytes
                        : 0u;
                return true;
            });
        return true;
    }

    std::size_t
    RuntimeObjectSpaceNormalBakeQueue::PendingSubmissionCount() const noexcept
    {
        return m_PendingSubmissions.size();
    }

    std::vector<RuntimeObjectSpaceNormalBakeSubmission>
    RuntimeObjectSpaceNormalBakeQueue::TakePendingSubmissions(
        const std::size_t maxCount)
    {
        const std::size_t count = maxCount == 0u
            ? m_PendingSubmissions.size()
            : std::min(maxCount, m_PendingSubmissions.size());
        std::vector<RuntimeObjectSpaceNormalBakeSubmission> out{};
        out.reserve(count);
        for (std::size_t index = 0u; index < count; ++index)
        {
            const std::size_t bytes =
                IdentityByteCount(m_PendingSubmissions.front().Identity);
            m_PendingIdentityBytes =
                m_PendingIdentityBytes >= bytes
                    ? m_PendingIdentityBytes - bytes
                    : 0u;
            out.push_back(std::move(m_PendingSubmissions.front()));
            m_PendingSubmissions.pop_front();
        }
        return out;
    }

    void RuntimeObjectSpaceNormalBakeQueue::RequeuePendingSubmission(
        RuntimeObjectSpaceNormalBakeSubmission submission)
    {
        if (!IsLatest(submission.StaleKey))
            return;
        const std::size_t bytes = IdentityByteCount(submission.Identity);
        if (bytes >
                kRuntimeObjectSpaceNormalBakeMaxQueuedIdentityBytes ||
            m_PendingIdentityBytes >
                kRuntimeObjectSpaceNormalBakeMaxQueuedIdentityBytes -
                    bytes)
        {
            ++m_Diagnostics.CapacityRejected;
            return;
        }
        m_PendingIdentityBytes += bytes;
        m_PendingSubmissions.push_back(std::move(submission));
    }

    std::size_t RuntimeObjectSpaceNormalBakeQueue::DetachTargets(
        const WorldHandle world,
        const std::uint64_t bindingEpoch)
    {
        if (!world.IsValid() || bindingEpoch == 0u)
            return 0u;

        const std::size_t before = m_LatestTargets.size();
        std::erase_if(
            m_LatestTargets,
            [world, bindingEpoch](
                const RuntimeObjectSpaceNormalBakeStaleKey& key)
            {
                return key.Target.World == world &&
                       key.Target.BindingEpoch == bindingEpoch;
            });
        std::erase_if(
            m_PendingSubmissions,
            [this, world, bindingEpoch](
                const RuntimeObjectSpaceNormalBakeSubmission& pending)
            {
                if (pending.Target.World != world ||
                    pending.Target.BindingEpoch != bindingEpoch)
                {
                    return false;
                }
                const std::size_t bytes =
                    IdentityByteCount(pending.Identity);
                m_PendingIdentityBytes =
                    m_PendingIdentityBytes >= bytes
                        ? m_PendingIdentityBytes - bytes
                        : 0u;
                return true;
            });
        return before - m_LatestTargets.size();
    }

    void RuntimeObjectSpaceNormalBakeQueue::ClearPending()
    {
        m_LatestTargets.clear();
        m_PendingSubmissions.clear();
        m_PendingIdentityBytes = 0u;
    }

    const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
    RuntimeObjectSpaceNormalBakeQueue::Diagnostics() const noexcept
    {
        return m_Diagnostics;
    }

    std::size_t RuntimeObjectSpaceNormalBakeQueue::PendingCount() const noexcept
    {
        return m_LatestTargets.size();
    }

    std::size_t
    RuntimeObjectSpaceNormalBakeQueue::PendingIdentityByteCount() const noexcept
    {
        return m_PendingIdentityBytes;
    }

    void RuntimeObjectSpaceNormalBakeQueue::Clear()
    {
        m_LatestTargets.clear();
        m_PendingSubmissions.clear();
        m_Diagnostics = {};
        m_PendingIdentityBytes = 0u;
        m_NextRequestGeneration = 1u;
    }
}
