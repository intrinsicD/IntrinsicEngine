module;

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Diagnostics;

export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Backends::Vulkan
{
    export constexpr std::uint32_t kMaxTimestampScopes = 256u;

    class VulkanDevice;

    export struct VulkanProfilerCommandContextView
    {
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        RHI::QueueAffinity Queue = RHI::QueueAffinity::Graphics;
        bool Owned = false;
        bool Recording = false;
        bool Primary = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Owned && CommandBuffer != VK_NULL_HANDLE;
        }
    };

    export using VulkanProfilerContextResolver =
        VulkanProfilerCommandContextView (*)(
            VulkanDevice&,
            RHI::ICommandContext&) noexcept;

    export using VulkanProfilerDeviceLostNotifier =
        void (*)(VulkanDevice&) noexcept;

    export struct VulkanProfilerCreateInfo
    {
        VkDevice Device = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        std::uint32_t FramesInFlight = 0u;
        std::uint32_t GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        bool AsyncComputeQueueAvailable = false;
        std::uint32_t AsyncComputeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        std::uint32_t EngineRequestedApiVersion = VK_API_VERSION_1_0;
        VulkanDevice* Owner = nullptr;
        VulkanProfilerContextResolver ResolveContext = nullptr;
        VulkanProfilerDeviceLostNotifier NotifyDeviceLost = nullptr;
    };

    export class VulkanProfiler final : public RHI::IProfiler
    {
    public:
        explicit VulkanProfiler(const VulkanProfilerCreateInfo& createInfo);
        ~VulkanProfiler() override;

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
        [[nodiscard]] std::uint32_t GetFramesInFlight() const override;

        // Called only from VulkanDevice immediately after the reused slot's
        // graphics/async/transfer fence set has completed.
        void NotifyFrameSlotComplete(std::uint32_t frameSlot) noexcept;
        void NotifyDeviceLost() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
