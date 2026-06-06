#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.RenderPrepPipeline;
import Extrinsic.Graphics.RenderSubsystemRegistry;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic::Graphics;

    [[nodiscard]] std::vector<RenderPrepStep> ExpectedOrder()
    {
        return {
            RenderPrepStep::PipelineCommit,
            RenderPrepStep::MaterialBaseSync,
            RenderPrepStep::VisualizationSync,
            RenderPrepStep::MaterialOverrideSync,
            RenderPrepStep::TransformSync,
            RenderPrepStep::LightSync,
            RenderPrepStep::ClusterLightTableSync,
            RenderPrepStep::GpuWorldSync,
            RenderPrepStep::CullingSync,
        };
    }

    struct PrepHarness
    {
        Extrinsic::Tests::MockDevice Device{};
        RenderSubsystemRegistry Registry{};
        std::vector<VisualizationSyncRecord> VisualizationRecords{};
        std::vector<TransformSyncRecord> TransformRecords{};
        std::vector<LightSnapshot> Lights{};
        bool ClusterResourcesRequested = false;

        PrepHarness()
        {
            Registry.Initialize(Device);
        }

        ~PrepHarness()
        {
            Registry.Shutdown();
        }

        [[nodiscard]] RenderPrepPipelineInputs MakeInputs(std::vector<RenderPrepStep>& observedSteps)
        {
            return RenderPrepPipelineInputs{
                .PipelineManager = Registry.PipelineManager() ? &*Registry.PipelineManager() : nullptr,
                .Materials = Registry.MaterialSystemRegistry() ? &*Registry.MaterialSystemRegistry() : nullptr,
                .Colormaps = Registry.ColormapSystemRegistry() ? &*Registry.ColormapSystemRegistry() : nullptr,
                .VisualizationSync = Registry.VisualizationSyncSystemRegistry() ? &*Registry.VisualizationSyncSystemRegistry() : nullptr,
                .TransformSync = Registry.TransformSyncSystemRegistry() ? &*Registry.TransformSyncSystemRegistry() : nullptr,
                .Lights = Registry.LightSystemRegistry() ? &*Registry.LightSystemRegistry() : nullptr,
                .World = Registry.GpuWorldSystem() ? &*Registry.GpuWorldSystem() : nullptr,
                .Culling = Registry.CullingSystemRegistry() ? &*Registry.CullingSystemRegistry() : nullptr,
                .VisualizationSyncRecords = std::span<VisualizationSyncRecord>{VisualizationRecords},
                .TransformSyncRecords = std::span<const TransformSyncRecord>{TransformRecords},
                .LightSnapshots = std::span<const LightSnapshot>{Lights},
                .EnsureClusterLightResources = [this]
                {
                    ClusterResourcesRequested = true;
                    return true;
                },
                .OnStepExecuted = [&observedSteps](RenderPrepStep step)
                {
                    observedSteps.push_back(step);
                },
            };
        }
    };
}

TEST(RenderPrepPipeline, TaskGraphExecutesPrepStepsInRequiredOrder)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> observedSteps{};
    RenderPrepPipeline pipeline;

    RenderPrepPipelineResult result = pipeline.Run(harness.MakeInputs(observedSteps));

    EXPECT_TRUE(result.Succeeded) << result.Diagnostic;
    EXPECT_EQ(result.FailureReason, RenderPrepFailureReason::None);
    EXPECT_EQ(result.ExecutedSteps, ExpectedOrder());
    EXPECT_EQ(observedSteps, ExpectedOrder());
    EXPECT_TRUE(harness.ClusterResourcesRequested);
}

TEST(RenderPrepPipeline, SequentialFallbackMatchesTaskGraphStepOrder)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> observedSteps{};
    RenderPrepPipeline pipeline;

    RenderPrepPipelineResult result = pipeline.Run(
        harness.MakeInputs(observedSteps),
        RenderPrepPipelineOptions{.UseTaskGraph = false});

    EXPECT_TRUE(result.Succeeded) << result.Diagnostic;
    EXPECT_EQ(result.ExecutedSteps, ExpectedOrder());
    EXPECT_EQ(observedSteps, ExpectedOrder());
    EXPECT_TRUE(harness.ClusterResourcesRequested);
}

TEST(RenderPrepPipeline, MissingRequiredInputFailsClosedBeforeExecutingSteps)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> observedSteps{};
    RenderPrepPipelineInputs inputs = harness.MakeInputs(observedSteps);
    inputs.Culling = nullptr;
    RenderPrepPipeline pipeline;

    RenderPrepPipelineResult result = pipeline.Run(inputs);

    EXPECT_FALSE(result.Succeeded);
    EXPECT_EQ(result.FailureReason, RenderPrepFailureReason::MissingRequiredInput);
    EXPECT_TRUE(std::find(result.MissingInputs.begin(),
                          result.MissingInputs.end(),
                          RenderPrepRequiredInput::CullingSystem) != result.MissingInputs.end());
    EXPECT_TRUE(result.ExecutedSteps.empty());
    EXPECT_TRUE(observedSteps.empty());
    EXPECT_FALSE(harness.ClusterResourcesRequested);
    EXPECT_NE(result.Diagnostic.find("CullingSystem"), std::string::npos);
}

TEST(RenderPrepPipeline, ForcedTaskGraphCompileFailureReportsDiagnostic)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> observedSteps{};
    RenderPrepPipeline pipeline;

    RenderPrepPipelineResult result = pipeline.Run(
        harness.MakeInputs(observedSteps),
        RenderPrepPipelineOptions{
            .ForceTaskGraphCompileFailure = Extrinsic::Core::ErrorCode::InvalidState,
        });

    EXPECT_FALSE(result.Succeeded);
    EXPECT_EQ(result.FailureReason, RenderPrepFailureReason::TaskGraphCompileFailed);
    EXPECT_EQ(result.TaskGraphError, Extrinsic::Core::ErrorCode::InvalidState);
    EXPECT_TRUE(result.ExecutedSteps.empty());
    EXPECT_TRUE(observedSteps.empty());
    EXPECT_FALSE(harness.ClusterResourcesRequested);
    EXPECT_NE(result.Diagnostic.find("compile failed"), std::string::npos);
}

TEST(RenderPrepPipeline, ForcedTaskGraphExecuteFailureReportsDiagnostic)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> observedSteps{};
    RenderPrepPipeline pipeline;

    RenderPrepPipelineResult result = pipeline.Run(
        harness.MakeInputs(observedSteps),
        RenderPrepPipelineOptions{
            .ForceTaskGraphExecuteFailure = Extrinsic::Core::ErrorCode::InvalidState,
        });

    EXPECT_FALSE(result.Succeeded);
    EXPECT_EQ(result.FailureReason, RenderPrepFailureReason::TaskGraphExecuteFailed);
    EXPECT_EQ(result.TaskGraphError, Extrinsic::Core::ErrorCode::InvalidState);
    EXPECT_TRUE(result.ExecutedSteps.empty());
    EXPECT_TRUE(observedSteps.empty());
    EXPECT_FALSE(harness.ClusterResourcesRequested);
    EXPECT_NE(result.Diagnostic.find("execute failed"), std::string::npos);
}
