module;
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <entt/entity/fwd.hpp>
#include <entt/signal/fwd.hpp>
#include "RHI.Vulkan.hpp"

export module Runtime.SystemBundles;

import Core.Assets;
import Core.FeatureRegistry;
import Core.FrameGraph;
import Graphics.Geometry;
import Graphics.GPUScene;
import Graphics.MaterialRegistry;
import RHI.Device;
import RHI.Transfer;

export namespace Runtime
{
    struct CoreFrameGraphRegistrationContext
    {
        Core::FrameGraph& Graph;
        entt::registry& Registry;
        Core::FeatureRegistry& Features;
    };

    struct GpuFrameGraphRegistrationContext
    {
        CoreFrameGraphRegistrationContext& Core;
        Graphics::GPUScene& GpuScene;
        Core::Assets::AssetManager& AssetManager;
        Graphics::MaterialRegistry& MaterialRegistry;
        Graphics::GeometryPool& GeometryStorage;
        std::shared_ptr<RHI::VulkanDevice> Device;
        RHI::TransferManager& TransferManager;
        entt::dispatcher& Dispatcher;
        uint32_t DefaultTextureId = 0;
    };

    struct CoreFrameGraphSystemBundle
    {
        void Register(this const CoreFrameGraphSystemBundle&,
                      const CoreFrameGraphRegistrationContext& context);
    };

    struct GpuFrameGraphSystemBundle
    {
        void Register(this const GpuFrameGraphSystemBundle&,
                      const GpuFrameGraphRegistrationContext& context);
    };

    struct VariableFrameGraphSystemBundle
    {
        void Register(this const VariableFrameGraphSystemBundle&,
                      const CoreFrameGraphRegistrationContext& coreContext,
                      const GpuFrameGraphRegistrationContext* gpuContext = nullptr);
    };

    [[nodiscard]] std::span<const Core::FeatureDescriptor> GetCoreFrameGraphFeatureOrder();
    [[nodiscard]] std::span<const Core::FeatureDescriptor> GetGpuFrameGraphFeatureOrder();
    [[nodiscard]] std::span<const Core::FeatureDescriptor> GetVariableFrameGraphFeatureOrder();
}
