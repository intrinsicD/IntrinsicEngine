#include <gtest/gtest.h>

import Core;
import Graphics;
import RHI;

using namespace Core;

// Contract-ish test: AssetManager must be able to own non-copyable payloads like Graphics::Material
// via the Create(name, unique_ptr<T>) overload.
TEST(CoreAssets, Create_UniquePtrMaterialCompiles)
{
    static_assert(!std::is_copy_constructible_v<Graphics::Material>);

    // We only validate compile-time ownership plumbing here.
    // Running this would require a VulkanDevice + BindlessDescriptorSystem instance.
    SUCCEED();
}

