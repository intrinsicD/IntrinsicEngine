module;

#include <cstdint>
#include <string>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
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
        Assets::AssetId GeneratedTextureAsset{};
        Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc RecordDesc{};
        std::uint64_t CacheGeneration{0u};
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

    [[nodiscard]] const char* DebugNameForRuntimeObjectSpaceNormalBakeGpuSubmitStatus(
        RuntimeObjectSpaceNormalBakeGpuSubmitStatus status) noexcept;

    [[nodiscard]] RuntimeObjectSpaceNormalBakeGpuSubmitResult
        BeginObjectSpaceNormalBakeGpuSubmission(
            Graphics::GpuAssetCache& gpuAssets,
            std::uint32_t stableEntityId,
            const RuntimeObjectSpaceNormalBakeSubmission& scheduled,
            const Graphics::ObjectSpaceNormalTextureBakePlan& plan);

    Core::Result MarkObjectSpaceNormalBakeGpuSubmissionReady(
        Graphics::GpuAssetCache& gpuAssets,
        const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket& ticket,
        std::uint64_t readyFrame);
}
