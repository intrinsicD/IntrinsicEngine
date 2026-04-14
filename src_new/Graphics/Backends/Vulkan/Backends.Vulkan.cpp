module Extrinsic.Backends.Vulkan;

import <cstdint>;
import <memory>;
import Extrinsic.Core.Config;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Vulkan
{
    class NullCommandContext final : public Extrinsic::RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
    };

    class VulkanDevice final : public Extrinsic::RHI::IDevice
    {
    public:
        void Initialize(Extrinsic::Platform::IWindow& window, const Extrinsic::Core::RenderConfig& config) override
        {
            (void)config;
            m_BackbufferExtent = window.GetExtent();
        }

        void Shutdown() override {}

        void WaitIdle() override {}

        bool BeginFrame(Extrinsic::RHI::FrameHandle& outFrame) override
        {
            outFrame.FrameIndex = m_FrameIndex++;
            outFrame.SwapchainImageIndex = 0;
            return true;
        }

        void EndFrame(const Extrinsic::RHI::FrameHandle& frame) override
        {
            (void)frame;
        }

        void Present(const Extrinsic::RHI::FrameHandle& frame) override
        {
            (void)frame;
        }

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            m_BackbufferExtent = {.Width = width, .Height = height};
        }

        Extrinsic::Platform::Extent2D GetBackbufferExtent() const override
        {
            return m_BackbufferExtent;
        }

        Extrinsic::RHI::ICommandContext& GetGraphicsContext(std::uint32_t frameIndex) override
        {
            (void)frameIndex;
            return m_CommandContext;
        }

    private:
        std::uint32_t m_FrameIndex{0};
        Extrinsic::Platform::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};
    };

    std::unique_ptr<Extrinsic::RHI::IDevice> CreateVulkanDevice()
    {
        return std::make_unique<VulkanDevice>();
    }
}
