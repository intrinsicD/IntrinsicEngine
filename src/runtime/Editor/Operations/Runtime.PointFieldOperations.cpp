module;
#include <functional>
#include <optional>
#include <string>
#include <utility>
module Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
namespace Extrinsic::Runtime
{
    RuntimeEngineConfigApplyResult ApplyEditorKernelDensityConfig(
        const EditorProcessingCommands& commands, const KernelDensityConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateKernelDensityConfigSection(SerializeKernelDensityConfig(config), {}, kKernelDensityConfigSectionName),
            sourceId.empty() ? std::string{kKernelDensityConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetKernelDensityConfig(candidate, config); });
    }
    std::optional<KernelDensityConfig> GetEditorKernelDensityConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetKernelDensityConfig(context.EngineConfigControlState->ActiveConfig);
    }
    RuntimeEngineConfigApplyResult ApplyEditorPointSpacingConfig(
        const EditorProcessingCommands& commands, const PointSpacingConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidatePointSpacingConfigSection(SerializePointSpacingConfig(config), {}, kPointSpacingConfigSectionName),
            sourceId.empty() ? std::string{kPointSpacingConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetPointSpacingConfig(candidate, config); });
    }
    std::optional<PointSpacingConfig> GetEditorPointSpacingConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetPointSpacingConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
