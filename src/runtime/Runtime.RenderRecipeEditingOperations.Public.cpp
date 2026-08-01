module;

#include <memory>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.RenderRecipeEditingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
struct EditorRenderRecipeEditingCommands::State {
  explicit State(EditorRenderRecipeEditingContext context)
      : Context(std::move(context)) {}

  EditorRenderRecipeEditingContext Context{};
};

EditorRenderRecipeEditingCommands::EditorRenderRecipeEditingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorRenderRecipeEditingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

const EditorRenderRecipeEditingContext *
EditorRenderRecipeEditingCommandsAccess::Resolve(
    const EditorRenderRecipeEditingCommands &commands) noexcept {
  return commands.IsBound() && commands.m_State != nullptr
             ? &commands.m_State->Context
             : nullptr;
}

namespace {
const EditorRenderRecipeEditingContext &ContextOrEmpty(
    const EditorRenderRecipeEditingCommands &commands) noexcept {
  static const EditorRenderRecipeEditingContext empty{};
  const EditorRenderRecipeEditingContext *context =
      EditorRenderRecipeEditingCommandsAccess::Resolve(commands);
  return context != nullptr ? *context : empty;
}
} // namespace

EditorRenderRecipeEditingCommands BindEditorRenderRecipeEditingCommands(
    EditorRenderRecipeEditingContext context) {
  return EditorRenderRecipeEditingCommands{
      std::make_shared<EditorRenderRecipeEditingCommands::State>(
          std::move(context))};
}

EditorRenderRecipeEditingPreparedFrame PrepareEditorRenderRecipeEditingFrame(
    const EditorWorkspaceAttachment &attachment) {
  EditorRenderRecipeEditingPreparedFrame prepared{};
  const auto state =
      EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
  if (state == nullptr)
    return prepared;

  (void)state->Session.VisitPreparedFrame(
      [&prepared](EditorFeatureDetail::EditorWorkspacePreparedFrame frame) {
        const EditorRenderRecipeEditingContext context =
            EditorFeatureDetail::MakeEditorRenderRecipeEditingContext(
                frame.Context);
        prepared.Commands = BindEditorRenderRecipeEditingCommands(context);
        if (context.RenderRecipeEditorState != nullptr) {
          prepared.Draft = EditorRenderRecipeDraftSnapshot{
              .DraftDocument = context.RenderRecipeEditorState->DraftDocument,
              .DraftState = context.RenderRecipeEditorState->DraftState,
              .DraftRevision = context.RenderRecipeEditorState->DraftRevision,
          };
        }
        prepared.CommandsAvailable =
            context.RenderRecipeContext != nullptr &&
            context.RenderRecipeEditorState != nullptr &&
            context.RenderRecipeCommandsAvailable;
        prepared.ArtifactCommandsAvailable =
            context.RenderArtifacts != nullptr &&
            context.RenderRecipeCommandsAvailable;
      });
  return prepared;
}

std::string_view DebugNameForEditorRenderRecipeConfigState(
    EditorRenderRecipeConfigState state) noexcept {
  return EditorFeatureDetail::DebugNameForEditorRenderRecipeConfigStateImpl(
      state);
}

std::string_view DebugNameForEditorRenderRecipeConfigDiagnosticCode(
    EditorRenderRecipeConfigDiagnosticCode code) noexcept {
  return EditorFeatureDetail::
      DebugNameForEditorRenderRecipeConfigDiagnosticCodeImpl(code);
}

const char *DebugNameForEditorRenderRecipeDraftState(
    EditorRenderRecipeDraftState state) noexcept {
  return EditorFeatureDetail::DebugNameForEditorRenderRecipeDraftStateImpl(
      state);
}

const char *DebugNameForEditorRenderRecipeCommandKind(
    EditorRenderRecipeCommandKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorRenderRecipeCommandKindImpl(
      kind);
}

const char *DebugNameForEditorRenderRecipeCommandStatus(
    EditorRenderRecipeCommandStatus status) noexcept {
  return EditorFeatureDetail::DebugNameForEditorRenderRecipeCommandStatusImpl(
      status);
}

EditorGpuProfilingConfigResult ApplyEditorGpuProfilingConfigCommand(
    const EditorRenderRecipeEditingContext &context, bool enabled,

    std::string sourceId) {
  return EditorFeatureDetail::ApplyEditorGpuProfilingConfigCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), enabled,
      sourceId);
}

EditorRenderRecipeEditorModel BuildEditorRenderRecipeEditorModel(
    const EditorRenderRecipeEditingContext &context) {
  return EditorFeatureDetail::BuildEditorRenderRecipeEditorModelImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

EditorRenderRecipeCommandResult
ApplyEditorRenderRecipeCommand(const EditorRenderRecipeEditingContext &context,
                               const EditorRenderRecipeCommand &command) {
  return EditorFeatureDetail::ApplyEditorRenderRecipeCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorGpuProfilingConfigResult ApplyEditorGpuProfilingConfigCommand(
    const EditorRenderRecipeEditingCommands &commands,
    bool enabled,
    std::string sourceId) {
  return ApplyEditorGpuProfilingConfigCommand(
      ContextOrEmpty(commands), enabled, std::move(sourceId));
}

EditorRenderRecipeEditorModel BuildEditorRenderRecipeEditorModel(
    const EditorRenderRecipeEditingCommands &commands) {
  return BuildEditorRenderRecipeEditorModel(ContextOrEmpty(commands));
}

EditorRenderRecipeCommandResult ApplyEditorRenderRecipeCommand(
    const EditorRenderRecipeEditingCommands &commands,
    const EditorRenderRecipeCommand &command) {
  return ApplyEditorRenderRecipeCommand(ContextOrEmpty(commands), command);
}
} // namespace Extrinsic::Runtime
