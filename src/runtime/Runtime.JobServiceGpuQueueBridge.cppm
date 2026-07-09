module;

#include <cstdint>
#include <functional>

export module Extrinsic.Runtime.JobServiceGpuQueueBridge;

import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.JobService;

export namespace Extrinsic::Runtime
{
    class JobServiceGpuQueueBridge
    {
    public:
        JobServiceGpuQueueBridge() = default;

        JobServiceGpuQueueBridge(const JobServiceGpuQueueBridge&) = delete;
        JobServiceGpuQueueBridge& operator=(const JobServiceGpuQueueBridge&) = delete;
        JobServiceGpuQueueBridge(JobServiceGpuQueueBridge&&) = delete;
        JobServiceGpuQueueBridge& operator=(JobServiceGpuQueueBridge&&) = delete;

        void Install(Graphics::IRenderer& renderer, JobService& jobs);
        void Uninstall(Graphics::IRenderer* renderer) noexcept;
        [[nodiscard]] std::uint64_t ShutdownParticipants(
            Graphics::IRenderer* renderer,
            JobService& jobs,
            std::function<void()> waitForGpuIdle = {});

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        Graphics::RuntimeFrameCommandHookHandle m_Hook{};
    };
}
