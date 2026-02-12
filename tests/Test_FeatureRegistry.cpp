#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

import Core;

using namespace Core;
using namespace Core::Hash;

// -------------------------------------------------------------------------
// Test types — simple structs that simulate real features
// -------------------------------------------------------------------------
namespace
{
    struct MockRenderFeature
    {
        int Value = 42;
    };

    struct MockGeometryOp
    {
        float Threshold = 0.5f;
    };

    struct MockPanel
    {
        bool Visible = true;
    };

    struct MockSystem
    {
        uint32_t Priority = 0;
    };

    // A non-default-constructible type for factory testing
    struct ConfiguredFeature
    {
        int Config;
        explicit ConfiguredFeature(int config) : Config(config) {}
    };
}

// =========================================================================
// Test: Empty registry has zero entries
// =========================================================================
TEST(FeatureRegistry, EmptyRegistryIsEmpty)
{
    FeatureRegistry registry;
    EXPECT_EQ(registry.Count(), 0u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::RenderFeature), 0u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::GeometryOperator), 0u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::Panel), 0u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::System), 0u);
}

// =========================================================================
// Test: Register a single feature via template
// =========================================================================
TEST(FeatureRegistry, RegisterSingleFeature)
{
    FeatureRegistry registry;
    bool ok = registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature, "Main forward rendering");
    EXPECT_TRUE(ok);
    EXPECT_EQ(registry.Count(), 1u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::RenderFeature), 1u);
}

// =========================================================================
// Test: Find registered feature by StringID
// =========================================================================
TEST(FeatureRegistry, FindByStringID)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);

    const FeatureInfo* info = registry.Find("ForwardPass"_id);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->Name, "ForwardPass");
    EXPECT_EQ(info->Category, FeatureCategory::RenderFeature);
    EXPECT_TRUE(info->Enabled);
}

// =========================================================================
// Test: Find returns nullptr for unknown ID
// =========================================================================
TEST(FeatureRegistry, FindUnknownReturnsNull)
{
    FeatureRegistry registry;
    EXPECT_EQ(registry.Find("NonExistent"_id), nullptr);
}

// =========================================================================
// Test: Duplicate registration is rejected
// =========================================================================
TEST(FeatureRegistry, DuplicateRegistrationRejected)
{
    FeatureRegistry registry;
    bool first = registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    bool second = registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    EXPECT_TRUE(first);
    EXPECT_FALSE(second);
    EXPECT_EQ(registry.Count(), 1u);
}

// =========================================================================
// Test: Register multiple features in different categories
// =========================================================================
TEST(FeatureRegistry, MultipleCategoriesWork)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("PickingPass", FeatureCategory::RenderFeature);
    registry.Register<MockGeometryOp>("Simplify", FeatureCategory::GeometryOperator);
    registry.Register<MockPanel>("Inspector", FeatureCategory::Panel);
    registry.Register<MockSystem>("TransformUpdate", FeatureCategory::System);

    EXPECT_EQ(registry.Count(), 5u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::RenderFeature), 2u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::GeometryOperator), 1u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::Panel), 1u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::System), 1u);
}

// =========================================================================
// Test: GetByCategory returns correct features
// =========================================================================
TEST(FeatureRegistry, GetByCategoryFilters)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("PickingPass", FeatureCategory::RenderFeature);
    registry.Register<MockGeometryOp>("Simplify", FeatureCategory::GeometryOperator);

    auto renderFeatures = registry.GetByCategory(FeatureCategory::RenderFeature);
    EXPECT_EQ(renderFeatures.size(), 2u);

    auto geoOps = registry.GetByCategory(FeatureCategory::GeometryOperator);
    EXPECT_EQ(geoOps.size(), 1u);
    EXPECT_EQ(geoOps[0]->Name, "Simplify");

    auto panels = registry.GetByCategory(FeatureCategory::Panel);
    EXPECT_EQ(panels.size(), 0u);
}

// =========================================================================
// Test: GetByCategory preserves registration order
// =========================================================================
TEST(FeatureRegistry, GetByCategoryPreservesOrder)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("Alpha", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("Beta", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("Gamma", FeatureCategory::RenderFeature);

    auto features = registry.GetByCategory(FeatureCategory::RenderFeature);
    ASSERT_EQ(features.size(), 3u);
    EXPECT_EQ(features[0]->Name, "Alpha");
    EXPECT_EQ(features[1]->Name, "Beta");
    EXPECT_EQ(features[2]->Name, "Gamma");
}

// =========================================================================
// Test: Enable/disable features
// =========================================================================
TEST(FeatureRegistry, EnableDisable)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);

    EXPECT_TRUE(registry.IsEnabled("ForwardPass"_id));

    bool changed = registry.SetEnabled("ForwardPass"_id, false);
    EXPECT_TRUE(changed);
    EXPECT_FALSE(registry.IsEnabled("ForwardPass"_id));

    registry.SetEnabled("ForwardPass"_id, true);
    EXPECT_TRUE(registry.IsEnabled("ForwardPass"_id));
}

// =========================================================================
// Test: SetEnabled on unknown ID returns false
// =========================================================================
TEST(FeatureRegistry, SetEnabledUnknownReturnsFalse)
{
    FeatureRegistry registry;
    EXPECT_FALSE(registry.SetEnabled("NonExistent"_id, false));
}

// =========================================================================
// Test: IsEnabled on unknown ID returns false
// =========================================================================
TEST(FeatureRegistry, IsEnabledUnknownReturnsFalse)
{
    FeatureRegistry registry;
    EXPECT_FALSE(registry.IsEnabled("NonExistent"_id));
}

// =========================================================================
// Test: GetEnabled filters disabled features
// =========================================================================
TEST(FeatureRegistry, GetEnabledFiltersDisabled)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("PickingPass", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("DebugView", FeatureCategory::RenderFeature);

    registry.SetEnabled("PickingPass"_id, false);

    auto enabled = registry.GetEnabled(FeatureCategory::RenderFeature);
    EXPECT_EQ(enabled.size(), 2u);
    EXPECT_EQ(enabled[0]->Name, "ForwardPass");
    EXPECT_EQ(enabled[1]->Name, "DebugView");
}

// =========================================================================
// Test: CreateInstance creates a valid object
// =========================================================================
TEST(FeatureRegistry, CreateInstanceWorks)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);

    void* raw = registry.CreateInstance("ForwardPass"_id);
    ASSERT_NE(raw, nullptr);

    auto* feature = static_cast<MockRenderFeature*>(raw);
    EXPECT_EQ(feature->Value, 42);

    registry.DestroyInstance("ForwardPass"_id, raw);
}

// =========================================================================
// Test: CreateInstance returns nullptr for disabled feature
// =========================================================================
TEST(FeatureRegistry, CreateInstanceDisabledReturnsNull)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    registry.SetEnabled("ForwardPass"_id, false);

    EXPECT_EQ(registry.CreateInstance("ForwardPass"_id), nullptr);
}

// =========================================================================
// Test: CreateInstance returns nullptr for unknown ID
// =========================================================================
TEST(FeatureRegistry, CreateInstanceUnknownReturnsNull)
{
    FeatureRegistry registry;
    EXPECT_EQ(registry.CreateInstance("NonExistent"_id), nullptr);
}

// =========================================================================
// Test: DestroyInstance is safe with null
// =========================================================================
TEST(FeatureRegistry, DestroyNullIsSafe)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    // Should not crash
    registry.DestroyInstance("ForwardPass"_id, nullptr);
}

// =========================================================================
// Test: RegisterWithFactory for non-default-constructible types
// =========================================================================
TEST(FeatureRegistry, RegisterWithFactoryWorks)
{
    FeatureRegistry registry;
    bool ok = registry.RegisterWithFactory<ConfiguredFeature>(
        "ConfigFeature", FeatureCategory::RenderFeature,
        []() -> ConfiguredFeature* { return new ConfiguredFeature(99); },
        "A feature with custom config"
    );
    EXPECT_TRUE(ok);

    void* raw = registry.CreateInstance("ConfigFeature"_id);
    ASSERT_NE(raw, nullptr);

    auto* feature = static_cast<ConfiguredFeature*>(raw);
    EXPECT_EQ(feature->Config, 99);

    registry.DestroyInstance("ConfigFeature"_id, raw);
}

// =========================================================================
// Test: Unregister removes the entry
// =========================================================================
TEST(FeatureRegistry, UnregisterRemovesEntry)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    EXPECT_EQ(registry.Count(), 1u);

    bool removed = registry.Unregister("ForwardPass"_id);
    EXPECT_TRUE(removed);
    EXPECT_EQ(registry.Count(), 0u);
    EXPECT_EQ(registry.Find("ForwardPass"_id), nullptr);
}

// =========================================================================
// Test: Unregister unknown ID returns false
// =========================================================================
TEST(FeatureRegistry, UnregisterUnknownReturnsFalse)
{
    FeatureRegistry registry;
    EXPECT_FALSE(registry.Unregister("NonExistent"_id));
}

// =========================================================================
// Test: Re-register after unregister succeeds
// =========================================================================
TEST(FeatureRegistry, ReregisterAfterUnregister)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);
    registry.Unregister("ForwardPass"_id);

    bool ok = registry.Register<MockGeometryOp>("ForwardPass", FeatureCategory::GeometryOperator);
    EXPECT_TRUE(ok);
    EXPECT_EQ(registry.Count(), 1u);

    const FeatureInfo* info = registry.Find("ForwardPass"_id);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->Category, FeatureCategory::GeometryOperator);
}

// =========================================================================
// Test: Clear removes all entries
// =========================================================================
TEST(FeatureRegistry, ClearRemovesAll)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("A", FeatureCategory::RenderFeature);
    registry.Register<MockGeometryOp>("B", FeatureCategory::GeometryOperator);
    registry.Register<MockPanel>("C", FeatureCategory::Panel);
    EXPECT_EQ(registry.Count(), 3u);

    registry.Clear();
    EXPECT_EQ(registry.Count(), 0u);
    EXPECT_EQ(registry.Find("A"_id), nullptr);
}

// =========================================================================
// Test: ForEach iterates all entries
// =========================================================================
TEST(FeatureRegistry, ForEachVisitsAll)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("A", FeatureCategory::RenderFeature);
    registry.Register<MockGeometryOp>("B", FeatureCategory::GeometryOperator);
    registry.Register<MockPanel>("C", FeatureCategory::Panel);

    std::vector<std::string> names;
    registry.ForEach([&](const FeatureInfo& info) {
        names.push_back(info.Name);
    });

    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "A");
    EXPECT_EQ(names[1], "B");
    EXPECT_EQ(names[2], "C");
}

// =========================================================================
// Test: ForEachInCategory only visits matching entries
// =========================================================================
TEST(FeatureRegistry, ForEachInCategoryFilters)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("Forward", FeatureCategory::RenderFeature);
    registry.Register<MockRenderFeature>("Picking", FeatureCategory::RenderFeature);
    registry.Register<MockGeometryOp>("Simplify", FeatureCategory::GeometryOperator);

    std::vector<std::string> names;
    registry.ForEachInCategory(FeatureCategory::RenderFeature, [&](const FeatureInfo& info) {
        names.push_back(info.Name);
    });

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "Forward");
    EXPECT_EQ(names[1], "Picking");
}

// =========================================================================
// Test: Description is stored correctly
// =========================================================================
TEST(FeatureRegistry, DescriptionStored)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature,
                                          "Main PBR forward rendering pass");

    const FeatureInfo* info = registry.Find("ForwardPass"_id);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->Description, "Main PBR forward rendering pass");
}

// =========================================================================
// Test: Multiple CreateInstance calls produce independent objects
// =========================================================================
TEST(FeatureRegistry, MultipleInstancesAreIndependent)
{
    FeatureRegistry registry;
    registry.Register<MockRenderFeature>("ForwardPass", FeatureCategory::RenderFeature);

    void* a = registry.CreateInstance("ForwardPass"_id);
    void* b = registry.CreateInstance("ForwardPass"_id);

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b); // Different allocations

    auto* fa = static_cast<MockRenderFeature*>(a);
    auto* fb = static_cast<MockRenderFeature*>(b);
    fa->Value = 100;
    EXPECT_EQ(fb->Value, 42); // Independent

    registry.DestroyInstance("ForwardPass"_id, a);
    registry.DestroyInstance("ForwardPass"_id, b);
}

// =========================================================================
// Test: Explicit factory registration with FeatureInfo
// =========================================================================
TEST(FeatureRegistry, ExplicitRegistration)
{
    FeatureRegistry registry;

    FeatureInfo info{};
    info.Name = "CustomFeature";
    info.Id = StringID(HashString(info.Name));
    info.Category = FeatureCategory::System;
    info.Description = "A system with custom factory";
    info.Enabled = true;

    int callCount = 0;
    bool ok = registry.Register(
        std::move(info),
        [&callCount]() -> void* {
            ++callCount;
            return new MockSystem{};
        },
        [](void* p) { delete static_cast<MockSystem*>(p); }
    );
    EXPECT_TRUE(ok);

    void* instance = registry.CreateInstance("CustomFeature"_id);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(callCount, 1);

    registry.DestroyInstance("CustomFeature"_id, instance);
}

// =========================================================================
// Test: Large number of registrations
// =========================================================================
TEST(FeatureRegistry, ManyRegistrations)
{
    FeatureRegistry registry;

    for (int i = 0; i < 100; ++i)
    {
        std::string name = "Feature_" + std::to_string(i);
        bool ok = registry.Register<MockRenderFeature>(name, FeatureCategory::RenderFeature);
        EXPECT_TRUE(ok) << "Failed to register " << name;
    }

    EXPECT_EQ(registry.Count(), 100u);
    EXPECT_EQ(registry.CountByCategory(FeatureCategory::RenderFeature), 100u);

    // Verify lookup works for all
    for (int i = 0; i < 100; ++i)
    {
        std::string name = "Feature_" + std::to_string(i);
        StringID id(HashString(name));
        const FeatureInfo* info = registry.Find(id);
        ASSERT_NE(info, nullptr) << "Could not find " << name;
        EXPECT_EQ(info->Name, name);
    }
}
