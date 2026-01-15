#include <gtest/gtest.h>

#include <type_traits>

import Core;
import RHI;
import Graphics;

// Regression test: Graphics::Material must not require shared ownership of textures.
// It should accept a default bindless texture index (uint32_t), and must not expose the old
// constructor taking std::shared_ptr<RHI::Texture>.
//
// This test is Vulkan-free and purely guards the API contract.

TEST(GraphicsMaterial, ConstructorSignature_NoSharedPtrTexture)
{
    static_assert(std::is_constructible_v<Graphics::Material,
                                         RHI::VulkanDevice&,
                                         RHI::BindlessDescriptorSystem&,
                                         Core::Assets::AssetHandle,
                                         uint32_t,
                                         Core::Assets::AssetManager&>);

    static_assert(std::is_destructible_v<Graphics::Material>);

    static_assert(!std::is_constructible_v<Graphics::Material,
                                          RHI::VulkanDevice&,
                                          RHI::BindlessDescriptorSystem&,
                                          Core::Assets::AssetHandle,
                                          std::shared_ptr<RHI::Texture>,
                                          Core::Assets::AssetManager&>);
}
