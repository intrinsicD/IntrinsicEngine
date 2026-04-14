module;

#include <memory>

module Extrinsic.Backends.Vulkan;

import Extrinsic.Core.Config.Render;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Vulkan
{
    class NullCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
    };

    class VulkanDevice final : public RHI::IDevice
    {
    public:
        void Initialize(Platform::IWindow& window, const Core::Config::RenderConfig& config) override
        {
            (void)config;
            m_BackbufferExtent = window.GetExtent();
        }

        void Shutdown() override {}

        void WaitIdle() override {}

        bool BeginFrame(RHI::FrameHandle& outFrame) override
        {
            outFrame.FrameIndex = m_FrameIndex++;
            outFrame.SwapchainImageIndex = 0;
            return true;
        }

        void EndFrame(const RHI::FrameHandle& frame) override
        {
            (void)frame;
        }

        void Present(const RHI::FrameHandle& frame) override
        {
            (void)frame;
        }

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            m_BackbufferExtent = {.Width = width, .Height = height};
        }

        Platform::Extent2D GetBackbufferExtent() const override
        {
            return m_BackbufferExtent;
        }

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t frameIndex) override
        {
            (void)frameIndex;
            return m_CommandContext;
        }

    private:
        std::uint32_t m_FrameIndex{0};
        Platform::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};
    };

    std::unique_ptr<RHI::IDevice> CreateVulkanDevice()
    {
        return std::make_unique<VulkanDevice>();
    }
}
