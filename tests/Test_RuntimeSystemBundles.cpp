#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <entt/entity/registry.hpp>

import Core;
import ECS;
import Runtime.SystemBundles;

using Core::Hash::operator""_id;

namespace
{
    struct MockSystemFeature final {};

    void RegisterSystemFeature(Core::FeatureRegistry& registry, const std::string& name)
    {
        const bool ok = registry.Register<MockSystemFeature>(name, Core::FeatureCategory::System);
        ASSERT_TRUE(ok) << "Failed to register feature '" << name << "'";
    }
}

TEST(RuntimeSystemBundles, CoreBundle_PreservesCanonicalPassOrder)
{
    Core::FeatureRegistry featureRegistry;
    RegisterSystemFeature(featureRegistry, "TransformUpdate");
    RegisterSystemFeature(featureRegistry, "PropertySetDirtySync");
    RegisterSystemFeature(featureRegistry, "PrimitiveBVHSync");

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
    RegisterSystemFeature(featureRegistry, "TransformUpdate");
    RegisterSystemFeature(featureRegistry, "PropertySetDirtySync");
    RegisterSystemFeature(featureRegistry, "PrimitiveBVHSync");
    ASSERT_TRUE(featureRegistry.SetEnabled("PropertySetDirtySync"_id, false));
    ASSERT_TRUE(featureRegistry.SetEnabled("PrimitiveBVHSync"_id, false));

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
