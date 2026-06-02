module Extrinsic.RHI.Device;

namespace Extrinsic::RHI
{
    DeviceCreateDesc MakeDeviceCreateDesc(const Core::Config::RenderConfig& renderConfig,
                                          const Core::Extent2D initialFramebufferExtent,
                                          void* const nativeWindowHandle) noexcept
    {
        return DeviceCreateDesc{
            .RenderConfig = renderConfig,
            .InitialFramebufferExtent = initialFramebufferExtent,
            .NativeWindowHandle = nativeWindowHandle,
        };
    }
}
