#include <gtest/gtest.h>

import Core;

namespace
{
    constexpr auto kNoopFactory = []() -> void* { return nullptr; };
    constexpr auto kNoopDestroy = [](void*) {};
}

TEST(SystemFeatureCatalog, GpuMemoryThresholdPresetSetIsStable)
{
    static_assert(Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets.size() == 4u);
    EXPECT_EQ(Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets[0].Name, "GpuMemoryWarnThreshold70");
    EXPECT_EQ(Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets[1].Name, "GpuMemoryWarnThreshold75");
    EXPECT_EQ(Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets[2].Name, "GpuMemoryWarnThreshold85");
    EXPECT_EQ(Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets[3].Name, "GpuMemoryWarnThreshold90");
}

TEST(SystemFeatureCatalog, ResolveGpuMemoryWarningThresholdDefaultsTo80)
{
    Core::FeatureRegistry registry;

    const auto cfg = Runtime::SystemFeatureCatalog::ResolveGpuMemoryWarningThreshold(registry);
    EXPECT_DOUBLE_EQ(cfg.ThresholdFraction, 0.80);
    EXPECT_EQ(cfg.EnabledPresetCount, 0u);
    EXPECT_EQ(cfg.ActivePresetName, "default-80");
}

TEST(SystemFeatureCatalog, ResolveGpuMemoryWarningThresholdUsesHighestPrecedencePreset)
{
    Core::FeatureRegistry registry;

    for (const auto& preset : Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets)
    {
        EXPECT_TRUE(registry.Register(preset, kNoopFactory, kNoopDestroy));
    }

    EXPECT_TRUE(registry.SetEnabled(Runtime::SystemFeatureCatalog::GpuMemoryWarnThreshold75, true));
    EXPECT_TRUE(registry.SetEnabled(Runtime::SystemFeatureCatalog::GpuMemoryWarnThreshold90, true));

    const auto cfg = Runtime::SystemFeatureCatalog::ResolveGpuMemoryWarningThreshold(registry);
    EXPECT_EQ(cfg.EnabledPresetCount, 2u);
    EXPECT_DOUBLE_EQ(cfg.ThresholdFraction, 0.90);
    EXPECT_EQ(cfg.ActivePresetName, "90");
}
