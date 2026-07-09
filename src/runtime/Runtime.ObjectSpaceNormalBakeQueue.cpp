module;

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <deque>
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

        [[nodiscard]] bool SubmissionMatches(
            const RuntimeObjectSpaceNormalBakeSubmission& submission,
            const RuntimeObjectSpaceNormalBakeStaleKey& key) noexcept
        {
            return RuntimeObjectSpaceNormalBakeStaleKeyMatches(
                submission.StaleKey,
                key);
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
