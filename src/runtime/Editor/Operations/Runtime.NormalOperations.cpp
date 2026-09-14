module;
#include <functional>
#include <optional>
#include <string>
#include <utility>
module Extrinsic.Runtime.NormalOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
namespace Extrinsic::Runtime
{
    RuntimeEngineConfigApplyResult ApplyEditorNormalEstimationConfig(
        const EditorProcessingCommands& commands, const NormalEstimationConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateNormalEstimationConfigSection(SerializeNormalEstimationConfig(config), {}, kNormalEstimationConfigSectionName),
            sourceId.empty() ? std::string{kNormalEstimationConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetNormalEstimationConfig(candidate, config); });
    }
    std::optional<NormalEstimationConfig> GetEditorNormalEstimationConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetNormalEstimationConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
