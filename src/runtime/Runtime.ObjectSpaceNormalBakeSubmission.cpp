module;

#include <cstdint>
#include <string>
#include <utility>

module Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] RuntimeObjectSpaceNormalBakeGpuSubmitResult SubmitFail(
            const RuntimeObjectSpaceNormalBakeGpuSubmitStatus status,
            std::string diagnostic,
            const Core::ErrorCode cacheError = Core::ErrorCode::Success)
        {
            return RuntimeObjectSpaceNormalBakeGpuSubmitResult{
                .Status = status,
                .CacheError = cacheError,
                .Diagnostic = std::move(diagnostic),
            };
        }
    }

    const char* DebugNameForRuntimeObjectSpaceNormalBakeGpuSubmitStatus(
        const RuntimeObjectSpaceNormalBakeGpuSubmitStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::Submitted:
            return "Submitted";
        case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidStableEntity:
            return "InvalidStableEntity";
        case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidQueueSubmission:
            return "InvalidQueueSubmission";
        case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidPlan:
            return "InvalidPlan";
        case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::StalePlan:
            return "StalePlan";
        case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::CacheRejected:
            return "CacheRejected";
        }
        return "Unknown";
    }

    RuntimeObjectSpaceNormalBakeGpuSubmitResult
    BeginObjectSpaceNormalBakeGpuSubmission(
        Graphics::GpuAssetCache& gpuAssets,
        const std::uint32_t stableEntityId,
        const RuntimeObjectSpaceNormalBakeSubmission& scheduled,
        const Graphics::ObjectSpaceNormalTextureBakePlan& plan)
    {
        if (stableEntityId == 0u)
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidStableEntity,
                "object-space normal bake GPU submission has no stable render entity");
        }

        if (!scheduled.GeneratedTextureAsset.IsValid() ||
            !scheduled.StaleKey.Bake.GeneratedTextureAsset.IsValid() ||
            scheduled.StaleKey.Bake.Source.EntityKey == 0u ||
            scheduled.GeneratedTextureAsset !=
                scheduled.StaleKey.Bake.GeneratedTextureAsset)
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidQueueSubmission,
                "object-space normal bake GPU submission is missing a valid queued texture or entity key");
        }

        if (scheduled.StaleKey.Bake.Source.EntityKey !=
            static_cast<std::uint64_t>(stableEntityId))
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidStableEntity,
                "object-space normal bake GPU submission entity does not match target stable entity");
        }

        if (!plan.Succeeded())
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidPlan,
                "object-space normal bake GPU plan is not submittable");
        }

        if (plan.TextureRequest.Id != scheduled.GeneratedTextureAsset ||
            !Graphics::ObjectSpaceNormalTextureBakeCompletionKeyMatches(
                plan.CompletionKey,
                scheduled.StaleKey.Bake))
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::StalePlan,
                "object-space normal bake GPU plan does not match queued stale key");
        }

        auto pending = gpuAssets.BeginGpuProducedTexture(plan.TextureRequest);
        if (!pending.has_value())
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::CacheRejected,
                "object-space normal bake GPU-produced texture was rejected by GpuAssetCache",
                pending.error());
        }

        return RuntimeObjectSpaceNormalBakeGpuSubmitResult{
            .Status = RuntimeObjectSpaceNormalBakeGpuSubmitStatus::Submitted,
            .Ticket = RuntimeObjectSpaceNormalBakeGpuSubmissionTicket{
                .StableEntityId = stableEntityId,
                .Completion = scheduled.StaleKey,
                .GeneratedTextureAsset = scheduled.GeneratedTextureAsset,
                .RecordDesc =
                    Graphics::MakeObjectSpaceNormalTextureBakeGpuRecordDesc(
                        plan,
                        pending->Texture),
                .CacheGeneration = pending->Generation,
            },
            .Diagnostic = "object-space normal bake GPU submission prepared",
        };
    }

    Core::Result MarkObjectSpaceNormalBakeGpuSubmissionReady(
        Graphics::GpuAssetCache& gpuAssets,
        const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket& ticket,
        const std::uint64_t readyFrame)
    {
        if (!ticket.GeneratedTextureAsset.IsValid() ||
            ticket.Completion.Bake.Source.EntityKey == 0u)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        return gpuAssets.SetGpuProducedTextureReadyFrame(
            ticket.GeneratedTextureAsset,
            ticket.CacheGeneration,
            readyFrame);
    }
}
