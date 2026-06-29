module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

module Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::uint64_t MixHash(std::uint64_t seed,
                                            const std::uint64_t value) noexcept
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            return seed;
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
        ++m_Diagnostics.ReadyCompletions;

        return RuntimeObjectSpaceNormalBakeResult{
            .Status = RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding,
            .Submission = submission,
            .Diagnostic = "object-space normal bake ready for material binding",
        };
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
        m_Diagnostics = {};
    }
}
