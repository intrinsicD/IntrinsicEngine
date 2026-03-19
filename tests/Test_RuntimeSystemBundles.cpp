#include <gtest/gtest.h>

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

TEST(RuntimeSystemBundles, CoreBundle_PreservesCanonicalPassOrder)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::TransformUpdate);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PropertySetDirtySync);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PrimitiveBVHSync);

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
    EXPECT_EQ(graph.GetPassName(2), "PrimitiveBVHSync");

    const auto compileResult = graph.Compile();
    ASSERT_TRUE(compileResult.has_value()) << "Compile failed";
}

TEST(RuntimeSystemBundles, CoreBundle_RespectsFeatureToggles)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::TransformUpdate);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PropertySetDirtySync);
    RegisterSystemFeature(featureRegistry, Runtime::SystemFeatureCatalog::PrimitiveBVHSync);
    ASSERT_TRUE(featureRegistry.SetEnabled(Runtime::SystemFeatureCatalog::PropertySetDirtySync, false));
    ASSERT_TRUE(featureRegistry.SetEnabled(Runtime::SystemFeatureCatalog::PrimitiveBVHSync, false));

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
