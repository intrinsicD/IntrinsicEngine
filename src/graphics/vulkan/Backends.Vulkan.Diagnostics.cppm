module;

#include <cstdint>
#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Diagnostics;

export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.Profiler;

namespace Extrinsic::Backends::Vulkan
{
    export constexpr uint32_t kMaxTimestampScopes = 256;

    export class VulkanProfiler final : public RHI::IProfiler
    {
    public:
        explicit VulkanProfiler(VkDevice device, VkPhysicalDevice physDevice,
                                 uint32_t framesInFlight);
        ~VulkanProfiler() override;

        void SetCommandBuffer(VkCommandBuffer cmd) { m_Cmd = cmd; }
        void ResetFrame(uint32_t frameIndex, VkCommandBuffer cmd);

        void BeginFrame(uint32_t frameIndex, uint32_t maxScopesHint) override;
        void EndFrame() override;
        [[nodiscard]] uint32_t BeginScope(std::string_view name) override;
        void EndScope(uint32_t scopeHandle) override;
        [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
            Resolve(uint32_t frameIndex) const override;
        [[nodiscard]] uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

    private:
        struct ScopeRecord { std::string Name; uint32_t BeginQuery, EndQuery; };
        struct FrameState
        {
            uint32_t FrameIndex  = 0;
            uint32_t QueryBase   = 0;
            uint32_t QueryCount  = 0;
            uint32_t FrameBegin  = ~0u;
            uint32_t FrameEnd    = ~0u;
            std::vector<ScopeRecord> Scopes;
        };

        VkDevice        m_Device    = VK_NULL_HANDLE;
        VkQueryPool     m_Pool      = VK_NULL_HANDLE;
        VkCommandBuffer m_Cmd       = VK_NULL_HANDLE;
        double          m_PeriodNs  = 1.0;
        bool            m_Supported = false;
        uint32_t        m_FramesInFlight;
        uint32_t        m_TotalQueries;
        std::vector<FrameState> m_Frames;
        mutable std::mutex m_Mutex;
    };
}

