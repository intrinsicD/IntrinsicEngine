module;

#include <cstdint>

module Extrinsic.Runtime.ObjectSpaceNormalBakeService;

import Extrinsic.RHI.Device;

namespace Extrinsic::Runtime
{
    void ObjectSpaceNormalBakeService::SetDependencies(
        ObjectSpaceNormalBakeServiceDependencies deps)
    {
        RHI::IDevice* device = deps.Device;
        m_GpuQueue.SetDependencies(
            RuntimeObjectSpaceNormalBakeGpuQueueDependencies{
                .GpuAssets = deps.GpuAssets,
                .RenderExtraction = deps.RenderExtraction,
                .ReadyFrame =
                    [device]() -> std::uint64_t
                    {
                        return device != nullptr
                            ? device->GetGlobalFrameNumber() + 1u
                            : 0u;
                    },
            });
    }

    void ObjectSpaceNormalBakeService::ClearDependencies()
    {
        m_GpuQueue.SetDependencies({});
        m_ParticipantHandle = {};
    }

    GpuQueueParticipantHandle
    ObjectSpaceNormalBakeService::RegisterGpuQueueParticipant(JobService& jobs)
    {
        if (m_ParticipantHandle.IsValid())
            return m_ParticipantHandle;

        m_ParticipantHandle =
            jobs.RegisterGpuQueueParticipant(m_GpuQueue.MakeGpuQueueParticipantDesc());
        return m_ParticipantHandle;
    }

    RuntimeObjectSpaceNormalBakeQueue&
    ObjectSpaceNormalBakeService::Queue() noexcept
    {
        return m_GpuQueue.Queue();
    }

    const RuntimeObjectSpaceNormalBakeQueue&
    ObjectSpaceNormalBakeService::Queue() const noexcept
    {
        return m_GpuQueue.Queue();
    }

    const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
    ObjectSpaceNormalBakeService::QueueDiagnostics() const noexcept
    {
        return m_GpuQueue.Queue().Diagnostics();
    }

    std::size_t ObjectSpaceNormalBakeService::PendingCount() const noexcept
    {
        return m_GpuQueue.Queue().PendingCount();
    }
}
