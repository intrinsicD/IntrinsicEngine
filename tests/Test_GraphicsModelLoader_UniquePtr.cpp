#include <gtest/gtest.h>

import Core;
import Graphics;
import RHI;

using namespace Core;

// This is a compile-time / contract test.
// We can't run ModelLoader::LoadAsync without a Vulkan device in unit tests,
// but we *can* prove the public API returns a unique_ptr<Model> now.
TEST(ModelLoader, LoadAsync_ReturnsUniquePtr)
{
    using namespace Graphics;

    static_assert(std::is_same_v<decltype(std::declval<ModelLoadResult>().ModelData), std::unique_ptr<Model>>);

    SUCCEED();
}

