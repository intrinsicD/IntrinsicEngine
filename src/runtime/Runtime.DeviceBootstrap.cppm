module;

#include <memory>

export module Extrinsic.Runtime.DeviceBootstrap;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Device;

export namespace Extrinsic::Runtime
{
    struct RuntimeDeviceSelection
    {
        bool UsePromotedVulkanDevice{false};
        bool FallsBackToNullDevice{true};
    };

    [[nodiscard]] RuntimeDeviceSelection SelectRuntimeDeviceBackend(
        const Core::Config::RenderConfig& config,
        bool promotedVulkanAvailable) noexcept;

    [[nodiscard]] bool ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
        const Core::Config::RenderConfig& config,
        bool isDeviceOperational) noexcept;

    [[nodiscard]] std::unique_ptr<RHI::IDevice> CreateRuntimeDevice(
        const Core::Config::RenderConfig& config);

    [[nodiscard]] Core::Result InitializeRuntimeGpuAssetFallbackTexture(
        Graphics::GpuAssetCache& cache,
        const RHI::IDevice& device);
}
