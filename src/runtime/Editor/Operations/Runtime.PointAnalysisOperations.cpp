module;
#include <functional>
#include <optional>
#include <string>
#include <utility>
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
namespace Extrinsic::Runtime
{
    RuntimeEngineConfigApplyResult ApplyEditorOutlierAnalysisConfig(
        const EditorProcessingCommands& commands, const OutlierAnalysisConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateOutlierAnalysisConfigSection(SerializeOutlierAnalysisConfig(config), {}, kOutlierAnalysisConfigSectionName),
            sourceId.empty() ? std::string{kOutlierAnalysisConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetOutlierAnalysisConfig(candidate, config); });
    }
    std::optional<OutlierAnalysisConfig> GetEditorOutlierAnalysisConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetOutlierAnalysisConfig(context.EngineConfigControlState->ActiveConfig);
    }
    RuntimeEngineConfigApplyResult ApplyEditorKeypointAnalysisConfig(
        const EditorProcessingCommands& commands, const KeypointAnalysisConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateKeypointAnalysisConfigSection(SerializeKeypointAnalysisConfig(config), {}, kKeypointAnalysisConfigSectionName),
            sourceId.empty() ? std::string{kKeypointAnalysisConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetKeypointAnalysisConfig(candidate, config); });
    }
    std::optional<KeypointAnalysisConfig> GetEditorKeypointAnalysisConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetKeypointAnalysisConfig(context.EngineConfigControlState->ActiveConfig);
    }
    RuntimeEngineConfigApplyResult ApplyEditorDensityWeightConfig(
        const EditorProcessingCommands& commands, const DensityWeightConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateDensityWeightConfigSection(SerializeDensityWeightConfig(config), {}, kDensityWeightConfigSectionName),
            sourceId.empty() ? std::string{kDensityWeightConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetDensityWeightConfig(candidate, config); });
    }
    std::optional<DensityWeightConfig> GetEditorDensityWeightConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetDensityWeightConfig(context.EngineConfigControlState->ActiveConfig);
    }
    RuntimeEngineConfigApplyResult ApplyEditorDescriptorAnalysisConfig(
        const EditorProcessingCommands& commands, const DescriptorAnalysisConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateDescriptorAnalysisConfigSection(SerializeDescriptorAnalysisConfig(config), {}, kDescriptorAnalysisConfigSectionName),
            sourceId.empty() ? std::string{kDescriptorAnalysisConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetDescriptorAnalysisConfig(candidate, config); });
    }
    std::optional<DescriptorAnalysisConfig> GetEditorDescriptorAnalysisConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetDescriptorAnalysisConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
