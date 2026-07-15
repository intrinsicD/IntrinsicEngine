#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

import Extrinsic.Graphics.RenderSubsystemRegistry;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic::Graphics;

    [[nodiscard]] std::vector<RenderSubsystemStage> StagesFor(
        const std::vector<RenderSubsystemLifecycleEvent>& events,
        const RenderSubsystemLifecycleEventKind kind)
    {
        std::vector<RenderSubsystemStage> stages{};
        for (const RenderSubsystemLifecycleEvent& event : events)
        {
            if (event.Kind == kind)
            {
                stages.push_back(event.Stage);
            }
        }
        return stages;
    }

    [[nodiscard]] std::vector<RenderSubsystemStage> ExpectedInitializeOrder()
    {
        return {
            RenderSubsystemStage::BufferManager,
            RenderSubsystemStage::SamplerManager,
            RenderSubsystemStage::TextureManager,
            RenderSubsystemStage::PipelineManager,
            RenderSubsystemStage::GpuWorld,
            RenderSubsystemStage::MaterialSystem,
            RenderSubsystemStage::ColormapSystem,
            RenderSubsystemStage::VisualizationSyncSystem,
            RenderSubsystemStage::TransformSyncSystem,
            RenderSubsystemStage::CullingSystem,
            RenderSubsystemStage::LightSystem,
            RenderSubsystemStage::SelectionSystem,
            RenderSubsystemStage::ForwardSystem,
            RenderSubsystemStage::ShadowSystem,
            RenderSubsystemStage::DeferredSystem,
            RenderSubsystemStage::PostProcessSystem,
            RenderSubsystemStage::UvView,
        };
    }

    [[nodiscard]] std::vector<RenderSubsystemStage> ExpectedShutdownOrder()
    {
        return {
            RenderSubsystemStage::UvView,
            RenderSubsystemStage::SelectionSystem,
            RenderSubsystemStage::LightSystem,
            RenderSubsystemStage::ForwardSystem,
            RenderSubsystemStage::DeferredSystem,
            RenderSubsystemStage::PostProcessSystem,
            RenderSubsystemStage::ShadowSystem,
            RenderSubsystemStage::CullingSystem,
            RenderSubsystemStage::TransformSyncSystem,
            RenderSubsystemStage::VisualizationSyncSystem,
            RenderSubsystemStage::ColormapSystem,
            RenderSubsystemStage::GpuWorld,
            RenderSubsystemStage::MaterialSystem,
            RenderSubsystemStage::PipelineManager,
            RenderSubsystemStage::TextureManager,
            RenderSubsystemStage::SamplerManager,
            RenderSubsystemStage::BufferManager,
        };
    }
}

TEST(RenderSubsystemRegistry, InitializesAndShutsDownInDeterministicOrder)
{
    Extrinsic::Tests::MockDevice device;
    RenderSubsystemRegistry registry;

    registry.Initialize(device);
    RenderSubsystemRegistryDiagnostics diagnostics = registry.GetDiagnostics();
    EXPECT_TRUE(diagnostics.FullyInitialized);
    EXPECT_EQ(diagnostics.InitializedSubsystemCount, diagnostics.ExpectedSubsystemCount);
    EXPECT_EQ(StagesFor(diagnostics.LifecycleEvents,
                        RenderSubsystemLifecycleEventKind::Initialized),
              ExpectedInitializeOrder());

    registry.Shutdown();
    diagnostics = registry.GetDiagnostics();
    EXPECT_FALSE(diagnostics.FullyInitialized);
    EXPECT_EQ(diagnostics.InitializedSubsystemCount, 0u);
    EXPECT_EQ(StagesFor(diagnostics.LifecycleEvents,
                        RenderSubsystemLifecycleEventKind::Shutdown),
              ExpectedShutdownOrder());
}

TEST(RenderSubsystemRegistry, ShutdownIsSafeAfterPartialInitialization)
{
    Extrinsic::Tests::MockDevice device;
    RenderSubsystemRegistry registry;

    registry.Initialize(device, RenderSubsystemRegistryInitOptions{
        .StopAfterStage = RenderSubsystemStage::MaterialSystem,
    });

    RenderSubsystemRegistryDiagnostics diagnostics = registry.GetDiagnostics();
    EXPECT_FALSE(diagnostics.FullyInitialized);
    EXPECT_EQ(diagnostics.InitializedSubsystemCount, 6u);
    EXPECT_TRUE(std::find(diagnostics.MissingStages.begin(),
                          diagnostics.MissingStages.end(),
                          RenderSubsystemStage::ColormapSystem) != diagnostics.MissingStages.end());

    registry.Shutdown();
    diagnostics = registry.GetDiagnostics();
    EXPECT_FALSE(diagnostics.FullyInitialized);
    EXPECT_EQ(diagnostics.InitializedSubsystemCount, 0u);
}

TEST(RenderSubsystemRegistry, RebuildOperationalResourcesFailsClosedWhenRequiredSubsystemsAreMissing)
{
    Extrinsic::Tests::MockDevice device;
    RenderSubsystemRegistry registry;

    EXPECT_FALSE(registry.RebuildOperationalResources(device));
    RenderSubsystemRegistryDiagnostics diagnostics = registry.GetDiagnostics();
    EXPECT_FALSE(diagnostics.LastRebuildSucceeded);
    EXPECT_TRUE(diagnostics.LastRebuildFailedMissingRequiredSubsystem);

    registry.Initialize(device, RenderSubsystemRegistryInitOptions{
        .StopAfterStage = RenderSubsystemStage::PipelineManager,
    });
    EXPECT_FALSE(registry.RebuildOperationalResources(device));
    diagnostics = registry.GetDiagnostics();
    EXPECT_FALSE(diagnostics.LastRebuildSucceeded);
    EXPECT_TRUE(diagnostics.LastRebuildFailedMissingRequiredSubsystem);
}
