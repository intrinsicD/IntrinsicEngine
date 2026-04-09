#include <gtest/gtest.h>
#include <type_traits>

import Runtime.AssetIngestService;
import Asset.Pipeline;
import Runtime.SceneManager;
import Graphics;
import Core.IOBackend;
import RHI;

TEST(AssetIngestService, NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::AssetIngestService>);
    static_assert(!std::is_copy_assignable_v<Runtime::AssetIngestService>);
    SUCCEED();
}

TEST(AssetIngestService, NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::AssetIngestService>);
    static_assert(!std::is_move_assignable_v<Runtime::AssetIngestService>);
    SUCCEED();
}

TEST(AssetIngestService, NotDefaultConstructible)
{
    static_assert(!std::is_default_constructible_v<Runtime::AssetIngestService>);
    SUCCEED();
}

TEST(AssetIngestService, RequiresExplicitSubsystemDependencies)
{
    static_assert(std::is_constructible_v<Runtime::AssetIngestService,
                                          std::shared_ptr<RHI::VulkanDevice>,
                                          RHI::TransferManager&,
                                          RHI::BufferManager&,
                                          Graphics::GeometryPool&,
                                          Graphics::MaterialRegistry&,
                                          Runtime::AssetPipeline&,
                                          Runtime::SceneManager&,
                                          Graphics::IORegistry&,
                                          Core::IO::IIOBackend&,
                                          uint32_t>);
    SUCCEED();
}
