// Test_ShaderHotReload.cpp
// Tests for the ShaderRegistry source-path tracking and ShaderHotReload
// feature descriptor used by the shader hot-reload system (TODO E1).

#include <gtest/gtest.h>
#include <string>

import Core.Hash;
import Core.FeatureRegistry;
import Core.SystemFeatureCatalog;
import Graphics.ShaderRegistry;

using namespace Core::Hash;

// ---------------------------------------------------------------------------
// ShaderRegistry source-path tracking
// ---------------------------------------------------------------------------

TEST(ShaderRegistry, RegisterAndGetBasic)
{
    Graphics::ShaderRegistry reg;
    reg.Register("Test.Vert"_id, "shaders/test.vert.spv");

    auto result = reg.Get("Test.Vert"_id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "shaders/test.vert.spv");

    EXPECT_FALSE(reg.Get("NonExistent"_id).has_value());
}

TEST(ShaderRegistry, RegisterWithSourceTracksSourcePath)
{
    Graphics::ShaderRegistry reg;
    reg.RegisterWithSource("Surf.Vert"_id, "shaders/surface.vert.spv", "/src/shaders/surface.vert");

    auto spv = reg.Get("Surf.Vert"_id);
    ASSERT_TRUE(spv.has_value());
    EXPECT_EQ(*spv, "shaders/surface.vert.spv");

    auto src = reg.GetSourcePath("Surf.Vert"_id);
    ASSERT_TRUE(src.has_value());
    EXPECT_EQ(*src, "/src/shaders/surface.vert");
}

TEST(ShaderRegistry, GetSourcePathReturnsNulloptForPlainRegister)
{
    Graphics::ShaderRegistry reg;
    reg.Register("Test.Frag"_id, "shaders/test.frag.spv");

    EXPECT_FALSE(reg.GetSourcePath("Test.Frag"_id).has_value());
}

TEST(ShaderRegistry, ForEachIteratesAllEntries)
{
    Graphics::ShaderRegistry reg;
    reg.Register("A"_id, "a.spv");
    reg.Register("B"_id, "b.spv");
    reg.RegisterWithSource("C"_id, "c.spv", "/src/c.vert");

    int count = 0;
    reg.ForEach([&](auto /*id*/, const std::string& /*path*/) { ++count; });
    EXPECT_EQ(count, 3);
}

TEST(ShaderRegistry, ForEachWithSourceIteratesOnlyEntriesWithSource)
{
    Graphics::ShaderRegistry reg;
    reg.Register("A"_id, "a.spv");
    reg.RegisterWithSource("B"_id, "b.spv", "/src/b.frag");
    reg.RegisterWithSource("C"_id, "c.spv", "/src/c.vert");

    int count = 0;
    reg.ForEachWithSource([&](auto /*id*/, const std::string& spv, const std::string& src) {
        EXPECT_FALSE(spv.empty());
        EXPECT_FALSE(src.empty());
        ++count;
    });
    EXPECT_EQ(count, 2);
}

// ---------------------------------------------------------------------------
// ShaderHotReload feature descriptor
// ---------------------------------------------------------------------------

TEST(ShaderHotReload, FeatureDescriptorDefaultDisabled)
{
    EXPECT_EQ(Runtime::SystemFeatureCatalog::ShaderHotReload.Name, "ShaderHotReload");
    EXPECT_EQ(Runtime::SystemFeatureCatalog::ShaderHotReload.Category, Core::FeatureCategory::System);
    EXPECT_FALSE(Runtime::SystemFeatureCatalog::ShaderHotReload.DefaultEnabled);
}

TEST(ShaderHotReload, CanBeEnabledViaFeatureRegistry)
{
    Core::FeatureRegistry reg;
    reg.Register(
        Runtime::SystemFeatureCatalog::ShaderHotReload,
        []() -> void* { return nullptr; },
        [](void*) {});

    EXPECT_FALSE(reg.IsEnabled(Runtime::SystemFeatureCatalog::ShaderHotReload));

    reg.SetEnabled(Runtime::SystemFeatureCatalog::ShaderHotReload, true);
    EXPECT_TRUE(reg.IsEnabled(Runtime::SystemFeatureCatalog::ShaderHotReload));
}
