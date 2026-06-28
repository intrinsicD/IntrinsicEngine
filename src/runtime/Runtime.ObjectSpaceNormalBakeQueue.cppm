module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

export namespace Extrinsic::Runtime
{
    enum class RuntimeObjectSpaceNormalBakeStatus : std::uint8_t
    {
        Queued,
        ReadyForBinding,
        InvalidRequest,
        UnsupportedNormalTextureSpace,
        MissingGeneratedTextureAsset,
        NonOperationalBackend,
        StaleCompletion,
    };

    enum class RuntimeObjectSpaceNormalBakeAssetSelection : std::uint8_t
    {
        None,
        EntityScopedFallback,
        ContentKeyInserted,
        ContentKeyReuse,
    };

    struct RuntimeObjectSpaceNormalBakeContentKey
    {
        std::uint64_t GeometryKey = 0u;
        std::uint64_t TexcoordKey = 0u;
        std::uint64_t NormalKey = 0u;
        std::uint32_t VertexCount = 0u;
        std::uint32_t IndexCount = 0u;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return GeometryKey != 0u &&
                   TexcoordKey != 0u &&
                   NormalKey != 0u &&
                   VertexCount != 0u &&
                   IndexCount != 0u;
        }

        friend bool operator==(const RuntimeObjectSpaceNormalBakeContentKey& lhs,
                               const RuntimeObjectSpaceNormalBakeContentKey& rhs) noexcept = default;
    };

    struct RuntimeObjectSpaceNormalBakeContentKeyHash
    {
        [[nodiscard]] std::size_t operator()(
            const RuntimeObjectSpaceNormalBakeContentKey& key) const noexcept;
    };

    struct RuntimeObjectSpaceNormalBakeStaleKey
    {
        std::uint64_t EntityGeneration = 0u;
        Graphics::ObjectSpaceNormalTextureBakeCompletionKey Bake{};
    };

    struct RuntimeObjectSpaceNormalBakeRequest
    {
        Assets::AssetId EntityScopedGeneratedTextureAsset{};
        Graphics::ObjectSpaceNormalTextureBakeSourceKey SourceKey{};
        std::uint64_t EntityGeneration = 0u;
        Graphics::ObjectSpaceNormalTextureBakeOptions Options{};
        RuntimeObjectSpaceNormalBakeContentKey ContentKey{};
        bool HasStableContentKey = false;
    };

    struct RuntimeObjectSpaceNormalBakeSubmission
    {
        Assets::AssetId GeneratedTextureAsset{};
        RuntimeObjectSpaceNormalBakeAssetSelection AssetSelection{
            RuntimeObjectSpaceNormalBakeAssetSelection::None};
        RuntimeObjectSpaceNormalBakeStaleKey StaleKey{};
    };

    struct RuntimeObjectSpaceNormalBakeResult
    {
        RuntimeObjectSpaceNormalBakeStatus Status{
            RuntimeObjectSpaceNormalBakeStatus::InvalidRequest};
        RuntimeObjectSpaceNormalBakeSubmission Submission{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeObjectSpaceNormalBakeStatus::Queued ||
                   Status == RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding;
        }
    };

    struct RuntimeObjectSpaceNormalBakeQueueDiagnostics
    {
        std::uint64_t QueuedRequests = 0u;
        std::uint64_t ReadyCompletions = 0u;
        std::uint64_t StaleCompletions = 0u;
        std::uint64_t NonOperationalNoOps = 0u;
        std::uint64_t InvalidRequests = 0u;
        std::uint64_t ContentKeyReuses = 0u;
    };

    [[nodiscard]] const char* DebugNameForRuntimeObjectSpaceNormalBakeStatus(
        RuntimeObjectSpaceNormalBakeStatus status) noexcept;

    [[nodiscard]] bool RuntimeObjectSpaceNormalBakeStaleKeyMatches(
        const RuntimeObjectSpaceNormalBakeStaleKey& expected,
        const RuntimeObjectSpaceNormalBakeStaleKey& actual) noexcept;

    class RuntimeObjectSpaceNormalBakeQueue
    {
    public:
        [[nodiscard]] RuntimeObjectSpaceNormalBakeResult Schedule(
            const RuntimeObjectSpaceNormalBakeRequest& request,
            bool graphicsBackendOperational);

        [[nodiscard]] RuntimeObjectSpaceNormalBakeResult Complete(
            const RuntimeObjectSpaceNormalBakeStaleKey& completion);

        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
            Diagnostics() const noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;
        [[nodiscard]] std::size_t CachedContentKeyCount() const noexcept;
        void Clear();

    private:
        std::unordered_map<
            RuntimeObjectSpaceNormalBakeContentKey,
            Assets::AssetId,
            RuntimeObjectSpaceNormalBakeContentKeyHash> m_ContentKeyAssets{};
        std::unordered_map<std::uint64_t, RuntimeObjectSpaceNormalBakeStaleKey>
            m_LatestByEntity{};
        RuntimeObjectSpaceNormalBakeQueueDiagnostics m_Diagnostics{};
    };
}
