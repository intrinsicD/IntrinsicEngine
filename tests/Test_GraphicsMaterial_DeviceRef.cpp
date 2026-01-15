#include <gtest/gtest.h>

import Core;
import Graphics;
import RHI;

TEST(GraphicsMaterial, ConstructorTakesDeviceByRef)
{
    using namespace Graphics;

    // Compile-time API contract: Materials must not require shared_ptr<VulkanDevice>.
    static_assert(std::is_constructible_v<Material,
        RHI::VulkanDevice&,
        RHI::BindlessDescriptorSystem&,
        Core::Assets::AssetHandle,
        uint32_t,
        Core::Assets::AssetManager&>);

    static_assert(!std::is_constructible_v<Material,
        std::shared_ptr<RHI::VulkanDevice>,
        RHI::BindlessDescriptorSystem&,
        Core::Assets::AssetHandle,
        uint32_t,
        Core::Assets::AssetManager&>);

    SUCCEED();
}
