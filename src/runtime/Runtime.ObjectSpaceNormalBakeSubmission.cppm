module;

#include <cstdint>
#include <optional>
#include <string>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

export namespace Extrinsic::Runtime
{
    enum class RuntimeObjectSpaceNormalBakeGpuSubmitStatus : std::uint8_t
    {
        Submitted,
        InvalidStableEntity,
        InvalidQueueSubmission,
        InvalidPlan,
        StalePlan,
        CacheRejected,
    };

    struct RuntimeObjectSpaceNormalBakeGpuSubmissionTicket
    {
        std::uint32_t StableEntityId{0u};
        RuntimeObjectSpaceNormalBakeStaleKey Completion{};
        RuntimeObjectSpaceNormalBakeIdentity Identity{};
        Assets::AssetId GeneratedTextureAsset{};
        Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc RecordDesc{};
        std::uint64_t CacheGeneration{0u};
        std::uint64_t GeometryContentRevision{0u};
    };

    struct RuntimeObjectSpaceNormalBakeGpuSubmitResult
    {
        RuntimeObjectSpaceNormalBakeGpuSubmitStatus Status{
            RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidQueueSubmission};
        RuntimeObjectSpaceNormalBakeGpuSubmissionTicket Ticket{};
        Core::ErrorCode CacheError{Core::ErrorCode::Success};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeObjectSpaceNormalBakeGpuSubmitStatus::Submitted;
        }
    };

    enum class RuntimeObjectSpaceNormalBakeResidencyStatus : std::uint8_t
    {
        Compatible,
        InvalidContentRevision,
        StaleContentRevision,
        IdentityMismatch,
        UnsupportedStorageLane,
        InvalidPositionLayout,
        InvalidTexcoordLayout,
        InvalidNormalLayout,
        InvalidSurfaceIndexLayout,
        MissingManagedIndexBuffer,
        MissingChannelAddress,
        InvalidRecordCounts,
    };

    struct RuntimeObjectSpaceNormalBakeResidencyResult
    {
        RuntimeObjectSpaceNormalBakeResidencyStatus Status{
            RuntimeObjectSpaceNormalBakeResidencyStatus::IdentityMismatch};
        const char* Diagnostic{
            "object-space normal bake residency was not validated"};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status ==
                   RuntimeObjectSpaceNormalBakeResidencyStatus::Compatible;
        }
    };

    [[nodiscard]] RuntimeObjectSpaceNormalBakeResidencyResult
        ValidateObjectSpaceNormalBakeResidency(
            const RuntimeObjectSpaceNormalBakeIdentity& identity,
            const Graphics::GpuGeometryResidencyView& residency,
            std::optional<std::uint64_t> expectedContentRevision =
                std::nullopt) noexcept;

    [[nodiscard]] const char* DebugNameForRuntimeObjectSpaceNormalBakeGpuSubmitStatus(
        RuntimeObjectSpaceNormalBakeGpuSubmitStatus status) noexcept;

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakeCompletionKey
        MakeObjectSpaceNormalBakeGraphicsCompletionKey(
            const RuntimeObjectSpaceNormalBakeSubmission& scheduled) noexcept;

    [[nodiscard]] RuntimeObjectSpaceNormalBakeGpuSubmitResult
        BeginObjectSpaceNormalBakeGpuSubmission(
            Graphics::GpuAssetCache& gpuAssets,
            const RuntimeObjectSpaceNormalBakeSubmission& scheduled,
            const Graphics::ObjectSpaceNormalTextureBakePlan& plan,
            std::uint64_t geometryContentRevision);

    Core::Result MarkObjectSpaceNormalBakeGpuSubmissionReady(
        Graphics::GpuAssetCache& gpuAssets,
        const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket& ticket,
        std::uint64_t readyFrame);
}
