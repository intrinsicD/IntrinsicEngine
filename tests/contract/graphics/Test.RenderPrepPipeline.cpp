#include <gtest/gtest.h>

#include <algorithm>
#include <thread>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Graphics.RenderPrepPipeline;
import Extrinsic.Graphics.RenderSubsystemRegistry;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic::Graphics;
    namespace Tasks = Extrinsic::Core::Tasks;

    class SchedulerScope
    {
    public:
        explicit SchedulerScope(const unsigned workerCount)
            : m_Owns(!Tasks::Scheduler::IsInitialized())
        {
            if (m_Owns)
                Tasks::Scheduler::Initialize(workerCount);
        }

        ~SchedulerScope()
        {
            if (m_Owns)
                Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;

    private:
        bool m_Owns = false;
    };

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

        [[nodiscard]] RenderPrepPipelineInputs MakeInputs(
            std::vector<RenderPrepStep>& observedSteps,
            std::vector<std::thread::id>* observedThreads = nullptr)
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
                .OnStepExecuted = [&observedSteps, observedThreads](
                                      RenderPrepStep step)
                {
                    observedSteps.push_back(step);
                    if (observedThreads != nullptr)
                    {
                        observedThreads->push_back(
                            std::this_thread::get_id());
                    }
                },
            };
        }
    };
}

TEST(RenderPrepPipeline, TaskGraphReusesPlanAndRebindsCallbacks)
{
    SchedulerScope scheduler{2u};
    const std::thread::id ownerThread = std::this_thread::get_id();
    PrepHarness harness;
    std::vector<RenderPrepStep> firstObservedSteps{};
    std::vector<RenderPrepStep> secondObservedSteps{};
    std::vector<std::thread::id> firstObservedThreads{};
    std::vector<std::thread::id> secondObservedThreads{};
    RenderPrepPipeline pipeline;

    const RenderPrepPipelineResult first =
        pipeline.Run(harness.MakeInputs(
            firstObservedSteps,
            &firstObservedThreads));
    const RenderPrepPipelineResult second =
        pipeline.Run(harness.MakeInputs(
            secondObservedSteps,
            &secondObservedThreads));

    EXPECT_TRUE(first.Succeeded) << first.Diagnostic;
    EXPECT_EQ(first.FailureReason, RenderPrepFailureReason::None);
    EXPECT_EQ(first.ExecutedSteps, ExpectedOrder());
    EXPECT_EQ(firstObservedSteps, ExpectedOrder());
    EXPECT_EQ(firstObservedThreads.size(), ExpectedOrder().size());
    EXPECT_TRUE(std::ranges::all_of(
        firstObservedThreads,
        [ownerThread](const std::thread::id thread)
        {
            return thread == ownerThread;
        }));
    EXPECT_EQ(first.TaskGraphCompileCalls, 1u);
    EXPECT_EQ(first.TaskGraphPlanBuilds, 1u);
    EXPECT_EQ(first.TaskGraphPlanReuses, 0u);
    EXPECT_FALSE(first.TaskGraphLastCompileReusedPlan);

    EXPECT_TRUE(second.Succeeded) << second.Diagnostic;
    EXPECT_EQ(second.FailureReason, RenderPrepFailureReason::None);
    EXPECT_EQ(second.ExecutedSteps, ExpectedOrder());
    EXPECT_EQ(secondObservedSteps, ExpectedOrder());
    EXPECT_EQ(secondObservedThreads.size(), ExpectedOrder().size());
    EXPECT_TRUE(std::ranges::all_of(
        secondObservedThreads,
        [ownerThread](const std::thread::id thread)
        {
            return thread == ownerThread;
        }));
    EXPECT_EQ(firstObservedSteps, ExpectedOrder());
    EXPECT_EQ(second.TaskGraphCompileCalls, 2u);
    EXPECT_EQ(second.TaskGraphPlanBuilds, 1u);
    EXPECT_EQ(second.TaskGraphPlanReuses, 1u);
    EXPECT_TRUE(second.TaskGraphLastCompileReusedPlan);
    EXPECT_TRUE(harness.ClusterResourcesRequested);
}

TEST(RenderPrepPipeline, SequentialFallbackMatchesTaskGraphStepOrder)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> firstTaskGraphSteps{};
    std::vector<RenderPrepStep> sequentialSteps{};
    std::vector<RenderPrepStep> secondTaskGraphSteps{};
    RenderPrepPipeline pipeline;

    const RenderPrepPipelineResult first =
        pipeline.Run(harness.MakeInputs(firstTaskGraphSteps));
    const RenderPrepPipelineResult sequential = pipeline.Run(
        harness.MakeInputs(sequentialSteps),
        RenderPrepPipelineOptions{.UseTaskGraph = false});
    const RenderPrepPipelineResult second =
        pipeline.Run(harness.MakeInputs(secondTaskGraphSteps));

    ASSERT_TRUE(first.Succeeded) << first.Diagnostic;
    EXPECT_TRUE(sequential.Succeeded) << sequential.Diagnostic;
    EXPECT_EQ(sequential.ExecutedSteps, ExpectedOrder());
    EXPECT_EQ(sequentialSteps, ExpectedOrder());
    EXPECT_EQ(sequential.TaskGraphCompileCalls, 0u);
    EXPECT_EQ(sequential.TaskGraphPlanBuilds, 0u);
    EXPECT_EQ(sequential.TaskGraphPlanReuses, 0u);

    EXPECT_TRUE(second.Succeeded) << second.Diagnostic;
    EXPECT_EQ(second.ExecutedSteps, ExpectedOrder());
    EXPECT_EQ(secondTaskGraphSteps, ExpectedOrder());
    EXPECT_EQ(firstTaskGraphSteps, ExpectedOrder());
    EXPECT_EQ(second.TaskGraphCompileCalls, 2u);
    EXPECT_EQ(second.TaskGraphPlanBuilds, 1u);
    EXPECT_EQ(second.TaskGraphPlanReuses, 1u);
    EXPECT_TRUE(second.TaskGraphLastCompileReusedPlan);
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
    EXPECT_EQ(result.TaskGraphCompileCalls, 0u);
    EXPECT_EQ(result.TaskGraphPlanBuilds, 0u);
    EXPECT_EQ(result.TaskGraphPlanReuses, 0u);
    EXPECT_NE(result.Diagnostic.find("CullingSystem"), std::string::npos);
}

TEST(RenderPrepPipeline, ForcedTaskGraphCompileFailureReportsDiagnostic)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> failedObservedSteps{};
    std::vector<RenderPrepStep> recoveredObservedSteps{};
    RenderPrepPipeline pipeline;

    const RenderPrepPipelineResult result = pipeline.Run(
        harness.MakeInputs(failedObservedSteps),
        RenderPrepPipelineOptions{
            .ForceTaskGraphCompileFailure = Extrinsic::Core::ErrorCode::InvalidState,
        });
    const RenderPrepPipelineResult recovered =
        pipeline.Run(harness.MakeInputs(recoveredObservedSteps));

    EXPECT_FALSE(result.Succeeded);
    EXPECT_EQ(result.FailureReason, RenderPrepFailureReason::TaskGraphCompileFailed);
    EXPECT_EQ(result.TaskGraphError, Extrinsic::Core::ErrorCode::InvalidState);
    EXPECT_TRUE(result.ExecutedSteps.empty());
    EXPECT_TRUE(failedObservedSteps.empty());
    EXPECT_EQ(result.TaskGraphCompileCalls, 0u);
    EXPECT_EQ(result.TaskGraphPlanBuilds, 0u);
    EXPECT_EQ(result.TaskGraphPlanReuses, 0u);
    EXPECT_NE(result.Diagnostic.find("compile failed"), std::string::npos);

    EXPECT_TRUE(recovered.Succeeded) << recovered.Diagnostic;
    EXPECT_EQ(recoveredObservedSteps, ExpectedOrder());
    EXPECT_TRUE(failedObservedSteps.empty());
    EXPECT_EQ(recovered.TaskGraphCompileCalls, 1u);
    EXPECT_EQ(recovered.TaskGraphPlanBuilds, 1u);
    EXPECT_EQ(recovered.TaskGraphPlanReuses, 0u);
    EXPECT_TRUE(harness.ClusterResourcesRequested);
}

TEST(RenderPrepPipeline, ForcedTaskGraphExecuteFailureReportsDiagnostic)
{
    PrepHarness harness;
    std::vector<RenderPrepStep> failedObservedSteps{};
    std::vector<RenderPrepStep> recoveredObservedSteps{};
    RenderPrepPipeline pipeline;

    const RenderPrepPipelineResult result = pipeline.Run(
        harness.MakeInputs(failedObservedSteps),
        RenderPrepPipelineOptions{
            .ForceTaskGraphExecuteFailure = Extrinsic::Core::ErrorCode::InvalidState,
        });
    const RenderPrepPipelineResult recovered =
        pipeline.Run(harness.MakeInputs(recoveredObservedSteps));

    EXPECT_FALSE(result.Succeeded);
    EXPECT_EQ(result.FailureReason, RenderPrepFailureReason::TaskGraphExecuteFailed);
    EXPECT_EQ(result.TaskGraphError, Extrinsic::Core::ErrorCode::InvalidState);
    EXPECT_TRUE(result.ExecutedSteps.empty());
    EXPECT_TRUE(failedObservedSteps.empty());
    EXPECT_EQ(result.TaskGraphCompileCalls, 1u);
    EXPECT_EQ(result.TaskGraphPlanBuilds, 1u);
    EXPECT_EQ(result.TaskGraphPlanReuses, 0u);
    EXPECT_FALSE(result.TaskGraphLastCompileReusedPlan);
    EXPECT_NE(result.Diagnostic.find("execute failed"), std::string::npos);

    EXPECT_TRUE(recovered.Succeeded) << recovered.Diagnostic;
    EXPECT_EQ(recoveredObservedSteps, ExpectedOrder());
    EXPECT_TRUE(failedObservedSteps.empty());
    EXPECT_EQ(recovered.TaskGraphCompileCalls, 2u);
    EXPECT_EQ(recovered.TaskGraphPlanBuilds, 1u);
    EXPECT_EQ(recovered.TaskGraphPlanReuses, 1u);
    EXPECT_TRUE(recovered.TaskGraphLastCompileReusedPlan);
    EXPECT_TRUE(harness.ClusterResourcesRequested);
}
