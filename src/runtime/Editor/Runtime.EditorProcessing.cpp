module;
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
// Named only so the shared job declarations in the point-field header resolve.
import Extrinsic.Runtime.JobService;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        bool CanApplyProcessingConfig(const EditorProcessingContext& context) noexcept
        {
            return context.EngineConfigControlState != nullptr &&
                   context.EngineConfigCommandsAvailable &&
                   static_cast<bool>(context.PreviewEngineConfigDocument) &&
                   static_cast<bool>(context.ApplyEngineConfigHotSubset);
        }
    }

    bool EditorProcessingCommands::IsBound() const noexcept
    {
        // Prepared command handles borrow a world's scene on the main thread.
        // Their session state can reject a switched/destroyed world before use.
        return m_Context && (!m_Context->AttachmentActive || m_Context->AttachmentActive()) &&
               GeometryProcessingDetail::EditorProcessingContextWorldCurrent(*m_Context);
    }
    extern "C++" const EditorProcessingContext& EditorProcessingCommandsAccess::Resolve(const EditorProcessingCommands& commands) noexcept
    {
        static const EditorProcessingContext empty{};
        return commands.IsBound() ? *commands.m_Context : empty;
    }
    EditorProcessingCommands BindEditorProcessingCommands(EditorProcessingContext context)
    {
        EditorProcessingCommands commands;
        commands.m_Context = std::make_shared<const EditorProcessingContext>(std::move(context));
        return commands;
    }
    bool AreEditorProcessingConfigCommandsAvailable(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return CanApplyProcessingConfig(context);
    }
    ActionReadiness ResolveEditorProcessingActionReadiness(
        const EditorProcessingCommands& commands, ActionReadiness method)
    {
        if (!AreEditorProcessingConfigCommandsAvailable(commands))
            return {false, "Processing controls are unavailable. Open an active editor session."};
        if (method.Enabled)
            method.DisabledReason.clear();
        else if (method.DisabledReason.empty())
            method.DisabledReason = "Processing prerequisites are unavailable. Check the selected inputs and settings.";
        return method;
    }
    GeometryPropertyCatalogSnapshot GetEditorPointInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t stableId)
    {
        return GeometryProcessingDetail::BuildPointInputCatalog(
            EditorProcessingCommandsAccess::Resolve(commands), stableId);
    }
    EditorPointInputReadinessStats GetEditorPointInputReadinessStats(const EditorProcessingCommands& commands)
    {
        return GeometryProcessingDetail::PointInputReadinessStats(
            EditorProcessingCommandsAccess::Resolve(commands));
    }
    extern "C++" RuntimeEngineConfigApplyResult ApplyEditorProcessingConfig(
        const EditorProcessingCommands& commands,
        const Core::Config::EngineConfigSectionValidationResult& validation,
        const std::string& sourceId, const std::function<void(Core::Config::EngineConfig&)>& update)
    {
        RuntimeEngineConfigApplyResult result{
            .Status = RuntimeEngineConfigApplyStatus::Rejected,
            .Source = RuntimeConfigControlSource::Editor,
        };
        if (!validation.Usable())
        {
            result.LoadResult.Diagnostics = validation.Diagnostics;
            return result;
        }
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!CanApplyProcessingConfig(context))
            return result;
        auto candidate = context.EngineConfigControlState->ActiveConfig;
        update(candidate);
        result.LoadResult = context.PreviewEngineConfigDocument(
            Core::Config::SerializeEngineConfig(candidate), sourceId);
        if (!Core::Config::IsConfigUsable(result.LoadResult)) return result;
        if (result.LoadResult.State == Core::Config::EngineConfigState::FallbackApplied)
        {
            // Reapply the pure section update to detect edits lost to file-load
            // fallback, while tolerating fallback in unrelated sections.
            auto accepted = result.LoadResult.Preview.Config;
            update(accepted);
            if (accepted.AppSections != result.LoadResult.Preview.Config.AppSections)
                return result;
        }
        return context.ApplyEngineConfigHotSubset(result.LoadResult);
    }
}
