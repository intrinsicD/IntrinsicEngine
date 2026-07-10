module;

#include <cstdint>
#include <functional>
#include <utility>

module Extrinsic.Runtime.JobServiceGpuQueueBridge;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Runtime
{
    void JobServiceGpuQueueBridge::Install(
        Graphics::IRenderer& renderer,
        JobService& jobs)
    {
        if (m_Hook.IsValid())
            return;

        m_Hook = renderer.RegisterRuntimeFrameCommandHook(
            [&jobs](RHI::ICommandContext& commandContext)
            {
                jobs.RecordGpuQueueFrameCommands(commandContext);
            });
    }

    void JobServiceGpuQueueBridge::Uninstall(
        Graphics::IRenderer* renderer) noexcept
    {
        if (renderer != nullptr && m_Hook.IsValid())
            renderer->UnregisterRuntimeFrameCommandHook(m_Hook);
        m_Hook = {};
    }

    std::uint64_t JobServiceGpuQueueBridge::ShutdownParticipants(
        Graphics::IRenderer* renderer,
        JobService& jobs,
        std::function<void()> waitForGpuIdle)
    {
        Uninstall(renderer);
        return jobs.ShutdownGpuQueueParticipants(std::move(waitForGpuIdle));
    }

    bool JobServiceGpuQueueBridge::IsInstalled() const noexcept
    {
        return m_Hook.IsValid();
    }
}
