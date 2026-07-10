module;

#include <cstddef>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeService;

import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue;
export import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;

export namespace Extrinsic::Runtime
{
    struct ObjectSpaceNormalBakeServiceDependencies
    {
        Graphics::GpuAssetCache* GpuAssets{};
        RenderExtractionCache* RenderExtraction{};
        RHI::IDevice* Device{};
    };

    class ObjectSpaceNormalBakeService
    {
    public:
        ObjectSpaceNormalBakeService() = default;

        ObjectSpaceNormalBakeService(
            const ObjectSpaceNormalBakeService&) = delete;
        ObjectSpaceNormalBakeService& operator=(
            const ObjectSpaceNormalBakeService&) = delete;
        ObjectSpaceNormalBakeService(ObjectSpaceNormalBakeService&&) = delete;
        ObjectSpaceNormalBakeService& operator=(
            ObjectSpaceNormalBakeService&&) = delete;

        void SetDependencies(ObjectSpaceNormalBakeServiceDependencies deps);
        void ClearDependencies();

        [[nodiscard]] GpuQueueParticipantHandle
            RegisterGpuQueueParticipant(JobService& jobs);

        [[nodiscard]] RuntimeObjectSpaceNormalBakeQueue& Queue() noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueue& Queue() const noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
            QueueDiagnostics() const noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;

    private:
        RuntimeObjectSpaceNormalBakeGpuQueue m_GpuQueue{};
        GpuQueueParticipantHandle m_ParticipantHandle{};
    };
}
