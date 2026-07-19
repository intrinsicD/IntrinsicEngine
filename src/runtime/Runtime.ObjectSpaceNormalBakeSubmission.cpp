module;

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

module Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.Descriptors;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] constexpr RuntimeObjectSpaceNormalBakeResidencyResult
        ResidencyFail(
            const RuntimeObjectSpaceNormalBakeResidencyStatus status,
            const char* diagnostic) noexcept
        {
            return RuntimeObjectSpaceNormalBakeResidencyResult{
                .Status = status,
                .Diagnostic = diagnostic,
            };
        }

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

    RuntimeObjectSpaceNormalBakeResidencyResult
    ValidateObjectSpaceNormalBakeResidency(
        const RuntimeObjectSpaceNormalBakeIdentity& identity,
        const Graphics::GpuGeometryResidencyView& residency,
        const std::optional<std::uint64_t> expectedContentRevision) noexcept
    {
        if (residency.ContentRevision == 0u ||
            (expectedContentRevision.has_value() &&
             *expectedContentRevision == 0u))
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    InvalidContentRevision,
                "object-space normal bake residency has no valid content revision");
        }

        if (expectedContentRevision.has_value() &&
            residency.ContentRevision != *expectedContentRevision)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    StaleContentRevision,
                "object-space normal bake residency content revision is stale");
        }

        const bool fingerprintMismatch =
            identity.PositionFingerprint == 0u ||
            identity.SurfaceIndexFingerprint == 0u ||
            identity.TexcoordFingerprint == 0u ||
            identity.NormalFingerprint == 0u ||
            residency.PositionFingerprint == 0u ||
            residency.SurfaceIndexFingerprint == 0u ||
            residency.TexcoordFingerprint == 0u ||
            residency.NormalFingerprint == 0u ||
            residency.PositionFingerprint != identity.PositionFingerprint ||
            residency.SurfaceIndexFingerprint !=
                identity.SurfaceIndexFingerprint ||
            residency.TexcoordFingerprint != identity.TexcoordFingerprint ||
            residency.NormalFingerprint != identity.NormalFingerprint;
        const bool sizeMismatch =
            residency.PositionByteCount !=
                static_cast<std::uint64_t>(
                    identity.PackedPositionBytes.size()) ||
            residency.SurfaceIndexByteCount !=
                static_cast<std::uint64_t>(
                    identity.SurfaceIndexBytes.size()) ||
            residency.TexcoordByteCount !=
                static_cast<std::uint64_t>(
                    identity.ResolvedTexcoordBytes.size()) ||
            residency.NormalByteCount !=
                static_cast<std::uint64_t>(
                    identity.ResolvedNormalBytes.size()) ||
            residency.VertexCount != identity.VertexCount ||
            residency.SurfaceIndexCount != identity.SurfaceIndexCount;
        if (fingerprintMismatch || sizeMismatch)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::IdentityMismatch,
                "object-space normal bake residency does not match the exact identity");
        }

        if (residency.StorageLane !=
            Graphics::GpuWorld::GeometryStorageLane::UniformSoA)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    UnsupportedStorageLane,
                "object-space normal bake residency is not in the uniform SoA lane");
        }

        if (residency.PositionFormat != RHI::Format::RGB32_FLOAT ||
            residency.PositionElementBytes != 12u ||
            residency.PositionStrideBytes != 12u)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    InvalidPositionLayout,
                "object-space normal bake position residency is not tightly packed RGB32_FLOAT");
        }

        if (residency.TexcoordFormat != RHI::Format::RG32_FLOAT ||
            residency.TexcoordElementBytes != 8u ||
            residency.TexcoordStrideBytes != 8u ||
            (residency.Record.TexcoordBufferBDA %
             Graphics::kObjectSpaceNormalBakeTexcoordAddressAlignment) != 0u)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    InvalidTexcoordLayout,
                "object-space normal bake texcoord residency is not tightly packed or eight-byte-aligned RG32_FLOAT");
        }

        if (residency.NormalFormat != RHI::Format::RGB32_FLOAT ||
            residency.NormalElementBytes != 12u ||
            residency.NormalStrideBytes != 12u ||
            (residency.Record.NormalBufferBDA %
             Graphics::kObjectSpaceNormalBakeNormalAddressAlignment) != 0u)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    InvalidNormalLayout,
                "object-space normal bake normal residency is not tightly packed or four-byte-aligned RGB32_FLOAT");
        }

        if (residency.SurfaceIndexFormat != RHI::Format::R32_UINT ||
            residency.SurfaceIndexElementBytes != 4u ||
            residency.SurfaceIndexStrideBytes != 4u)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    InvalidSurfaceIndexLayout,
                "object-space normal bake index residency is not tightly packed R32_UINT");
        }

        if (!residency.IndexBuffer.IsValid())
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    MissingManagedIndexBuffer,
                "object-space normal bake residency has no managed index buffer");
        }

        if (residency.Record.TexcoordBufferBDA == 0u ||
            residency.Record.NormalBufferBDA == 0u)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    MissingChannelAddress,
                "object-space normal bake residency has no texcoord or normal device address");
        }

        if (residency.Record.VertexCount != residency.VertexCount ||
            residency.Record.SurfaceIndexCount !=
                residency.SurfaceIndexCount ||
            residency.Record.SurfaceIndexCount == 0u)
        {
            return ResidencyFail(
                RuntimeObjectSpaceNormalBakeResidencyStatus::
                    InvalidRecordCounts,
                "object-space normal bake residency record counts do not match retained content");
        }

        return RuntimeObjectSpaceNormalBakeResidencyResult{
            .Status =
                RuntimeObjectSpaceNormalBakeResidencyStatus::Compatible,
            .Diagnostic =
                "object-space normal bake residency matches the exact identity",
        };
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

    Graphics::ObjectSpaceNormalTextureBakeCompletionKey
    MakeObjectSpaceNormalBakeGraphicsCompletionKey(
        const RuntimeObjectSpaceNormalBakeSubmission& scheduled) noexcept
    {
        if (!scheduled.Identity.has_value())
            return {};

        const RuntimeObjectSpaceNormalBakeIdentity& identity =
            *scheduled.Identity;
        return Graphics::ObjectSpaceNormalTextureBakeCompletionKey{
            .GeneratedTextureAsset = scheduled.GeneratedTextureAsset,
            .Source =
                Graphics::ObjectSpaceNormalTextureBakeSourceKey{
                    .EntityKey = scheduled.Target.StableEntityId,
                    .GeometryGeneration =
                        identity.GeometryFingerprint,
                    .TexcoordGeneration =
                        identity.TexcoordFingerprint,
                    .NormalGeneration =
                        identity.NormalFingerprint,
                },
            .Width = identity.Width,
            .Height = identity.Height,
            .PaddingTexels = identity.PaddingTexels,
            .Space = identity.Space,
        };
    }

    RuntimeObjectSpaceNormalBakeGpuSubmitResult
    BeginObjectSpaceNormalBakeGpuSubmission(
        Graphics::GpuAssetCache& gpuAssets,
        const RuntimeObjectSpaceNormalBakeSubmission& scheduled,
        const Graphics::ObjectSpaceNormalTextureBakePlan& plan,
        const std::uint64_t geometryContentRevision)
    {
        if (!IsValidRuntimeObjectSpaceNormalBakeTarget(scheduled.Target))
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidStableEntity,
                "object-space normal bake GPU submission target is invalid");
        }

        if (!scheduled.GeneratedTextureAsset.IsValid() ||
            !scheduled.Identity.has_value() ||
            scheduled.StaleKey.RequestGeneration == 0u ||
            scheduled.StaleKey.Target != scheduled.Target ||
            geometryContentRevision == 0u)
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidQueueSubmission,
                "object-space normal bake GPU submission is missing an exact identity, generated asset, request generation, or content revision");
        }

        if (!plan.Succeeded())
        {
            return SubmitFail(
                RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidPlan,
                "object-space normal bake GPU plan is not submittable");
        }

        const Graphics::ObjectSpaceNormalTextureBakeCompletionKey expected =
            MakeObjectSpaceNormalBakeGraphicsCompletionKey(scheduled);
        if (plan.TextureRequest.Id != scheduled.GeneratedTextureAsset ||
            !Graphics::ObjectSpaceNormalTextureBakeCompletionKeyMatches(
                plan.CompletionKey,
                expected))
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
                .StableEntityId = scheduled.Target.StableEntityId,
                .Completion = scheduled.StaleKey,
                .Identity = *scheduled.Identity,
                .GeneratedTextureAsset = scheduled.GeneratedTextureAsset,
                .RecordDesc =
                    Graphics::MakeObjectSpaceNormalTextureBakeGpuRecordDesc(
                        plan,
                        pending->Texture),
                .CacheGeneration = pending->Generation,
                .GeometryContentRevision = geometryContentRevision,
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
            ticket.Completion.RequestGeneration == 0u ||
            ticket.GeometryContentRevision == 0u)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        return gpuAssets.SetGpuProducedTextureReadyFrame(
            ticket.GeneratedTextureAsset,
            ticket.CacheGeneration,
            readyFrame);
    }
}
