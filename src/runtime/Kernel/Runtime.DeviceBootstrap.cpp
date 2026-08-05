module;

#include <array>
#include <cstddef>
#include <memory>
#include <span>

module Extrinsic.Runtime.DeviceBootstrap;

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
import Extrinsic.Backends.Vulkan;
#endif
import Extrinsic.Backends.Null;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Error;
import Extrinsic.Core.Logging;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

namespace Extrinsic::Runtime
{
    namespace
    {
        // RUNTIME-070: deterministic runtime-owned fallback texture bytes for
        // GpuAssetCache. The descriptor is private to the bootstrap module so
        // Engine only owns composition order.
        consteval std::array<std::byte, 4 * 4 * 4> MakeFallbackTextureBytes() noexcept
        {
            std::array<std::byte, 4 * 4 * 4> bytes{};
            for (std::size_t y = 0; y < 4; ++y)
            {
                for (std::size_t x = 0; x < 4; ++x)
                {
                    const bool magenta = (((x / 2) ^ (y / 2)) & 1u) == 0u;
                    const std::size_t base = (y * 4 + x) * 4;
                    bytes[base + 0] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 1] = static_cast<std::byte>(0x00);
                    bytes[base + 2] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 3] = static_cast<std::byte>(0xFF);
                }
            }
            return bytes;
        }

        constexpr auto kFallbackTextureBytes = MakeFallbackTextureBytes();

        [[nodiscard]] Graphics::GpuTextureFallbackDesc BuildFallbackTextureDesc() noexcept
        {
            Graphics::GpuTextureFallbackDesc desc{};
            desc.Bytes = std::span<const std::byte>(kFallbackTextureBytes);
            desc.Desc.Width = 4;
            desc.Desc.Height = 4;
            desc.Desc.MipLevels = 1;
            desc.Desc.Fmt = RHI::Format::RGBA8_UNORM;
            desc.Desc.Usage =
                RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
            desc.Desc.DebugName = "gpu-asset-fallback-texture";
            desc.SamplerDesc.MagFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MinFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MipFilter = RHI::MipmapMode::Nearest;
            desc.SamplerDesc.AddressU = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressV = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressW = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.DebugName = "gpu-asset-fallback-sampler";
            return desc;
        }

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
        constexpr bool kPromotedVulkanAvailable = true;
#else
        constexpr bool kPromotedVulkanAvailable = false;
#endif
    }

    RuntimeDeviceSelection SelectRuntimeDeviceBackend(
        const Core::Config::RenderConfig& config,
        const bool promotedVulkanAvailable) noexcept
    {
        switch (config.Backend)
        {
        case Core::Config::GraphicsBackend::Vulkan:
            if (config.EnablePromotedVulkanDevice && promotedVulkanAvailable)
            {
                return RuntimeDeviceSelection{
                    .UsePromotedVulkanDevice = true,
                    .FallsBackToNullDevice = false,
                };
            }
            return RuntimeDeviceSelection{};
        }
        return RuntimeDeviceSelection{};
    }

    bool ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
        const Core::Config::RenderConfig& config,
        const bool isDeviceOperational) noexcept
    {
        if (config.Backend != Core::Config::GraphicsBackend::Vulkan)
            return false;
        if (!config.EnablePromotedVulkanDevice)
            return false;
        return !isDeviceOperational;
    }

    std::unique_ptr<RHI::IDevice> CreateRuntimeDevice(
        const Core::Config::RenderConfig& config)
    {
        const RuntimeDeviceSelection selection =
            SelectRuntimeDeviceBackend(config, kPromotedVulkanAvailable);
        if (selection.UsePromotedVulkanDevice)
        {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
            Core::Log::Warn(
                "[Runtime] Promoted Vulkan device selected; backend remains fail-closed until the first clean default-recipe validation promotes it.");
            return Backends::Vulkan::CreateVulkanDevice();
#endif
        }

        if (config.EnablePromotedVulkanDevice && !kPromotedVulkanAvailable)
        {
            Core::Log::Warn(
                "[Runtime] Promoted Vulkan device requested but not compiled into this build; using Null device fallback.");
        }

        // Vulkan execution is opt-in during GRAPHICS-018. The default path
        // routes through the Null stub so IDevice::IsOperational() remains
        // false and resource managers surface DeviceNotOperational rather
        // than faking GPU work.
        return Backends::Null::CreateNullDevice();
    }

    Core::Result InitializeRuntimeGpuAssetFallbackTexture(
        Graphics::GpuAssetCache& cache,
        const RHI::IDevice& device)
    {
        // Skipped when the device is non-operational (e.g. the Null backend):
        // material resolution then returns GpuAssetFallbackReason::Unavailable
        // and shaders route to factor-only shading.
        if (!device.IsOperational())
            return Core::Ok();

        return cache.InitializeFallbackTexture(BuildFallbackTextureDesc());
    }
}
