module;
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
module Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
namespace Extrinsic::Runtime
{
    bool EditorCurvatureSegmentationResult::Succeeded() const noexcept
    {
        const bool commandSucceeded =
            Status == EditorCommandStatus::Applied ||
            Status == EditorCommandStatus::NoChange;
        if (!commandSucceeded || RequestedMethod != ActualMethod)
            return false;
        if (ActualMethod ==
            CurvatureSegmentationMethod::FeatureAlignedPatches)
        {
            return FeatureDiagnostics.has_value() &&
                   FeatureDiagnostics->Succeeded() &&
                   PatchDiagnostics.has_value() &&
                   PatchDiagnostics->Succeeded();
        }
        if (ActualMethod == CurvatureSegmentationMethod::FeatureBoundaryCurves)
        {
            return FeatureDiagnostics.has_value() &&
                   FeatureDiagnostics->Succeeded() &&
                   BoundaryDiagnostics.has_value() &&
                   BoundaryDiagnostics->Status == Geometry::CurvatureSegmentation::
                       BoundaryPartitionStatus::Success;
        }
        return ActualMethod == CurvatureSegmentationMethod::CurvatureGmm &&
               Diagnostics.Succeeded();
    }

    const char* DebugNameForEditorMeshCurvatureOutput(
        const EditorMeshCurvatureOutput output) noexcept
    {
        switch (output)
        {
        case EditorMeshCurvatureOutput::All:
            return "All";
        case EditorMeshCurvatureOutput::Mean:
            return "Mean";
        case EditorMeshCurvatureOutput::Gaussian:
            return "Gaussian";
        case EditorMeshCurvatureOutput::PrincipalDirections:
            return "Principal";
        }
        return "Unknown";
    }

    RuntimeEngineConfigApplyResult ApplyEditorMeshCurvatureConfig(
        const EditorProcessingCommands& commands, const MeshCurvatureConfig& config,
        std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateMeshCurvatureConfigSection(
                SerializeMeshCurvatureConfig(config), {}, kMeshCurvatureConfigSectionName),
            sourceId.empty() ? std::string{kMeshCurvatureConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetMeshCurvatureConfig(candidate, config); });
    }

    std::optional<MeshCurvatureConfig> GetEditorMeshCurvatureConfig(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState == nullptr) return std::nullopt;
        return GetMeshCurvatureConfig(context.EngineConfigControlState->ActiveConfig);
    }

    RuntimeEngineConfigApplyResult ApplyEditorCurvatureSegmentationConfig(
        const EditorProcessingCommands& commands, const CurvatureSegmentationConfig& config,
        std::string sourceId)
    {
        // Curvature segmentation has no standalone section pre-validation gate:
        // the whole-document preview below is the only rejection path, and its
        // diagnostics are what callers already read.
        return ApplyEditorProcessingConfig(commands,
            {.State = Core::Config::EngineConfigState::Valid},
            sourceId.empty() ? std::string{kCurvatureSegmentationConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate)
            { SetCurvatureSegmentationConfig(candidate, config); });
    }

    std::optional<CurvatureSegmentationConfig> GetEditorCurvatureSegmentationConfig(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState == nullptr) return std::nullopt;
        return GetCurvatureSegmentationConfig(context.EngineConfigControlState->ActiveConfig);
    }

    RuntimeEngineConfigApplyResult ApplyEditorGeodesicsConfig(
        const EditorProcessingCommands& commands, const GeodesicsConfig& config,
        std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateGeodesicsConfigSection(
                SerializeGeodesicsConfig(config), {}, kGeodesicsConfigSectionName),
            sourceId.empty() ? std::string{kGeodesicsConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetGeodesicsConfig(candidate, config); });
    }

    std::optional<GeodesicsConfig> GetEditorGeodesicsConfig(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState == nullptr) return std::nullopt;
        return GetGeodesicsConfig(context.EngineConfigControlState->ActiveConfig);
    }

    EditorGeodesicsResult ApplyEditorConfiguredGeodesicsCommand(
        const EditorProcessingCommands& commands, const std::uint32_t stableEntityId)
    {
        const auto config = GetEditorGeodesicsConfig(commands);
        if (!config.has_value())
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Geodesics configuration is unavailable or invalid."};
        return ApplyEditorGeodesicsCommand(commands, {stableEntityId, *config});
    }
}
