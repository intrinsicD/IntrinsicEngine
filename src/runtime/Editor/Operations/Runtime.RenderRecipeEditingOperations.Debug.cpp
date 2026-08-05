module;

#include <string>
#include <string_view>

module Extrinsic.Runtime.RenderRecipeEditingOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;

namespace Extrinsic::Runtime
{
std::string_view DebugNameForEditorRenderRecipeConfigState(
        const EditorRenderRecipeConfigState state) noexcept
    {
        return Graphics::ToString(state);
    }

std::string_view DebugNameForEditorRenderRecipeConfigDiagnosticCode(
        const EditorRenderRecipeConfigDiagnosticCode code) noexcept
    {
        return Graphics::ToString(code);
    }

const char*DebugNameForEditorRenderRecipeDraftState(
        const EditorRenderRecipeDraftState state) noexcept
    {
        using State = EditorRenderRecipeDraftState;
        switch (state)
        {
        case State::InactiveDraft:
            return "InactiveDraft";
        case State::Debounced:
            return "Debounced";
        case State::Validated:
            return "Validated";
        case State::Rejected:
            return "Rejected";
        case State::Previewed:
            return "Previewed";
        case State::Activated:
            return "Activated";
        case State::Canceled:
            return "Canceled";
        }
        return "Unknown";
    }

const char*DebugNameForEditorRenderRecipeCommandKind(
        const EditorRenderRecipeCommandKind kind) noexcept
    {
        using Kind = EditorRenderRecipeCommandKind;
        switch (kind)
        {
        case Kind::UpdateDraft:
            return "UpdateDraft";
        case Kind::ValidateDraft:
            return "ValidateDraft";
        case Kind::PreviewDraft:
            return "PreviewDraft";
        case Kind::ActivatePreview:
            return "ActivatePreview";
        case Kind::CancelDraft:
            return "CancelDraft";
        case Kind::PublishArtifact:
            return "PublishArtifact";
        case Kind::ApplyArtifact:
            return "ApplyArtifact";
        }
        return "Unknown";
    }

const char*DebugNameForEditorRenderRecipeCommandStatus(
        const EditorRenderRecipeCommandStatus status) noexcept
    {
        using Status = EditorRenderRecipeCommandStatus;
        switch (status)
        {
        case Status::NoChange:
            return "NoChange";
        case Status::DraftUpdated:
            return "DraftUpdated";
        case Status::Debounced:
            return "Debounced";
        case Status::Validated:
            return "Validated";
        case Status::ValidationFailed:
            return "ValidationFailed";
        case Status::Previewed:
            return "Previewed";
        case Status::PreviewFailed:
            return "PreviewFailed";
        case Status::Activated:
            return "Activated";
        case Status::Canceled:
            return "Canceled";
        case Status::Published:
            return "Published";
        case Status::Applied:
            return "Applied";
        case Status::ActivationFailed:
            return "ActivationFailed";
        case Status::MissingRecipeContext:
            return "MissingRecipeContext";
        case Status::MissingEditorState:
            return "MissingEditorState";
        case Status::MissingArtifactRegistry:
            return "MissingArtifactRegistry";
        case Status::ArtifactCommandFailed:
            return "ArtifactCommandFailed";
        }
        return "Unknown";
    }

EditorGpuProfilingConfigResult
ApplyEditorGpuProfilingConfigCommand(
        const EditorRenderRecipeEditingContext& context,
        const bool enabled,
        std::string sourceId)
    {
        EditorGpuProfilingConfigResult result{};
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            result.Status =
                EditorGpuProfilingConfigStatus::MissingConfigControl;
            result.Message =
        "GPU profiling config requires the engine config-control module.";
            return result;
        }

        Core::Config::EngineConfig candidate =
            context.EngineConfigControlState->ActiveConfig;
        candidate.Render.EnableGpuProfiling = enabled;
        if (sourceId.empty())
        {
            sourceId = "sandbox.frame_graph.gpu_profiling";
        }
        result.Preview = context.PreviewEngineConfigDocument(
            Core::Config::SerializeEngineConfig(candidate),
            sourceId);
        if (!Core::Config::IsConfigUsable(result.Preview))
        {
            result.Status =
                EditorGpuProfilingConfigStatus::PreviewRejected;
            result.Message =
                "GPU profiling config preview was rejected.";
            return result;
        }

        result.Apply = context.ApplyEngineConfigHotSubset(result.Preview);
        if (!result.Apply.Succeeded())
        {
            result.Status =
                EditorGpuProfilingConfigStatus::ApplyRejected;
            result.Message =
                "GPU profiling config hot-apply was rejected.";
            return result;
        }

        result.Status =
            result.Apply.Status == RuntimeEngineConfigApplyStatus::NoChange
                ? EditorGpuProfilingConfigStatus::NoChange
                : EditorGpuProfilingConfigStatus::Applied;
        result.Message =
            result.Status == EditorGpuProfilingConfigStatus::NoChange
                ? "GPU profiling config unchanged."
                : "GPU profiling config applied.";
        return result;
    }
}
