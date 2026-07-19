module;

#include <cstdint>
#include <expected>
#include <span>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Diagnostics;

export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Backends::Vulkan
{
    export constexpr uint32_t kMaxTimestampScopes = 256;

    export class VulkanProfiler final : public RHI::IProfiler
    {
    public:
        explicit VulkanProfiler(VkDevice device, VkPhysicalDevice physDevice,
                                 uint32_t framesInFlight);
        ~VulkanProfiler() override = default;

        [[nodiscard]] std::expected<RHI::ProfilerFramePlan, RHI::ProfilerError>
        BeginFrame(RHI::ProfilerFrameKey frame,
                   std::span<const RHI::ProfilerScopeDesc> scopes) override;
        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        BeginQueue(RHI::ICommandContext& context,
                   RHI::QueueAffinity queue) override;
        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        EndQueue(RHI::ICommandContext& context,
                 RHI::QueueAffinity queue) override;
        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        BeginScope(RHI::ICommandContext& context,
                   RHI::ProfilerScopeToken scope) override;
        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        EndScope(RHI::ICommandContext& context,
                 RHI::ProfilerScopeToken scope) override;
        [[nodiscard]] std::expected<void, RHI::ProfilerError>
        EndFrame(RHI::ProfilerFrameKey frame,
                 RHI::ProfilerFrameDisposition disposition) override;
        [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
        Resolve(RHI::ProfilerFrameKey frame) const override;
        [[nodiscard]] RHI::ProfilerStatusSnapshot GetStatus() const override;
        [[nodiscard]] uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

    private:
        VkDevice m_Device{VK_NULL_HANDLE};
        VkPhysicalDevice m_PhysicalDevice{VK_NULL_HANDLE};
        uint32_t m_FramesInFlight{0};
    };
}
