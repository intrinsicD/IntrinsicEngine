module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;

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

    [[nodiscard]] std::uint64_t ComputeRuntimeObjectSpaceNormalBakeIdentityDigest(
        const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept;

    [[nodiscard]] RuntimeObjectSpaceNormalBakeIdentityBuildResult
        BuildRuntimeObjectSpaceNormalBakeIdentity(
            const RuntimeObjectSpaceNormalBakeIdentityInput& input);

    inline constexpr std::size_t
        kRuntimeObjectSpaceNormalBakeMaxQueuedTargets = 256u;
    inline constexpr std::size_t
        kRuntimeObjectSpaceNormalBakeMaxQueuedIdentityBytes =
            64u * 1024u * 1024u;

    struct RuntimeObjectSpaceNormalBakeTarget
    {
        WorldHandle World{};
        std::uint64_t BindingEpoch = 0u;
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId = 0u;
        std::string PresentationKey{};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Normal};
        std::uint64_t ExpectedProgressiveBindingGeneration = 0u;

        friend bool operator==(
            const RuntimeObjectSpaceNormalBakeTarget& lhs,
            const RuntimeObjectSpaceNormalBakeTarget& rhs) noexcept = default;
    };

    [[nodiscard]] bool IsValidRuntimeObjectSpaceNormalBakeTarget(
        const RuntimeObjectSpaceNormalBakeTarget& target) noexcept;

    struct RuntimeObjectSpaceNormalBakeRequest
    {
        std::optional<RuntimeObjectSpaceNormalBakeIdentity> Identity{};
        RuntimeObjectSpaceNormalBakeTarget Target{};
    };

    enum class RuntimeObjectSpaceNormalBakeRequestBuildStatus : std::uint8_t
    {
        Success,
        InvalidTarget,
        MeshPackRejected,
        IdentityRejected,
    };

    struct RuntimeObjectSpaceNormalBakeRequestBuildResult
    {
        RuntimeObjectSpaceNormalBakeRequestBuildStatus Status{
            RuntimeObjectSpaceNormalBakeRequestBuildStatus::InvalidTarget};
        MeshPackStatus PackStatus{MeshPackStatus::WrongDomain};
        RuntimeObjectSpaceNormalBakeIdentityBuildStatus IdentityStatus{
            RuntimeObjectSpaceNormalBakeIdentityBuildStatus::EmptyInput};
        std::optional<RuntimeObjectSpaceNormalBakeRequest> Request{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status ==
                       RuntimeObjectSpaceNormalBakeRequestBuildStatus::Success &&
                   Request.has_value();
        }
    };

    [[nodiscard]] RuntimeObjectSpaceNormalBakeRequestBuildResult
        BuildRuntimeObjectSpaceNormalBakeRequest(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            RuntimeObjectSpaceNormalBakeTarget target,
            const Graphics::ObjectSpaceNormalTextureBakeOptions& options = {},
            const VertexChannelBindingSet* channelBindings = nullptr);

    enum class RuntimeObjectSpaceNormalBakeStatus : std::uint8_t
    {
        Queued,
        ReadyForBinding,
        InvalidRequest,
        UnsupportedNormalTextureSpace,
        NonOperationalBackend,
        StaleCompletion,
        CapacityExceeded,
    };

    enum class RuntimeObjectSpaceNormalBakeAssetSelection : std::uint8_t
    {
        None,
        NonReusableAllocated,
        IdentityInserted,
        PendingIdentityReuse,
        ProvenReadyReuse,
    };

    struct RuntimeObjectSpaceNormalBakeStaleKey
    {
        RuntimeObjectSpaceNormalBakeTarget Target{};
        std::uint64_t RequestGeneration = 0u;
    };

    struct RuntimeObjectSpaceNormalBakeSubmission
    {
        std::optional<RuntimeObjectSpaceNormalBakeIdentity> Identity{};
        RuntimeObjectSpaceNormalBakeTarget Target{};
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
        std::uint64_t CapacityRejected = 0u;
        std::uint64_t SupersededQueuedRequests = 0u;
        std::uint64_t IdentityRequests = 0u;
        std::uint64_t IdentitylessRequests = 0u;
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
            const RuntimeObjectSpaceNormalBakeStaleKey& completion,
            Assets::AssetId generatedTextureAsset,
            RuntimeObjectSpaceNormalBakeAssetSelection selection =
                RuntimeObjectSpaceNormalBakeAssetSelection::None);

        [[nodiscard]] bool IsLatest(
            const RuntimeObjectSpaceNormalBakeStaleKey& key) const noexcept;
        [[nodiscard]] bool Discard(
            const RuntimeObjectSpaceNormalBakeStaleKey& key);
        [[nodiscard]] std::size_t PendingSubmissionCount() const noexcept;
        [[nodiscard]] std::vector<RuntimeObjectSpaceNormalBakeSubmission>
            TakePendingSubmissions(std::size_t maxCount = 0u);
        void RequeuePendingSubmission(RuntimeObjectSpaceNormalBakeSubmission submission);
        [[nodiscard]] std::size_t DetachTargets(
            WorldHandle world,
            std::uint64_t bindingEpoch);
        void ClearPending();

        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
            Diagnostics() const noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;
        [[nodiscard]] std::size_t PendingIdentityByteCount() const noexcept;
        void Clear();

    private:
        std::vector<RuntimeObjectSpaceNormalBakeStaleKey> m_LatestTargets{};
        std::deque<RuntimeObjectSpaceNormalBakeSubmission> m_PendingSubmissions{};
        RuntimeObjectSpaceNormalBakeQueueDiagnostics m_Diagnostics{};
        std::size_t m_PendingIdentityBytes = 0u;
        std::uint64_t m_NextRequestGeneration = 1u;
    };
}
