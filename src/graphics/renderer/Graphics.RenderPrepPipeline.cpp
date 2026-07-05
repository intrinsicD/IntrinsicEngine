module;

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Graphics.RenderPrepPipeline;

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct PrepPipelineCommitTag {};
        struct PrepMaterialBaseSyncTag {};
        struct PrepVisualizationSyncTag {};
        struct PrepMaterialOverrideSyncTag {};
        struct PrepTransformSyncTag {};
        struct PrepLightSyncTag {};
        struct PrepClusterLightTableTag {};
        struct PrepGpuWorldSyncTag {};

        void AppendMissing(RenderPrepPipelineResult& result, RenderPrepRequiredInput input)
        {
            result.MissingInputs.push_back(input);
        }

        [[nodiscard]] bool ValidateInputs(const RenderPrepPipelineInputs& inputs,
                                          RenderPrepPipelineResult& result)
        {
            if (inputs.PipelineManager == nullptr) AppendMissing(result, RenderPrepRequiredInput::PipelineManager);
            if (inputs.Materials == nullptr) AppendMissing(result, RenderPrepRequiredInput::MaterialSystem);
            if (inputs.Colormaps == nullptr) AppendMissing(result, RenderPrepRequiredInput::ColormapSystem);
            if (inputs.VisualizationSync == nullptr) AppendMissing(result, RenderPrepRequiredInput::VisualizationSyncSystem);
            if (inputs.TransformSync == nullptr) AppendMissing(result, RenderPrepRequiredInput::TransformSyncSystem);
            if (inputs.Lights == nullptr) AppendMissing(result, RenderPrepRequiredInput::LightSystem);
            if (inputs.World == nullptr) AppendMissing(result, RenderPrepRequiredInput::GpuWorld);
            if (inputs.Culling == nullptr) AppendMissing(result, RenderPrepRequiredInput::CullingSystem);
            if (!inputs.EnsureClusterLightResources) AppendMissing(result, RenderPrepRequiredInput::ClusterLightResourcesHook);

            if (result.MissingInputs.empty())
            {
                return true;
            }

            result.Succeeded = false;
            result.FailureReason = RenderPrepFailureReason::MissingRequiredInput;
            std::ostringstream message;
            message << "Render prep missing required input";
            for (std::size_t i = 0; i < result.MissingInputs.size(); ++i)
            {
                message << (i == 0 ? ": " : ", ")
                        << ToString(result.MissingInputs[i]);
            }
            result.Diagnostic = message.str();
            return false;
        }

        void RecordStep(RenderPrepPipelineResult& result,
                        const RenderPrepPipelineInputs& inputs,
                        const RenderPrepStep step)
        {
            result.ExecutedSteps.push_back(step);
            if (inputs.OnStepExecuted)
            {
                inputs.OnStepExecuted(step);
            }
        }

        void ExecutePipelineCommit(RenderPrepPipelineResult& result,
                                   const RenderPrepPipelineInputs& inputs)
        {
            inputs.PipelineManager->CommitPending();
            RecordStep(result, inputs, RenderPrepStep::PipelineCommit);
        }

        void ExecuteMaterialBaseSync(RenderPrepPipelineResult& result,
                                     const RenderPrepPipelineInputs& inputs)
        {
            inputs.Materials->SyncGpuBuffer();
            RecordStep(result, inputs, RenderPrepStep::MaterialBaseSync);
        }

        void ExecuteVisualizationSync(RenderPrepPipelineResult& result,
                                      const RenderPrepPipelineInputs& inputs)
        {
            inputs.VisualizationSync->Sync(inputs.VisualizationSyncRecords,
                                           *inputs.Materials,
                                           *inputs.Colormaps,
                                           *inputs.World,
                                           inputs.VisualizationPropertyBufferAddresses,
                                           inputs.VisualizationScalarPackets);
            RecordStep(result, inputs, RenderPrepStep::VisualizationSync);
        }

        void ExecuteMaterialOverrideSync(RenderPrepPipelineResult& result,
                                         const RenderPrepPipelineInputs& inputs)
        {
            inputs.Materials->SyncGpuBuffer();
            RecordStep(result, inputs, RenderPrepStep::MaterialOverrideSync);
        }

        void ExecuteTransformSync(RenderPrepPipelineResult& result,
                                  const RenderPrepPipelineInputs& inputs)
        {
            inputs.TransformSync->SyncGpuBuffer(inputs.TransformSyncRecords, *inputs.World);
            RecordStep(result, inputs, RenderPrepStep::TransformSync);
        }

        void ExecuteLightSync(RenderPrepPipelineResult& result,
                              const RenderPrepPipelineInputs& inputs)
        {
            inputs.Lights->SyncGpuBuffer(inputs.LightSnapshots, *inputs.World);
            RecordStep(result, inputs, RenderPrepStep::LightSync);
        }

        void ExecuteClusterLightTableSync(RenderPrepPipelineResult& result,
                                          const RenderPrepPipelineInputs& inputs)
        {
            [[maybe_unused]] const bool clusterResourcesReady = inputs.EnsureClusterLightResources();
            RecordStep(result, inputs, RenderPrepStep::ClusterLightTableSync);
        }

        void ExecuteGpuWorldSync(RenderPrepPipelineResult& result,
                                 const RenderPrepPipelineInputs& inputs)
        {
            inputs.World->SetMaterialBuffer(inputs.Materials->GetBuffer(), inputs.Materials->GetCapacity());
            inputs.World->SyncFrame();
            RecordStep(result, inputs, RenderPrepStep::GpuWorldSync);
        }

        void ExecuteCullingSync(RenderPrepPipelineResult& result,
                                const RenderPrepPipelineInputs& inputs)
        {
            inputs.Culling->SyncGpuBuffer();
            RecordStep(result, inputs, RenderPrepStep::CullingSync);
        }

        void ExecuteSequential(RenderPrepPipelineResult& result,
                               const RenderPrepPipelineInputs& inputs)
        {
            ExecutePipelineCommit(result, inputs);
            ExecuteMaterialBaseSync(result, inputs);
            ExecuteVisualizationSync(result, inputs);
            ExecuteMaterialOverrideSync(result, inputs);
            ExecuteTransformSync(result, inputs);
            ExecuteLightSync(result, inputs);
            ExecuteClusterLightTableSync(result, inputs);
            ExecuteGpuWorldSync(result, inputs);
            ExecuteCullingSync(result, inputs);
        }

        [[nodiscard]] RenderPrepPipelineResult Failure(RenderPrepFailureReason reason,
                                                       Core::ErrorCode error,
                                                       std::string diagnostic)
        {
            RenderPrepPipelineResult result{};
            result.Succeeded = false;
            result.FailureReason = reason;
            result.TaskGraphError = error;
            result.Diagnostic = std::move(diagnostic);
            return result;
        }
    }

    std::string_view ToString(RenderPrepStep step) noexcept
    {
        switch (step)
        {
        case RenderPrepStep::PipelineCommit: return "PipelineCommit";
        case RenderPrepStep::MaterialBaseSync: return "MaterialBaseSync";
        case RenderPrepStep::VisualizationSync: return "VisualizationSync";
        case RenderPrepStep::MaterialOverrideSync: return "MaterialOverrideSync";
        case RenderPrepStep::TransformSync: return "TransformSync";
        case RenderPrepStep::LightSync: return "LightSync";
        case RenderPrepStep::ClusterLightTableSync: return "ClusterLightTableSync";
        case RenderPrepStep::GpuWorldSync: return "GpuWorldSync";
        case RenderPrepStep::CullingSync: return "CullingSync";
        case RenderPrepStep::Count: return "Count";
        }
        return "Unknown";
    }

    std::string_view ToString(RenderPrepRequiredInput input) noexcept
    {
        switch (input)
        {
        case RenderPrepRequiredInput::PipelineManager: return "PipelineManager";
        case RenderPrepRequiredInput::MaterialSystem: return "MaterialSystem";
        case RenderPrepRequiredInput::ColormapSystem: return "ColormapSystem";
        case RenderPrepRequiredInput::VisualizationSyncSystem: return "VisualizationSyncSystem";
        case RenderPrepRequiredInput::TransformSyncSystem: return "TransformSyncSystem";
        case RenderPrepRequiredInput::LightSystem: return "LightSystem";
        case RenderPrepRequiredInput::GpuWorld: return "GpuWorld";
        case RenderPrepRequiredInput::CullingSystem: return "CullingSystem";
        case RenderPrepRequiredInput::ClusterLightResourcesHook: return "ClusterLightResourcesHook";
        case RenderPrepRequiredInput::Count: return "Count";
        }
        return "Unknown";
    }

    std::string_view ToString(RenderPrepFailureReason reason) noexcept
    {
        switch (reason)
        {
        case RenderPrepFailureReason::None: return "None";
        case RenderPrepFailureReason::MissingRequiredInput: return "MissingRequiredInput";
        case RenderPrepFailureReason::TaskGraphCompileFailed: return "TaskGraphCompileFailed";
        case RenderPrepFailureReason::TaskGraphExecuteFailed: return "TaskGraphExecuteFailed";
        }
        return "Unknown";
    }

    RenderPrepPipelineResult RenderPrepPipeline::Run(const RenderPrepPipelineInputs& inputs,
                                                     const RenderPrepPipelineOptions& options)
    {
        RenderPrepPipelineResult result{};
        if (!ValidateInputs(inputs, result))
        {
            return result;
        }

        if (!options.UseTaskGraph)
        {
            ExecuteSequential(result, inputs);
            result.Succeeded = true;
            return result;
        }

        Core::Dag::TaskGraph graph{Core::Dag::QueueDomain::Cpu};
        graph.AddPass(
            "RenderPrep.PipelineCommit",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Write<PrepPipelineCommitTag>();
            },
            [&result, &inputs]
            {
                ExecutePipelineCommit(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.MaterialBaseSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepPipelineCommitTag>();
                b.Write<PrepMaterialBaseSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteMaterialBaseSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.VisualizationSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepMaterialBaseSyncTag>();
                b.Write<PrepVisualizationSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteVisualizationSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.MaterialOverrideSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepVisualizationSyncTag>();
                b.Write<PrepMaterialOverrideSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteMaterialOverrideSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.TransformSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepMaterialOverrideSyncTag>();
                b.Write<PrepTransformSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteTransformSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.LightSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepTransformSyncTag>();
                b.Write<PrepLightSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteLightSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.ClusterLightTableSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepLightSyncTag>();
                b.Write<PrepClusterLightTableTag>();
            },
            [&result, &inputs]
            {
                ExecuteClusterLightTableSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.GpuWorldSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepClusterLightTableTag>();
                b.Write<PrepGpuWorldSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteGpuWorldSync(result, inputs);
            });
        graph.AddPass(
            "RenderPrep.CullingSync",
            [] (Core::Dag::TaskGraphBuilder& b)
            {
                b.Read<PrepGpuWorldSyncTag>();
            },
            [&result, &inputs]
            {
                ExecuteCullingSync(result, inputs);
            });

        if (options.ForceTaskGraphCompileFailure.has_value())
        {
            return Failure(RenderPrepFailureReason::TaskGraphCompileFailed,
                           *options.ForceTaskGraphCompileFailure,
                           "Render prep TaskGraph compile failed: forced fault.");
        }

        const Core::Result compile = graph.Compile();
        if (!compile.has_value())
        {
            return Failure(RenderPrepFailureReason::TaskGraphCompileFailed,
                           compile.error(),
                           "Render prep TaskGraph compile failed.");
        }

        if (options.ForceTaskGraphExecuteFailure.has_value())
        {
            return Failure(RenderPrepFailureReason::TaskGraphExecuteFailed,
                           *options.ForceTaskGraphExecuteFailure,
                           "Render prep TaskGraph execute failed: forced fault.");
        }

        const Core::Result execute = graph.Execute();
        if (!execute.has_value())
        {
            result.Succeeded = false;
            result.FailureReason = RenderPrepFailureReason::TaskGraphExecuteFailed;
            result.TaskGraphError = execute.error();
            result.Diagnostic = "Render prep TaskGraph execute failed.";
            return result;
        }

        result.Succeeded = true;
        return result;
    }
}
