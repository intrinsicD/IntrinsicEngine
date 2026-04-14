module Extrinsic.Backends.Vulkan.Device;

import <memory>;
import Extrinsic.Core.Config;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Vulkan
{
	class VulkanDevice final : public Extrinsic::RHI::IDevice
	{
	public:
		void Initialize(Extrinsic::Platform::IWindow& window, const Extrinsic::Core::RenderConfig& config) override;
		void Shutdown() override;

		void WaitIdle() override;

		bool BeginFrame(Extrinsic::RHI::FrameHandle& outFrame) override;
		void EndFrame(const Extrinsic::RHI::FrameHandle& frame) override;
		void Present(const Extrinsic::RHI::FrameHandle& frame) override;

		void Resize(std::uint32_t width, std::uint32_t height) override;
		Extrinsic::Platform::Extent2D GetBackbufferExtent() const override;

		Extrinsic::RHI::ICommandContext& GetGraphicsContext(std::uint32_t frameIndex) override;

	private:
		// Vulkan state stays hidden here.
	};

	std::unique_ptr<Extrinsic::RHI::IDevice> CreateVulkanDevice()
	{
		return std::make_unique<VulkanDevice>();
	}
}
