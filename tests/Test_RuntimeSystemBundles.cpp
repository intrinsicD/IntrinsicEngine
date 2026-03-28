#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>

import Core;
import ECS;
import Runtime.SystemFeatureCatalog;
import Runtime.SystemBundles;

namespace
{
    struct MockSystemFeature final {};

    void RegisterSystemFeature(Core::FeatureRegistry& registry, const Core::FeatureDescriptor& feature)
    {
        const bool ok = registry.Register(feature,
                                          []() -> void* { return new MockSystemFeature{}; },
                                          [](void* p) { delete static_cast<MockSystemFeature*>(p); });
        ASSERT_TRUE(ok) << "Failed to register feature '" << feature.Name << "'";
    }
}

TEST(RuntimeSystemBundles, ExportedCoreFeatureOrderMatchesCanonicalBaseline)
{
    constexpr std::array<std::string_view, 3> kExpectedOrder{
        "TransformUpdate",
        "PropertySetDirtySync",
        "PrimitiveBVHBuild",
    };

    const auto order = Runtime::GetCoreFrameGraphFeatureOrder();
    ASSERT_EQ(order.size(), kExpectedOrder.size());

    for (size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(order[i].Name, kExpectedOrder[i]) << "Unexpected core bundle entry at index " << i;
        EXPECT_EQ(order[i].Category, Core::FeatureCategory::System);
        EXPECT_TRUE(order[i].DefaultEnabled);
    }
}

TEST(RuntimeSystemBundles, ExportedGpuFeatureOrderMatchesCanonicalBaseline)
{
    constexpr std::array<std::string_view, 5> kExpectedOrder{
        "GraphLifecycle",
        "MeshRendererLifecycle",
        "PointCloudLifecycle",
        "MeshViewLifecycle",
        "GPUSceneSync",
    };

    const auto order = Runtime::GetGpuFrameGraphFeatureOrder();
    ASSERT_EQ(order.size(), kExpectedOrder.size());

    for (size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(order[i].Name, kExpectedOrder[i]) << "Unexpected GPU bundle entry at index " << i;
        EXPECT_EQ(order[i].Category, Core::FeatureCategory::System);
        EXPECT_TRUE(order[i].DefaultEnabled);
    }
}


TEST(RuntimeSystemBundles, ExportedFullVariableFeatureOrderMatchesCanonicalBaseline)
{
    constexpr std::array<std::string_view, 8> kExpectedOrder{
        "TransformUpdate",
        "PropertySetDirtySync",
        "PrimitiveBVHBuild",
        "GraphLifecycle",
        "MeshRendererLifecycle",
        "PointCloudLifecycle",
        "MeshViewLifecycle",
        "GPUSceneSync",
    };

    const auto order = Runtime::GetVariableFrameGraphFeatureOrder();
    ASSERT_EQ(order.size(), kExpectedOrder.size());

    for (size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(order[i].Name, kExpectedOrder[i]) << "Unexpected combined bundle entry at index " << i;
    }
}

TEST(RuntimeSystemBundles, CoreBundle_PreservesCanonicalPassOrder)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::TransformUpdate);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PropertySetDirtySync);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PrimitiveBVHBuild);

    Core::Memory::ScopeStack scope(1024 * 64);
    Core::FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    Runtime::CoreFrameGraphRegistrationContext context{
        .Graph = graph,
        .Registry = registry,
        .Features = featureRegistry,
    };

    Runtime::CoreFrameGraphSystemBundle{}.Register(context);

    ASSERT_EQ(graph.GetPassCount(), 3u);
    EXPECT_EQ(graph.GetPassName(0), "TransformUpdate");
    EXPECT_EQ(graph.GetPassName(1), "PropertySetDirtySync");
    EXPECT_EQ(graph.GetPassName(2), "PrimitiveBVHBuild");

    const auto compileResult = graph.Compile();
    ASSERT_TRUE(compileResult.has_value()) << "Compile failed";
}

TEST(RuntimeSystemBundles, CoreBundle_RespectsFeatureToggles)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::TransformUpdate);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PropertySetDirtySync);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PrimitiveBVHBuild);
    ASSERT_TRUE(featureRegistry.SetEnabled(Runtime::SystemFeatureCatalog::PropertySetDirtySync, false));
    ASSERT_TRUE(featureRegistry.SetEnabled(Runtime::SystemFeatureCatalog::PrimitiveBVHBuild, false));

    Core::Memory::ScopeStack scope(1024 * 64);
    Core::FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    Runtime::CoreFrameGraphRegistrationContext context{
        .Graph = graph,
        .Registry = registry,
        .Features = featureRegistry,
    };

    Runtime::CoreFrameGraphSystemBundle{}.Register(context);

    ASSERT_EQ(graph.GetPassCount(), 1u);
    EXPECT_EQ(graph.GetPassName(0), "TransformUpdate");
}

TEST(RuntimeSystemBundles, VariableBundle_CoreOnlyMatchesCanonicalBaseline)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::TransformUpdate);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PropertySetDirtySync);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PrimitiveBVHBuild);

    Core::Memory::ScopeStack scope(1024 * 64);
    Core::FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    Runtime::CoreFrameGraphRegistrationContext context{
        .Graph = graph,
        .Registry = registry,
        .Features = featureRegistry,
    };

    Runtime::VariableFrameGraphSystemBundle{}.Register(context);

    ASSERT_EQ(graph.GetPassCount(), 3u);
    EXPECT_EQ(graph.GetPassName(0), "TransformUpdate");
    EXPECT_EQ(graph.GetPassName(1), "PropertySetDirtySync");
    EXPECT_EQ(graph.GetPassName(2), "PrimitiveBVHBuild");

    const auto compileResult = graph.Compile();
    ASSERT_TRUE(compileResult.has_value()) << "Compile failed";
}

TEST(RuntimeSystemBundles, VariableBundle_CoreOnlyRespectsFeatureToggles)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::TransformUpdate);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PropertySetDirtySync);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PrimitiveBVHBuild);
    ASSERT_TRUE(featureRegistry.SetEnabled(Runtime::SystemFeatureCatalog::TransformUpdate, false));
    ASSERT_TRUE(featureRegistry.SetEnabled(Runtime::SystemFeatureCatalog::PrimitiveBVHBuild, false));

    Core::Memory::ScopeStack scope(1024 * 64);
    Core::FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    Runtime::CoreFrameGraphRegistrationContext context{
        .Graph = graph,
        .Registry = registry,
        .Features = featureRegistry,
    };

    Runtime::VariableFrameGraphSystemBundle{}.Register(context);

    ASSERT_EQ(graph.GetPassCount(), 1u);
    EXPECT_EQ(graph.GetPassName(0), "PropertySetDirtySync");

    const auto compileResult = graph.Compile();
    ASSERT_TRUE(compileResult.has_value()) << "Compile failed";
}
