module;

#include <cstddef>
#include <cstdint>
#include <memory>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeService;

import Extrinsic.Asset.Service;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.JobService;
export import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] std::uint64_t ObjectSpaceNormalBakeReadyFrame(
        std::uint64_t issueFrame,
        std::uint32_t framesInFlight) noexcept;

    struct ObjectSpaceNormalBakeServiceDependencies
    {
        Assets::AssetService* Assets{};
        Graphics::GpuAssetCache* GpuAssets{};
        Graphics::IRenderer* Renderer{};
        RenderExtractionCache* RenderExtraction{};
        RHI::IDevice* Device{};
    };

    class ObjectSpaceNormalBakeService;

    struct ObjectSpaceNormalBakeServiceTestHooks
    {
        bool EnableDeterministicPlan{false};
        bool InvalidateFirstRecord{false};
        bool RejectFirstReadyPublication{false};
        std::uint64_t IdentityDigestOverride{0u};
    };

    struct ObjectSpaceNormalBakeServiceTestDiagnostics
    {
        std::uint64_t PendingStaleDiscards{0u};
        std::uint64_t CacheRejected{0u};
        std::uint64_t RecordedSubmissions{0u};
        std::uint64_t RecordFailures{0u};
        std::uint64_t ReadyFrameFailures{0u};
        std::uint64_t WaitingBindings{0u};
        std::uint64_t BoundCompletions{0u};
        std::uint64_t LastRecordAttempted{0u};
        std::uint64_t LastRecordSubmitted{0u};
        std::uint64_t LastDrainProcessed{0u};
        std::uint64_t LastDrainBound{0u};
        std::uint64_t AllocatedAssets{0u};
        std::uint64_t PendingIdentityReuses{0u};
        std::uint64_t ProvenReadyReuses{0u};
        std::uint64_t CapacityRejected{0u};
    };

    void SetObjectSpaceNormalBakeServiceTestHooks(
        ObjectSpaceNormalBakeService& service,
        ObjectSpaceNormalBakeServiceTestHooks hooks);
    [[nodiscard]] ObjectSpaceNormalBakeServiceTestDiagnostics
        GetObjectSpaceNormalBakeServiceTestDiagnostics(
            const ObjectSpaceNormalBakeService& service) noexcept;

    class ObjectSpaceNormalBakeService
    {
    public:
        ObjectSpaceNormalBakeService();
        ~ObjectSpaceNormalBakeService();

        ObjectSpaceNormalBakeService(
            const ObjectSpaceNormalBakeService&) = delete;
        ObjectSpaceNormalBakeService& operator=(
            const ObjectSpaceNormalBakeService&) = delete;
        ObjectSpaceNormalBakeService(ObjectSpaceNormalBakeService&&) = delete;
        ObjectSpaceNormalBakeService& operator=(
            ObjectSpaceNormalBakeService&&) = delete;

        void SetDependencies(ObjectSpaceNormalBakeServiceDependencies deps);
        void ClearDependencies();
        void SetTargetScene(
            WorldHandle world,
            std::uint64_t bindingEpoch,
            ECS::Scene::Registry* scene) noexcept;
        void DetachTargets(WorldHandle world, std::uint64_t bindingEpoch);
        void PrepareScheduledRequests();

        [[nodiscard]] GpuQueueParticipantHandle
            RegisterGpuQueueParticipant(JobService& jobs);

        [[nodiscard]] RuntimeObjectSpaceNormalBakeQueue& Queue() noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueue& Queue() const noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
            QueueDiagnostics() const noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;

    private:
        friend void SetObjectSpaceNormalBakeServiceTestHooks(
            ObjectSpaceNormalBakeService& service,
            ObjectSpaceNormalBakeServiceTestHooks hooks);
        friend ObjectSpaceNormalBakeServiceTestDiagnostics
            GetObjectSpaceNormalBakeServiceTestDiagnostics(
                const ObjectSpaceNormalBakeService& service) noexcept;

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
