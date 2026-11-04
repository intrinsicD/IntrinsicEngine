module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.CullingSystem;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Core.Logging;

namespace Extrinsic::Graphics
{
    void RecordOpaqueSurfaceBucket(
        RHI::ICommandContext& cmd,
        const RHI::PipelineHandle pipeline,
        const GpuWorld& gpuWorld,
        const GpuDrawBucket& bucket,
        const std::uint32_t frameIndex,
        const std::uint32_t maxDrawCount)
    {
        if (!bucket.Indexed || !bucket.IndexedArgsBuffer.IsValid() ||
            !bucket.CountBuffer.IsValid() || bucket.Capacity == 0u)
        {
            return;
        }

        cmd.BindPipeline(pipeline);
        cmd.BindIndexBuffer(gpuWorld.GetManagedIndexBuffer(), 0, RHI::IndexType::Uint32);

        RHI::GpuScenePushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.FrameIndex    = frameIndex;
        pc.DrawBucket    = static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SurfaceOpaque);
        cmd.PushConstants(&pc, sizeof(pc));

        cmd.DrawIndexedIndirectCount(
            bucket.IndexedArgsBuffer, 0, bucket.CountBuffer, 0,
            std::min(bucket.Capacity, maxDrawCount));
    }

    static constexpr std::uint32_t kInitialCapacity = 1024;

    bool HZBRejectsNearestDepth(const CullingHZBDepthSample sample) noexcept
    {
        return sample.Valid && sample.NearestDepth > sample.ConservativeMaxDepth;
    }

    CullingCameraTransitionDecision EvaluateCameraTransition(
        const std::optional<CullingCameraTransitionState> previous,
        const CullingCameraTransitionState current,
        const CullingCameraTransitionThresholds thresholds) noexcept
    {
        CullingCameraTransitionDecision decision{};
        decision.ExplicitTrigger = current.ExplicitCameraTransition;

        if (previous.has_value() && previous->Valid && current.Valid)
        {
            const glm::vec3 delta = current.Position - previous->Position;
            decision.PositionDelta = glm::length(delta);
            if (std::isfinite(decision.PositionDelta) &&
                thresholds.PositionDeltaThreshold > 0.0f &&
                decision.PositionDelta >= thresholds.PositionDeltaThreshold)
            {
                decision.PositionDeltaTrigger = true;
            }

            const float previousLen = glm::length(previous->Forward);
            const float currentLen = glm::length(current.Forward);
            if (std::isfinite(previousLen) && std::isfinite(currentLen) &&
                previousLen > 0.000001f && currentLen > 0.000001f)
            {
                const glm::vec3 previousForward = previous->Forward / previousLen;
                const glm::vec3 currentForward = current.Forward / currentLen;
                decision.DirectionDot = glm::clamp(glm::dot(previousForward, currentForward), -1.0f, 1.0f);
                if (std::isfinite(decision.DirectionDot) &&
                    thresholds.DirectionDotThreshold < 1.0f &&
                    decision.DirectionDot <= thresholds.DirectionDotThreshold)
                {
                    decision.DirectionDeltaTrigger = true;
                }
            }
        }

        decision.SkipHZBPhase1 = decision.ExplicitTrigger ||
                                 decision.PositionDeltaTrigger ||
                                 decision.DirectionDeltaTrigger;
        return decision;
    }

    CullingTwoPhasePartition ComputeTwoPhaseCullPartition(
        std::span<const CullingTwoPhaseCandidate> candidates,
        const CullingTwoPhaseOptions options)
    {
        CullingTwoPhasePartition partition{};
        partition.Decisions.reserve(candidates.size());
        for (const CullingTwoPhaseCandidate& candidate : candidates)
        {
            if (!candidate.FrustumVisible ||
                candidate.Bucket == RHI::GpuDrawBucketKind::Count)
            {
                partition.Decisions.push_back(CullingTwoPhaseDecision::FrustumRejected);
                ++partition.FrustumRejectedCount;
                continue;
            }

            CullingTwoPhaseBucketCounters& counters =
                partition.Buckets[static_cast<std::size_t>(candidate.Bucket)];
            if (options.HZBStaleSkip)
            {
                ++counters.Phase1VisibleCount;
                ++partition.HZBStaleVisibleCount;
                partition.Decisions.push_back(CullingTwoPhaseDecision::Phase1Visible);
                continue;
            }

            if (options.ExemptSelectionBuckets && RHI::IsSelectionDrawBucket(candidate.Bucket))
            {
                ++counters.Phase1VisibleCount;
                ++partition.SelectionOcclusionExemptCount;
                partition.Decisions.push_back(CullingTwoPhaseDecision::Phase1Visible);
                continue;
            }

            if (!HZBRejectsNearestDepth(candidate.PreviousFrameHZB))
            {
                ++counters.Phase1VisibleCount;
                partition.Decisions.push_back(CullingTwoPhaseDecision::Phase1Visible);
                continue;
            }

            ++counters.Phase1RejectedCount;
            if (!HZBRejectsNearestDepth(candidate.CurrentFrameHZB))
            {
                ++counters.Phase2RescuedCount;
                partition.Decisions.push_back(CullingTwoPhaseDecision::Phase2Rescued);
            }
            else
            {
                partition.Decisions.push_back(CullingTwoPhaseDecision::Phase2Rejected);
            }
        }
        return partition;
    }

    namespace
    {
        constexpr std::size_t ToIndex(const RHI::GpuDrawBucketKind kind) noexcept
        {
            return static_cast<std::size_t>(kind);
        }

        constexpr std::size_t ToIndex(const CullingPhase phase) noexcept
        {
            return static_cast<std::size_t>(phase);
        }

        void ExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) noexcept
        {
            auto row = [&](int r) -> glm::vec4
            {
                return {vp[0][r], vp[1][r], vp[2][r], vp[3][r]};
            };

            const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
            planes[0] = r3 + r0;
            planes[1] = r3 - r0;
            planes[2] = r3 + r1;
            planes[3] = r3 - r1;
            planes[4] = r2;
            planes[5] = r3 - r2;
        }

        [[nodiscard]] constexpr GpuDrawBucketPhase MakePublicPhase(const RHI::BufferHandle args,
                                                                    const RHI::BufferHandle count,
                                                                    const bool indexed,
                                                                    const std::uint32_t capacity) noexcept
        {
            return GpuDrawBucketPhase{
                .IndexedArgsBuffer = indexed ? args : RHI::BufferHandle{},
                .NonIndexedArgsBuffer = indexed ? RHI::BufferHandle{} : args,
                .CountBuffer = count,
                .Capacity = capacity,
                .Indexed = indexed,
            };
        }

        [[nodiscard]] constexpr RHI::GpuCullBucketOutput MakeShaderPhase(const std::uint64_t argsBda,
                                                                         const std::uint64_t countBda,
                                                                         const std::uint32_t capacity) noexcept
        {
            return RHI::GpuCullBucketOutput{
                .ArgsBDA = argsBda,
                .CountBDA = countBda,
                .Capacity = capacity,
            };
        }
    }

    struct BucketStorage
    {
        struct PhaseStorage
        {
            RHI::BufferManager::BufferLease ArgsLease{};
            RHI::BufferManager::BufferLease CountLease{};
            std::uint64_t ArgsBDA = 0;
            std::uint64_t CountBDA = 0;
        };

        GpuDrawBucket Bucket{};
        std::array<PhaseStorage, static_cast<std::size_t>(RHI::GpuCullPhase::Count)> Phases{};
        RHI::BufferManager::BufferLease DiagnosticsLease{};
        std::uint64_t DiagnosticsBDA = 0;
        bool Indexed = true;
        std::uint32_t Capacity = 0;
    };

    struct CullingSystem::Impl
    {
        RHI::IDevice*         Device      = nullptr;
        RHI::BufferManager*   BufferMgr   = nullptr;
        RHI::PipelineManager* PipelineMgr = nullptr;
        RHI::PipelineManager::PipelineLease CullPipeline;
        RHI::BufferManager::BufferLease CullBucketTableLease{};
        std::uint64_t CullBucketTableBDA = 0;
        std::optional<CullingCameraTransitionState> PreviousCamera{};
        CullingCameraTransitionThresholds TransitionThresholds{};
        CullingDiagnostics Diagnostics{};

        std::array<BucketStorage, static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count)> Buckets{};

        std::uint32_t Capacity  = 0;

        bool AllocateBucket(BucketStorage& bucket, const bool indexed, const std::uint32_t capacity)
        {
            const std::uint64_t indexedArgsBytes = capacity * sizeof(RHI::GpuDrawIndexedCommand);
            const std::uint64_t pointArgsBytes   = capacity * sizeof(RHI::GpuDrawCommand);
            const std::uint64_t countBytes       = sizeof(std::uint32_t);
            const auto allocatePhase = [&](const CullingPhase phase) -> bool
            {
                auto argsOr = BufferMgr->Create({
                    .SizeBytes   = indexed ? indexedArgsBytes : pointArgsBytes,
                    .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName   = indexed ? "CullBucketIndexedArgs.Phase" : "CullBucketPointArgs.Phase",
                });
                if (!argsOr.has_value()) return false;

                auto countOr = BufferMgr->Create({
                    .SizeBytes   = countBytes,
                    // TransferSrc: a count can be read back (the shadow synchronization smoke does).
                    .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst |
                                   RHI::BufferUsage::TransferSrc,
                    .HostVisible = false,
                    .DebugName   = "CullBucketCount.Phase",
                });
                if (!countOr.has_value()) return false;

                auto& phaseStorage = bucket.Phases[ToIndex(phase)];
                phaseStorage.ArgsLease = std::move(*argsOr);
                phaseStorage.CountLease = std::move(*countOr);
                phaseStorage.ArgsBDA = Device->GetBufferDeviceAddress(phaseStorage.ArgsLease.GetHandle());
                phaseStorage.CountBDA = Device->GetBufferDeviceAddress(phaseStorage.CountLease.GetHandle());
                return true;
            };

            if (!allocatePhase(CullingPhase::Phase1) || !allocatePhase(CullingPhase::Phase2))
            {
                return false;
            }

            auto diagnosticsOr = BufferMgr->Create({
                .SizeBytes = sizeof(RHI::GpuCullBucketDiagnosticsCounters),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName = "CullBucketDiagnostics",
            });
            if (!diagnosticsOr.has_value()) return false;

            bucket.DiagnosticsLease = std::move(*diagnosticsOr);
            bucket.DiagnosticsBDA = Device->GetBufferDeviceAddress(bucket.DiagnosticsLease.GetHandle());
            bucket.Indexed = indexed;
            bucket.Capacity = capacity;
            bucket.Bucket.Phase1 = MakePublicPhase(bucket.Phases[ToIndex(CullingPhase::Phase1)].ArgsLease.GetHandle(),
                                                   bucket.Phases[ToIndex(CullingPhase::Phase1)].CountLease.GetHandle(),
                                                   indexed,
                                                   capacity);
            bucket.Bucket.Phase2 = MakePublicPhase(bucket.Phases[ToIndex(CullingPhase::Phase2)].ArgsLease.GetHandle(),
                                                   bucket.Phases[ToIndex(CullingPhase::Phase2)].CountLease.GetHandle(),
                                                   indexed,
                                                   capacity);
            bucket.Bucket.IndexedArgsBuffer = bucket.Bucket.Phase1.IndexedArgsBuffer;
            bucket.Bucket.NonIndexedArgsBuffer = bucket.Bucket.Phase1.NonIndexedArgsBuffer;
            bucket.Bucket.CountBuffer = bucket.Bucket.Phase1.CountBuffer;
            bucket.Bucket.DiagnosticsBuffer = bucket.DiagnosticsLease.GetHandle();
            bucket.Bucket.Capacity = capacity;
            bucket.Bucket.Indexed = indexed;
            return true;
        }

        // All or nothing: the new buckets and their table replace the old ones together, so a failed growth keeps
        // a consistent set (the table is written only here).
        bool AllocateGpuBuffers(const std::uint32_t capacity)
        {
            std::array<BucketStorage, static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count)> next{};
            for (std::size_t i = 0; i < next.size(); ++i)
                if (!AllocateBucket(next[i], RHI::IsIndexedDrawBucket(static_cast<RHI::GpuDrawBucketKind>(i)), capacity))
                    return false;

            auto bucketTableOr = BufferMgr->Create({
                .SizeBytes   = sizeof(RHI::GpuCullBucketTable),
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "CullBucketTable",
            });
            if (!bucketTableOr.has_value())
            {
                return false;
            }

            Buckets = std::move(next);
            CullBucketTableLease = std::move(*bucketTableOr);
            CullBucketTableBDA = Device->GetBufferDeviceAddress(CullBucketTableLease.GetHandle());
            Capacity = capacity;
            // The table holds only these buffers' addresses and capacities, so it is written here, once per
            // allocation, not per frame (a device-local write waits for the graphics queue to go idle).
            const auto phases = [](const BucketStorage& bucket) -> RHI::GpuCullBucketPhases
            {
                const auto& phase1 = bucket.Phases[ToIndex(CullingPhase::Phase1)];
                const auto& phase2 = bucket.Phases[ToIndex(CullingPhase::Phase2)];
                return RHI::GpuCullBucketPhases{
                    .Phase1 = MakeShaderPhase(phase1.ArgsBDA, phase1.CountBDA, bucket.Capacity),
                    .Phase2 = MakeShaderPhase(phase2.ArgsBDA, phase2.CountBDA, bucket.Capacity),
                    .Diagnostics = RHI::GpuCullBucketDiagnosticsOutput{.CountersBDA = bucket.DiagnosticsBDA},
                };
            };
            const auto bucket = [&](const RHI::GpuDrawBucketKind kind) { return phases(Buckets[ToIndex(kind)]); };
            const RHI::GpuCullBucketTable table{
                .SurfaceOpaque = bucket(RHI::GpuDrawBucketKind::SurfaceOpaque),
                .SurfaceAlphaMask = bucket(RHI::GpuDrawBucketKind::SurfaceAlphaMask),
                .Lines = bucket(RHI::GpuDrawBucketKind::Lines),
                .LineQuads = bucket(RHI::GpuDrawBucketKind::LineQuads),
                .Points = bucket(RHI::GpuDrawBucketKind::Points),
                .ShadowOpaque = bucket(RHI::GpuDrawBucketKind::ShadowOpaque),
                .SelectionSurface = bucket(RHI::GpuDrawBucketKind::SelectionSurface),
                .SelectionLines = bucket(RHI::GpuDrawBucketKind::SelectionLines),
                .SelectionPoints = bucket(RHI::GpuDrawBucketKind::SelectionPoints),
            };
            Device->WriteBuffer(CullBucketTableLease.GetHandle(), &table, sizeof(table), 0);
            return true;
        }

    };

    CullingSystem::CullingSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    CullingSystem::~CullingSystem() = default;

    bool CullingSystem::Initialize(RHI::IDevice&         device,
                                   RHI::BufferManager&   bufferMgr,
                                   RHI::PipelineManager& pipelineMgr,
                                   std::string_view      cullShaderPath)
    {
        m_Impl->Device      = &device;
        m_Impl->BufferMgr   = &bufferMgr;
        m_Impl->PipelineMgr = &pipelineMgr;

        if (!m_Impl->AllocateGpuBuffers(kInitialCapacity))
        {
            Core::Log::Warn("[Graphics] CullingSystem GPU buffers unavailable; culling commands will be skipped");
            return false;
        }

        RHI::PipelineDesc cullDesc;
        cullDesc.ComputeShaderPath = std::string{cullShaderPath};
        cullDesc.PushConstantSize  = sizeof(RHI::GpuCullPushConstants);
        cullDesc.DebugName         = "CullComputeBuckets";

        if (auto pipelineOr = pipelineMgr.Create(cullDesc); pipelineOr.has_value())
        {
            m_Impl->CullPipeline = std::move(*pipelineOr);
            return true;
        }

        Core::Log::Warn("[Graphics] CullingSystem cull pipeline unavailable; culling commands will be skipped");
        return false;
    }

    void CullingSystem::Shutdown()
    {
        m_Impl->CullPipeline = {};
        for (auto& bucket : m_Impl->Buckets)
        {
            bucket.Bucket = {};
            for (auto& phase : bucket.Phases)
            {
                phase.ArgsLease = {};
                phase.CountLease = {};
                phase.ArgsBDA = 0;
                phase.CountBDA = 0;
            }
            bucket.DiagnosticsLease = {};
            bucket.DiagnosticsBDA = 0;
            bucket.Indexed = true;
            bucket.Capacity = 0;
        }
        m_Impl->CullBucketTableLease = {};
        m_Impl->CullBucketTableBDA = 0;
        m_Impl->PreviousCamera.reset();
        m_Impl->Diagnostics = {};
        m_Impl->Capacity = 0;
        m_Impl->Device = nullptr;
        m_Impl->BufferMgr = nullptr;
        m_Impl->PipelineMgr = nullptr;
    }

    void CullingSystem::ResetCounters(RHI::ICommandContext& cmd)
    {
        // The frames in flight share these buffers: a clear waits for the earlier frames' indirect reads and cull
        // atomics, and the cull's atomicAdd reads and writes the cleared value.
        const auto clear = [&](const RHI::BufferHandle buffer, const std::uint64_t bytes)
        {
            cmd.BufferBarrier(buffer,
                              RHI::MemoryAccess::IndirectRead | RHI::MemoryAccess::ShaderRead |
                                  RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::TransferWrite);
            cmd.FillBuffer(buffer, 0, bytes, 0);
            cmd.BufferBarrier(buffer,
                              RHI::MemoryAccess::TransferWrite,
                              RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        };
        for (auto& bucket : m_Impl->Buckets)
        {
            for (const auto& phase : bucket.Phases)
                clear(phase.CountLease.GetHandle(), sizeof(std::uint32_t));
            clear(bucket.DiagnosticsLease.GetHandle(), sizeof(RHI::GpuCullBucketDiagnosticsCounters));
        }
    }

    void CullingSystem::DispatchCull(RHI::ICommandContext& cmd,
                                     const RHI::CameraUBO& camera,
                                     const GpuWorld&       gpuWorld)
    {
        if (!m_Impl->CullPipeline.IsValid()) return;

        const auto pipe = m_Impl->PipelineMgr->GetDeviceHandle(m_Impl->CullPipeline.GetHandle());
        if (!pipe.IsValid()) return;

        bool resizedBuckets = false;
        if (gpuWorld.GetInstanceCapacity() > m_Impl->Capacity)
        {
            if (!m_Impl->AllocateGpuBuffers(gpuWorld.GetInstanceCapacity()))
            {
                Core::Log::Warn("[Graphics] CullingSystem could not grow its buckets to the instance capacity; "
                                "culling skipped this frame");
                return;
            }
            resizedBuckets = true;
        }

        if (resizedBuckets)
        {
            ResetCounters(cmd);
        }

        RHI::GpuCullPushConstants pc{};
        ExtractFrustumPlanes(camera.ViewProj, pc.FrustumPlanes);
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.CullBucketTableBDA = m_Impl->CullBucketTableBDA;

        const CullingCameraTransitionState currentCamera{
            .Position = glm::vec3{camera.CameraPosition},
            .Forward = glm::vec3{camera.CameraDirection},
            .Valid = true,
            .ExplicitCameraTransition =
                (camera.CullingFlags & RHI::CameraCulling_ExplicitTransition) != 0u,
        };
        const CullingCameraTransitionDecision transition =
            EvaluateCameraTransition(m_Impl->PreviousCamera,
                                     currentCamera,
                                     m_Impl->TransitionThresholds);
        m_Impl->PreviousCamera = currentCamera;
        m_Impl->Diagnostics.LastHzbStaleSkip = transition.SkipHZBPhase1;
        m_Impl->Diagnostics.LastExplicitCameraTransition = transition.ExplicitTrigger;
        m_Impl->Diagnostics.LastCameraPositionDeltaTransition = transition.PositionDeltaTrigger;
        m_Impl->Diagnostics.LastCameraDirectionDeltaTransition = transition.DirectionDeltaTrigger;
        if (transition.SkipHZBPhase1)
        {
            ++m_Impl->Diagnostics.HzbStaleSkipCount;
        }

        pc.InstanceCapacity = gpuWorld.GetInstanceCapacity();
        pc.CullPhase = static_cast<std::uint32_t>(RHI::GpuCullPhase::Phase1);
        pc.CullingFlags = RHI::GpuCullFlag_SelectionBucketOcclusionExempt;
        if (transition.SkipHZBPhase1)
        {
            pc.CullingFlags |= RHI::GpuCullFlag_HZBStaleSkip;
        }

        cmd.BufferBarrier(m_Impl->CullBucketTableLease.GetHandle(),
                          RHI::MemoryAccess::TransferWrite,
                          RHI::MemoryAccess::ShaderRead);
        // The earlier frames' draws still read these arguments (the render graph imports only some of them).
        for (const auto& bucket : m_Impl->Buckets)
            for (const auto& phase : bucket.Phases)
                cmd.BufferBarrier(phase.ArgsLease.GetHandle(),
                                  RHI::MemoryAccess::IndirectRead,
                                  RHI::MemoryAccess::ShaderWrite);

        cmd.BindPipeline(pipe);
        cmd.PushConstants(&pc, sizeof(pc));

        const std::uint32_t groups = (gpuWorld.GetInstanceCapacity() + RHI::kGpuCullDispatchGroupSize - 1) /
                                     RHI::kGpuCullDispatchGroupSize;
        if (groups > 0)
        {
            cmd.Dispatch(groups, 1, 1);
        }

        for (auto& bucket : m_Impl->Buckets)
        {
            for (const auto& phase : bucket.Phases)
            {
                cmd.BufferBarrier(phase.ArgsLease.GetHandle(),
                                  RHI::MemoryAccess::ShaderWrite,
                                  RHI::MemoryAccess::IndirectRead);
                cmd.BufferBarrier(phase.CountLease.GetHandle(),
                                  RHI::MemoryAccess::ShaderWrite,
                                  RHI::MemoryAccess::IndirectRead);
            }
            cmd.BufferBarrier(bucket.DiagnosticsLease.GetHandle(),
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
        }
    }

    const GpuDrawBucket& CullingSystem::GetBucket(const RHI::GpuDrawBucketKind kind) const
    {
        return m_Impl->Buckets[ToIndex(kind)].Bucket;
    }

    GpuDrawBucketPhase CullingSystem::GetBucketPhase(const RHI::GpuDrawBucketKind kind,
                                                     const CullingPhase phase) const
    {
        const GpuDrawBucket& bucket = GetBucket(kind);
        return phase == CullingPhase::Phase2 ? bucket.Phase2 : bucket.Phase1;
    }

    CullingDiagnostics CullingSystem::GetDiagnostics() const noexcept
    {
        return m_Impl->Diagnostics;
    }
}
