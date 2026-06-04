module;

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.CullingSystem;

import Extrinsic.Core.StrongHandle;
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

    struct CullSlot
    {
        RHI::BoundingSphere Sphere{};
        RHI::GpuDrawIndexedCommand DrawTemplate{};
        std::uint32_t       Generation = 0;
        bool                Live       = false;
    };

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

        std::vector<CullSlot>      Slots;
        std::vector<std::uint32_t> FreeList;
        std::uint32_t Capacity  = 0;
        std::uint32_t LiveCount = 0;

        bool AllocateBucket(RHI::GpuDrawBucketKind kind, const bool indexed, const std::uint32_t capacity)
        {
            auto& bucket = Buckets[ToIndex(kind)];

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
                    .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst,
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

        bool AllocateGpuBuffers(const std::uint32_t capacity)
        {
            const bool surfaceOk = AllocateBucket(RHI::GpuDrawBucketKind::SurfaceOpaque, true, capacity);
            const bool alphaOk   = AllocateBucket(RHI::GpuDrawBucketKind::SurfaceAlphaMask, true, capacity);
            const bool lineOk    = AllocateBucket(RHI::GpuDrawBucketKind::Lines, true, capacity);
            const bool pointOk   = AllocateBucket(RHI::GpuDrawBucketKind::Points, false, capacity);
            const bool shadowOk  = AllocateBucket(RHI::GpuDrawBucketKind::ShadowOpaque, true, capacity);
            const bool selectionSurfaceOk = AllocateBucket(RHI::GpuDrawBucketKind::SelectionSurface, true, capacity);
            const bool selectionLinesOk   = AllocateBucket(RHI::GpuDrawBucketKind::SelectionLines, true, capacity);
            const bool selectionPointsOk  = AllocateBucket(RHI::GpuDrawBucketKind::SelectionPoints, false, capacity);
            if (!(surfaceOk && alphaOk && lineOk && pointOk && shadowOk &&
                  selectionSurfaceOk && selectionLinesOk && selectionPointsOk))
            {
                return false;
            }

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

            CullBucketTableLease = std::move(*bucketTableOr);
            CullBucketTableBDA = Device->GetBufferDeviceAddress(CullBucketTableLease.GetHandle());
            Capacity = capacity;
            Slots.assign(capacity, CullSlot{});
            return true;
        }

        [[nodiscard]] CullSlot* Resolve(const CullingHandle h) noexcept
        {
            if (!h.IsValid() || h.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            auto& slot = Slots[h.Index];
            if (!slot.Live || slot.Generation != h.Generation) return nullptr;
            return &slot;
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
        m_Impl->Slots.clear();
        m_Impl->FreeList.clear();
        m_Impl->Capacity = 0;
        m_Impl->LiveCount = 0;
        m_Impl->Device = nullptr;
        m_Impl->BufferMgr = nullptr;
        m_Impl->PipelineMgr = nullptr;
    }

    CullingHandle CullingSystem::Register(const RHI::BoundingSphere& sphere,
                                          const RHI::GpuDrawIndexedCommand& drawTemplate)
    {
        assert(m_Impl->Device && "Register called before Initialize()");

        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        if (!m_Impl->FreeList.empty())
        {
            index = m_Impl->FreeList.back();
            m_Impl->FreeList.pop_back();
            generation = m_Impl->Slots[index].Generation;
        }
        else
        {
            assert(m_Impl->LiveCount < m_Impl->Capacity && "CullingSystem capacity exceeded");
            index = m_Impl->LiveCount;
        }

        auto& slot = m_Impl->Slots[index];
        slot.Sphere = sphere;
        slot.DrawTemplate = drawTemplate;
        slot.Generation = generation;
        slot.Live = true;
        ++m_Impl->LiveCount;
        return CullingHandle{index, generation};
    }

    void CullingSystem::Unregister(const CullingHandle handle)
    {
        auto* slot = m_Impl->Resolve(handle);
        if (!slot) return;

        slot->Live = false;
        ++slot->Generation;
        m_Impl->FreeList.push_back(handle.Index);
        --m_Impl->LiveCount;
    }

    void CullingSystem::UpdateBounds(const CullingHandle handle, const RHI::BoundingSphere& sphere)
    {
        auto* slot = m_Impl->Resolve(handle);
        if (!slot) return;
        slot->Sphere = sphere;
    }

    void CullingSystem::SetDrawTemplate(const CullingHandle handle, const RHI::GpuDrawIndexedCommand& cmd)
    {
        auto* slot = m_Impl->Resolve(handle);
        if (!slot) return;
        slot->DrawTemplate = cmd;
    }

    void CullingSystem::SyncGpuBuffer()
    {
        // Bucketed culling reads directly from GpuWorld SSBOs.
        // No CPU-authored cull input buffer remains.
    }

    void CullingSystem::ResetCounters(RHI::ICommandContext& cmd)
    {
        for (auto& bucket : m_Impl->Buckets)
        {
            for (const auto& phase : bucket.Phases)
            {
                const RHI::BufferHandle count = phase.CountLease.GetHandle();
                cmd.FillBuffer(count, 0, sizeof(std::uint32_t), 0);
                cmd.BufferBarrier(count,
                                  RHI::MemoryAccess::TransferWrite,
                                  RHI::MemoryAccess::ShaderWrite);
            }
            const RHI::BufferHandle diagnostics = bucket.DiagnosticsLease.GetHandle();
            cmd.FillBuffer(diagnostics, 0, sizeof(RHI::GpuCullBucketDiagnosticsCounters), 0);
            cmd.BufferBarrier(diagnostics,
                              RHI::MemoryAccess::TransferWrite,
                              RHI::MemoryAccess::ShaderWrite);
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
            [[maybe_unused]] const bool resized = m_Impl->AllocateGpuBuffers(gpuWorld.GetInstanceCapacity());
            assert(resized && "CullingSystem: failed to resize cull buckets to instance capacity");
            if (!resized)
            {
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

        const auto makeBucketPhases = [](const BucketStorage& bucket) -> RHI::GpuCullBucketPhases
        {
            const auto& phase1 = bucket.Phases[ToIndex(CullingPhase::Phase1)];
            const auto& phase2 = bucket.Phases[ToIndex(CullingPhase::Phase2)];
            return RHI::GpuCullBucketPhases{
                .Phase1 = MakeShaderPhase(phase1.ArgsBDA, phase1.CountBDA, bucket.Capacity),
                .Phase2 = MakeShaderPhase(phase2.ArgsBDA, phase2.CountBDA, bucket.Capacity),
                .Diagnostics = RHI::GpuCullBucketDiagnosticsOutput{.CountersBDA = bucket.DiagnosticsBDA},
            };
        };

        const auto& surface = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceOpaque)];
        const auto& alpha   = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceAlphaMask)];
        const auto& lines   = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::Lines)];
        const auto& points  = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::Points)];
        const auto& shadow  = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::ShadowOpaque)];
        const auto& selectionSurface = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SelectionSurface)];
        const auto& selectionLines   = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SelectionLines)];
        const auto& selectionPoints  = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SelectionPoints)];

        pc.InstanceCapacity = gpuWorld.GetInstanceCapacity();
        pc.CullPhase = static_cast<std::uint32_t>(RHI::GpuCullPhase::Phase1);
        pc.CullingFlags = RHI::GpuCullFlag_SelectionBucketOcclusionExempt;
        if (transition.SkipHZBPhase1)
        {
            pc.CullingFlags |= RHI::GpuCullFlag_HZBStaleSkip;
        }

        RHI::GpuCullBucketTable bucketTable{};
        bucketTable.SurfaceOpaque = makeBucketPhases(surface);
        bucketTable.SurfaceAlphaMask = makeBucketPhases(alpha);
        bucketTable.Lines = makeBucketPhases(lines);
        bucketTable.Points = makeBucketPhases(points);
        bucketTable.ShadowOpaque = makeBucketPhases(shadow);
        bucketTable.SelectionSurface = makeBucketPhases(selectionSurface);
        bucketTable.SelectionLines = makeBucketPhases(selectionLines);
        bucketTable.SelectionPoints = makeBucketPhases(selectionPoints);
        m_Impl->Device->WriteBuffer(m_Impl->CullBucketTableLease.GetHandle(), &bucketTable, sizeof(bucketTable), 0);
        cmd.BufferBarrier(m_Impl->CullBucketTableLease.GetHandle(),
                          RHI::MemoryAccess::TransferWrite,
                          RHI::MemoryAccess::ShaderRead);

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

    RHI::BufferHandle CullingSystem::GetDrawCommandBuffer() const noexcept
    {
        return m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceOpaque)].Bucket.IndexedArgsBuffer;
    }

    RHI::BufferHandle CullingSystem::GetVisibilityCountBuffer() const noexcept
    {
        return m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceOpaque)].Bucket.CountBuffer;
    }

    std::uint32_t CullingSystem::GetRegisteredCount() const noexcept
    {
        return m_Impl->LiveCount;
    }

    std::uint32_t CullingSystem::GetCapacity() const noexcept
    {
        return m_Impl->Capacity;
    }
}
