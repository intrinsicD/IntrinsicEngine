module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.CullingSystem;

import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Graphics
{
    struct CullingTag;
    using CullingHandle = Core::StrongHandle<CullingTag>;

    enum class CullingPhase : std::uint32_t
    {
        Phase1 = static_cast<std::uint32_t>(RHI::GpuCullPhase::Phase1),
        Phase2 = static_cast<std::uint32_t>(RHI::GpuCullPhase::Phase2),
    };

    struct GpuDrawBucketPhase
    {
        RHI::BufferHandle IndexedArgsBuffer{};
        RHI::BufferHandle NonIndexedArgsBuffer{};
        RHI::BufferHandle CountBuffer{};
        std::uint32_t Capacity = 0;
        bool Indexed = true;
    };

    struct GpuDrawBucket
    {
        RHI::BufferHandle IndexedArgsBuffer{};
        RHI::BufferHandle NonIndexedArgsBuffer{};
        RHI::BufferHandle CountBuffer{};
        RHI::BufferHandle DiagnosticsBuffer{};
        std::uint32_t Capacity = 0;
        bool Indexed = true;
        GpuDrawBucketPhase Phase1{};
        GpuDrawBucketPhase Phase2{};
    };

    struct CullingHZBDepthSample
    {
        float NearestDepth = 0.0f;
        float ConservativeMaxDepth = 1.0f;
        bool Valid = false;
    };

    enum class CullingTwoPhaseDecision : std::uint8_t
    {
        FrustumRejected,
        Phase1Visible,
        Phase1Rejected,
        Phase2Rescued,
        Phase2Rejected,
    };

    struct CullingTwoPhaseCandidate
    {
        RHI::GpuDrawBucketKind Bucket = RHI::GpuDrawBucketKind::SurfaceOpaque;
        bool FrustumVisible = true;
        CullingHZBDepthSample PreviousFrameHZB{};
        CullingHZBDepthSample CurrentFrameHZB{};
    };

    struct CullingTwoPhaseBucketCounters
    {
        std::uint32_t Phase1VisibleCount = 0;
        std::uint32_t Phase1RejectedCount = 0;
        std::uint32_t Phase2RescuedCount = 0;
    };

    struct CullingTwoPhasePartition
    {
        std::array<CullingTwoPhaseBucketCounters,
                   static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count)> Buckets{};
        std::vector<CullingTwoPhaseDecision> Decisions{};
        std::uint32_t FrustumRejectedCount = 0;
    };

    [[nodiscard]] bool HZBRejectsNearestDepth(CullingHZBDepthSample sample) noexcept;
    [[nodiscard]] CullingTwoPhasePartition ComputeTwoPhaseCullPartition(
        std::span<const CullingTwoPhaseCandidate> candidates);

    class CullingSystem
    {
    public:
        CullingSystem();
        ~CullingSystem();

        CullingSystem(const CullingSystem&)            = delete;
        CullingSystem& operator=(const CullingSystem&) = delete;

        [[nodiscard]] bool Initialize(RHI::IDevice&         device,
                        RHI::BufferManager&   bufferMgr,
                        RHI::PipelineManager& pipelineMgr,
                        std::string_view      cullShaderPath);

        void Shutdown();

        [[deprecated("Legacy path: use GpuWorld instance/bounds/render-flags + DispatchCull().")]]
        [[nodiscard]] CullingHandle Register(const RHI::BoundingSphere& sphere,
                                             const RHI::GpuDrawIndexedCommand&  drawTemplate);
        [[deprecated("Legacy path: use GpuWorld instance/bounds/render-flags + DispatchCull().")]]
        void Unregister(CullingHandle handle);
        [[deprecated("Legacy path: use GpuWorld::SetBounds() + DispatchCull().")]]
        void UpdateBounds(CullingHandle handle, const RHI::BoundingSphere& sphere);
        [[deprecated("Legacy path: draw templates are now generated from GpuWorld geometry records.")]]
        void SetDrawTemplate(CullingHandle handle, const RHI::GpuDrawIndexedCommand& cmd);

        void SyncGpuBuffer();
        void ResetCounters(RHI::ICommandContext& cmd);
        void DispatchCull(RHI::ICommandContext& cmd,
                          const RHI::CameraUBO& camera,
                          const GpuWorld&       gpuWorld);

        [[nodiscard]] const GpuDrawBucket& GetBucket(RHI::GpuDrawBucketKind kind) const;
        [[nodiscard]] GpuDrawBucketPhase GetBucketPhase(RHI::GpuDrawBucketKind kind,
                                                        CullingPhase phase) const;

        [[nodiscard]] RHI::BufferHandle GetDrawCommandBuffer()     const noexcept;
        [[nodiscard]] RHI::BufferHandle GetVisibilityCountBuffer() const noexcept;

        [[nodiscard]] std::uint32_t GetRegisteredCount() const noexcept;
        [[nodiscard]] std::uint32_t GetCapacity()        const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
