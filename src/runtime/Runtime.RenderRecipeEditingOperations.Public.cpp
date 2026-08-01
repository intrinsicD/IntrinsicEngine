module;

#include <memory>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.RenderRecipeEditingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;

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

EditorRenderRecipeEditingCommands::
operator const EditorRenderRecipeEditingContext &() const noexcept {
  static const EditorRenderRecipeEditingContext empty{};
  return m_State != nullptr ? m_State->Context : empty;
}

EditorRenderRecipeEditingCommands BindEditorRenderRecipeEditingCommands(
    EditorRenderRecipeEditingContext context) {
  return EditorRenderRecipeEditingCommands{
      std::make_shared<EditorRenderRecipeEditingCommands::State>(
          std::move(context))};
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
} // namespace Extrinsic::Runtime
