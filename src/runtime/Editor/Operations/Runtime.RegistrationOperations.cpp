module;
#include <functional>
#include <optional>
#include <string>
#include <utility>
module Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
namespace Extrinsic::Runtime
{
    RuntimeEngineConfigApplyResult ApplyEditorRegistrationConfig(
        const EditorProcessingCommands& commands, const RegistrationConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateRegistrationConfigSection(SerializeRegistrationConfig(config), {}, kRegistrationConfigSectionName),
            sourceId.empty() ? std::string{kRegistrationConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetRegistrationConfig(candidate, config); });
    }
    std::optional<RegistrationConfig> GetEditorRegistrationConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetRegistrationConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
