module;

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

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

        [[nodiscard]] std::uint64_t MixHash(std::uint64_t seed,
                                            const std::uint64_t value) noexcept
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            return seed;
        }

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
            case RuntimeObjectSpaceNormalBakeStatus::InvalidRequest:
            case RuntimeObjectSpaceNormalBakeStatus::UnsupportedNormalTextureSpace:
            case RuntimeObjectSpaceNormalBakeStatus::MissingGeneratedTextureAsset:
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

    std::size_t RuntimeObjectSpaceNormalBakeContentKeyHash::operator()(
        const RuntimeObjectSpaceNormalBakeContentKey& key) const noexcept
    {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        hash = MixHash(hash, key.GeometryKey);
        hash = MixHash(hash, key.TexcoordKey);
        hash = MixHash(hash, key.NormalKey);
        hash = MixHash(hash, key.VertexCount);
        hash = MixHash(hash, key.IndexCount);
        return static_cast<std::size_t>(hash);
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
        case RuntimeObjectSpaceNormalBakeStatus::MissingGeneratedTextureAsset:
            return "MissingGeneratedTextureAsset";
        case RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend:
            return "NonOperationalBackend";
        case RuntimeObjectSpaceNormalBakeStatus::StaleCompletion:
            return "StaleCompletion";
        }
        return "Unknown";
    }

    bool RuntimeObjectSpaceNormalBakeStaleKeyMatches(
        const RuntimeObjectSpaceNormalBakeStaleKey& expected,
        const RuntimeObjectSpaceNormalBakeStaleKey& actual) noexcept
    {
        return expected.EntityGeneration == actual.EntityGeneration &&
               Graphics::ObjectSpaceNormalTextureBakeCompletionKeyMatches(
                   expected.Bake,
                   actual.Bake);
    }

    RuntimeObjectSpaceNormalBakeResult RuntimeObjectSpaceNormalBakeQueue::Schedule(
        const RuntimeObjectSpaceNormalBakeRequest& request,
        const bool graphicsBackendOperational)
    {
        const auto options =
            Graphics::ResolveObjectSpaceNormalTextureBakeOptions(request.Options);

        if (request.SourceKey.EntityKey == 0u || request.EntityGeneration == 0u)
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::InvalidRequest,
                "object-space normal bake request has no stable entity key");
        }

        if (options.Space != Graphics::NormalTextureSpace::ObjectSpaceNormal)
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::UnsupportedNormalTextureSpace,
                "only ObjectSpaceNormal runtime bake requests are supported");
        }

        if (!graphicsBackendOperational)
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::NonOperationalBackend,
                "graphics backend is non-operational; no CPU fallback was scheduled");
        }

        Assets::AssetId generated = request.EntityScopedGeneratedTextureAsset;
        RuntimeObjectSpaceNormalBakeAssetSelection selection =
            RuntimeObjectSpaceNormalBakeAssetSelection::EntityScopedFallback;

        if (request.HasStableContentKey && request.ContentKey.IsValid())
        {
            auto cached = m_ContentKeyAssets.find(request.ContentKey);
            if (cached != m_ContentKeyAssets.end())
            {
                generated = cached->second;
                selection =
                    RuntimeObjectSpaceNormalBakeAssetSelection::ContentKeyReuse;
                ++m_Diagnostics.ContentKeyReuses;
            }
            else if (generated.IsValid())
            {
                m_ContentKeyAssets.emplace(request.ContentKey, generated);
                selection =
                    RuntimeObjectSpaceNormalBakeAssetSelection::ContentKeyInserted;
            }
        }

        if (!generated.IsValid())
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::MissingGeneratedTextureAsset,
                "object-space normal bake request has no generated texture asset");
        }

        RuntimeObjectSpaceNormalBakeSubmission submission{
            .GeneratedTextureAsset = generated,
            .AssetSelection = selection,
            .StaleKey = RuntimeObjectSpaceNormalBakeStaleKey{
                .EntityGeneration = request.EntityGeneration,
                .Bake = Graphics::ObjectSpaceNormalTextureBakeCompletionKey{
                    .GeneratedTextureAsset = generated,
                    .Source = request.SourceKey,
                    .Width = options.Width,
                    .Height = options.Height,
                    .PaddingTexels = options.PaddingTexels,
                    .Space = options.Space,
                },
            },
        };

        m_LatestByEntity[request.SourceKey.EntityKey] = submission.StaleKey;
        m_PendingSubmissions.push_back(submission);
        ++m_Diagnostics.QueuedRequests;

        return RuntimeObjectSpaceNormalBakeResult{
            .Status = RuntimeObjectSpaceNormalBakeStatus::Queued,
            .Submission = submission,
            .Diagnostic = "queued object-space normal GPU bake request",
        };
    }

    RuntimeObjectSpaceNormalBakeResult RuntimeObjectSpaceNormalBakeQueue::Complete(
        const RuntimeObjectSpaceNormalBakeStaleKey& completion)
    {
        const std::uint64_t entity = completion.Bake.Source.EntityKey;
        if (entity == 0u)
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::InvalidRequest,
                "object-space normal bake completion has no stable entity key");
        }

        const auto latest = m_LatestByEntity.find(entity);
        if (latest == m_LatestByEntity.end() ||
            !RuntimeObjectSpaceNormalBakeStaleKeyMatches(latest->second, completion))
        {
            return Fail(
                m_Diagnostics,
                RuntimeObjectSpaceNormalBakeStatus::StaleCompletion,
                "stale object-space normal bake completion discarded");
        }

        RuntimeObjectSpaceNormalBakeSubmission submission{
            .GeneratedTextureAsset = completion.Bake.GeneratedTextureAsset,
            .AssetSelection =
                RuntimeObjectSpaceNormalBakeAssetSelection::None,
            .StaleKey = completion,
        };
        m_LatestByEntity.erase(latest);
        std::erase_if(
            m_PendingSubmissions,
            [&completion](const RuntimeObjectSpaceNormalBakeSubmission& pending)
            {
                return SubmissionMatches(pending, completion);
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
        const std::uint64_t entity = key.Bake.Source.EntityKey;
        if (entity == 0u)
            return false;

        const auto latest = m_LatestByEntity.find(entity);
        return latest != m_LatestByEntity.end() &&
               RuntimeObjectSpaceNormalBakeStaleKeyMatches(latest->second, key);
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
        m_PendingSubmissions.push_back(std::move(submission));
    }

    void RuntimeObjectSpaceNormalBakeQueue::ClearPending()
    {
        m_LatestByEntity.clear();
        m_PendingSubmissions.clear();
    }

    const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
    RuntimeObjectSpaceNormalBakeQueue::Diagnostics() const noexcept
    {
        return m_Diagnostics;
    }

    std::size_t RuntimeObjectSpaceNormalBakeQueue::PendingCount() const noexcept
    {
        return m_LatestByEntity.size();
    }

    std::size_t RuntimeObjectSpaceNormalBakeQueue::CachedContentKeyCount() const noexcept
    {
        return m_ContentKeyAssets.size();
    }

    void RuntimeObjectSpaceNormalBakeQueue::Clear()
    {
        m_ContentKeyAssets.clear();
        m_LatestByEntity.clear();
        m_PendingSubmissions.clear();
        m_Diagnostics = {};
    }
}
