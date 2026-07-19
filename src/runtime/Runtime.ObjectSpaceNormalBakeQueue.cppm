module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

export namespace Extrinsic::Runtime
{
    inline constexpr std::uint32_t
        kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion = 1u;

    enum class RuntimeObjectSpaceNormalBakeIdentityBuildStatus : std::uint8_t
    {
        Success,
        EmptyInput,
        InvalidPositionByteCount,
        InvalidSurfaceIndexCount,
        InvalidSurfaceIndexByteCount,
        InvalidSurfaceIndex,
        InvalidTexcoordByteCount,
        InvalidNormalByteCount,
        UnsupportedNormalTextureSpace,
    };

    struct RuntimeObjectSpaceNormalBakeIdentityInput
    {
        std::span<const std::byte> PackedPositionBytes{};
        std::span<const std::byte> SurfaceIndexBytes{};
        std::span<const std::byte> ResolvedTexcoordBytes{};
        std::span<const std::byte> ResolvedNormalBytes{};
        std::uint32_t VertexCount = 0u;
        std::uint32_t SurfaceIndexCount = 0u;
        Graphics::ObjectSpaceNormalTextureBakeOptions Options{};
    };

    struct RuntimeObjectSpaceNormalBakeIdentity
    {
        std::uint32_t SchemaVersion =
            kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion;
        std::vector<std::byte> PackedPositionBytes{};
        std::vector<std::byte> SurfaceIndexBytes{};
        std::vector<std::byte> ResolvedTexcoordBytes{};
        std::vector<std::byte> ResolvedNormalBytes{};
        std::uint32_t VertexCount = 0u;
        std::uint32_t SurfaceIndexCount = 0u;
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::uint32_t PaddingTexels = 0u;
        Graphics::NormalTextureSpace Space =
            Graphics::NormalTextureSpace::ObjectSpaceNormal;
        std::uint32_t AtlasUvEpsilonBits = 0u;
        std::uint32_t DegenerateUvAreaEpsilonBits = 0u;
        std::uint32_t DegenerateNormalLengthEpsilonBits = 0u;

        // The builder populates these nonzero derived fingerprints so
        // producers can migrate source-generation keys without weakening
        // exact identity equality. GeometryFingerprint covers both positions
        // and surface-index order.
        std::uint64_t PositionFingerprint = 0u;
        std::uint64_t SurfaceIndexFingerprint = 0u;
        std::uint64_t GeometryFingerprint = 0u;
        std::uint64_t TexcoordFingerprint = 0u;
        std::uint64_t NormalFingerprint = 0u;
        std::uint64_t CombinedFingerprint = 0u;

        friend bool operator==(
            const RuntimeObjectSpaceNormalBakeIdentity& lhs,
            const RuntimeObjectSpaceNormalBakeIdentity& rhs) noexcept;
    };

    struct RuntimeObjectSpaceNormalBakeIdentityBuildResult
    {
        RuntimeObjectSpaceNormalBakeIdentityBuildStatus Status{
            RuntimeObjectSpaceNormalBakeIdentityBuildStatus::EmptyInput};
        std::optional<RuntimeObjectSpaceNormalBakeIdentity> Identity{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status ==
                       RuntimeObjectSpaceNormalBakeIdentityBuildStatus::Success &&
                   Identity.has_value();
        }
    };

    struct RuntimeObjectSpaceNormalBakeIdentityHash
    {
        [[nodiscard]] std::size_t operator()(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) const noexcept;
    };

    [[nodiscard]] RuntimeObjectSpaceNormalBakeIdentityBuildResult
        BuildRuntimeObjectSpaceNormalBakeIdentity(
            const RuntimeObjectSpaceNormalBakeIdentityInput& input);

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

        [[nodiscard]] bool IsLatest(
            const RuntimeObjectSpaceNormalBakeStaleKey& key) const noexcept;
        [[nodiscard]] std::size_t PendingSubmissionCount() const noexcept;
        [[nodiscard]] std::vector<RuntimeObjectSpaceNormalBakeSubmission>
            TakePendingSubmissions(std::size_t maxCount = 0u);
        void RequeuePendingSubmission(RuntimeObjectSpaceNormalBakeSubmission submission);
        void ClearPending();

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
        std::deque<RuntimeObjectSpaceNormalBakeSubmission> m_PendingSubmissions{};
        RuntimeObjectSpaceNormalBakeQueueDiagnostics m_Diagnostics{};
    };
}
