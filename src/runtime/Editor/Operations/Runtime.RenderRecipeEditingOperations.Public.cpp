module;
#include <functional>
#include <memory>
#include <string>
#include <utility>

module Extrinsic.Runtime.RenderRecipeEditingOperations;

// The prepared frame is projected onto this family's own context only; the
// all-family bindings stay opaque here.
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
namespace {
EditorRenderRecipeEditingContext MakeExpiredRenderRecipeEditingContext(
    EditorRenderRecipeEditingContext context) {
  EditorRenderRecipeEditingContext expired{};
  expired.PreviewRenderRecipeDocument = std::move(context.PreviewRenderRecipeDocument);
  expired.ApplyRenderRecipePreview = std::move(context.ApplyRenderRecipePreview);
  expired.PreviewEngineConfigDocument = std::move(context.PreviewEngineConfigDocument);
  expired.ApplyEngineConfigHotSubset = std::move(context.ApplyEngineConfigHotSubset);
  expired.AttachmentActive = [] { return false; };
  return expired;
}
} // namespace

struct EditorRenderRecipeEditingCommands::State {
  explicit State(EditorRenderRecipeEditingContext context)
      : Context(std::move(context)),
        ExpiredContext(MakeExpiredRenderRecipeEditingContext(Context)) {}

  EditorRenderRecipeEditingContext Context{};
  EditorRenderRecipeEditingContext ExpiredContext{};
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
  if (commands.m_State == nullptr)
    return nullptr;
  return commands.IsBound() ? &commands.m_State->Context
                            : &commands.m_State->ExpiredContext;
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
