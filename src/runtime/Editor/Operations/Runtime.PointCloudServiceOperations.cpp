module;
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.PointCloudServiceOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.SelectionController;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

namespace Extrinsic::Runtime
{
    bool IsEditorClusteringAvailable(
        const EditorProcessingCommands& commands,
        const ClusteringService* const clustering) noexcept
    {
        return commands.IsBound() && clustering != nullptr && clustering->Available();
    }

    bool IsEditorPointCloudConsolidationAvailable(
        const EditorProcessingCommands& commands,
        const PointCloudConsolidationService* const consolidation) noexcept
    {
        return commands.IsBound() && consolidation != nullptr && consolidation->Available();
    }

    KMeansRunCompleted SubmitKMeansRun(
        const EditorProcessingCommands& commands,
        ClusteringService* const clustering,
        const RunKMeans& command)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        KMeansRunCompleted result{
            .World = context.World,
            .Status = KMeansRunStatus::ModuleUnavailable,
            .StableEntityId = command.StableEntityId,
            .Properties = command.Properties,
            .Parameters = command.Parameters,
            .RequestedBackend = command.Backend,
            .ActualBackend = ClusteringBackend::None,
            .Message = "ClusteringService is unavailable.",
        };
        if (!IsEditorClusteringAvailable(commands, clustering))
            return result;

        result.Correlation = clustering->RunKMeans(command);
        result.Status = KMeansRunStatus::Queued;
        result.Message = "K-Means runtime job queued.";
        return result;
    }

    PointCloudConsolidationResult SubmitEditorPointCloudConsolidation(
        const EditorProcessingCommands& commands,
        PointCloudConsolidationService* const consolidation,
        PointCloudConsolidationRequest request)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        PointCloudConsolidationResult result{
            .World = context.World,
            .Status = PointCloudConsolidationRunStatus::ModuleUnavailable,
            .StableEntityId = request.StableEntityId,
            .Properties = request.Properties,
            .Config = request.Config,
            .StrategyToken = std::string{StableToken(request.Config.Strategy)},
            .Error = Core::ErrorCode::Unknown,
            .Message = "Point-cloud consolidation service is unavailable.",
        };
        if (!IsEditorPointCloudConsolidationAvailable(commands, consolidation))
            return result;

        result.Correlation = consolidation->Run(std::move(request));
        if (!result.Correlation.IsValid())
        {
            result.Message = "Point-cloud consolidation command could not be queued.";
            return result;
        }
        result.Status = PointCloudConsolidationRunStatus::Queued;
        result.Error = Core::ErrorCode::Success;
        result.Message = result.Config.Backend ==
                                 PointCloudConsolidationBackend::VulkanCompute
            ? "Queued: support-radius profiling runs first; accepted Vulkan work "
              "then records, executes, reads back, and publishes positions."
            : "Queued: support-radius profiling runs first; CPU execution and "
              "position publication follow.";
        return result;
    }

    PointCloudConsolidationAvailability
    ResolveEditorPointCloudConsolidationAvailability(
        const EditorProcessingCommands& commands,
        const PointCloudConsolidationService* const consolidation,
        const PointCloudConsolidationRequest& request)
    {
        if (!IsEditorPointCloudConsolidationAvailable(commands, consolidation))
        {
            return PointCloudConsolidationAvailability{
                .Available = false,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Point-set consolidation service is unavailable.",
            };
        }
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.Scene == nullptr)
        {
            return PointCloudConsolidationAvailability{
                .Available = false,
                .Error = Core::ErrorCode::ResourceNotFound,
                .Message = "Scene registry is unavailable.",
            };
        }

        const entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(request.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
        {
            return PointCloudConsolidationAvailability{
                .Available = false,
                .Error = Core::ErrorCode::ResourceNotFound,
                .Message = "The selected geometry entity is stale or unavailable.",
            };
        }

        return ResolvePointCloudConsolidationAvailability(
            BuildGeometryAvailability(raw, entity), request.Properties, request.Config);
    }

    RuntimeEngineConfigApplyResult ApplyEditorClusteringConfig(
        const EditorProcessingCommands& commands,
        const ClusteringConfig& config,
        std::string sourceId)
    {
        // Clustering has no standalone section pre-validation gate: the
        // whole-document preview inside the shared apply is the only rejection
        // path, and its diagnostics are what callers already read.
        return ApplyEditorProcessingConfig(commands,
            {.State = Core::Config::EngineConfigState::Valid},
            sourceId.empty() ? std::string{kClusteringConfigSectionName} : std::move(sourceId),
            [&config](Core::Config::EngineConfig& candidate)
            { SetClusteringConfig(candidate, config); });
    }

    std::optional<ClusteringConfig> GetEditorClusteringConfig(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return GetClusteringConfig(context.EngineConfigControlState->ActiveConfig);
    }

    RuntimeEngineConfigApplyResult ApplyEditorPointCloudConsolidationConfig(
        const EditorProcessingCommands& commands,
        const PointCloudConsolidationConfig& config,
        std::string sourceId)
    {
        // Point-cloud consolidation likewise carries no editor-side section
        // pre-validation gate; the whole-document preview is its rejection path.
        return ApplyEditorProcessingConfig(commands,
            {.State = Core::Config::EngineConfigState::Valid},
            sourceId.empty() ? std::string{kPointCloudConsolidationConfigSectionName}
                             : std::move(sourceId),
            [&config](Core::Config::EngineConfig& candidate)
            { SetPointCloudConsolidationConfig(candidate, config); });
    }

    bool IsValidEditorPointCloudConsolidationConfig(
        const PointCloudConsolidationConfig& config)
    {
        const Core::Config::EngineConfigSectionValidationResult validation =
            ValidatePointCloudConsolidationConfigSection(
                SerializePointCloudConsolidationConfig(config),
                SerializePointCloudConsolidationConfig(PointCloudConsolidationConfig{}),
                "editor.point_cloud_consolidation");
        return validation.State == Core::Config::EngineConfigState::Valid;
    }

    std::optional<PointCloudConsolidationConfig>
    GetEditorPointCloudConsolidationConfig(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return GetPointCloudConsolidationConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
