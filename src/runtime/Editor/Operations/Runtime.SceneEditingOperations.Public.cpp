module;

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

module Extrinsic.Runtime.SceneEditingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
namespace {
EditorSceneEditingContext
MakeExpiredSceneEditingContext(EditorSceneEditingContext context) {
  context.AttachmentActive = [] { return false; };
  return EditorFeatureDetail::MakeEditorSceneEditingContext(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}
} // namespace

struct EditorSceneEditingCommands::State {
  explicit State(EditorSceneEditingContext context)
      : Context(std::move(context)),
        ExpiredContext(MakeExpiredSceneEditingContext(Context)) {}

  EditorSceneEditingContext Context{};
  EditorSceneEditingContext ExpiredContext{};
};

EditorSceneEditingCommands::EditorSceneEditingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorSceneEditingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

const EditorSceneEditingContext *EditorSceneEditingCommandsAccess::Resolve(
    const EditorSceneEditingCommands &commands) noexcept {
  if (commands.m_State == nullptr)
    return nullptr;
  return commands.IsBound() ? &commands.m_State->Context
                            : &commands.m_State->ExpiredContext;
}

namespace {
const EditorSceneEditingContext &ContextOrEmpty(
    const EditorSceneEditingCommands &commands) noexcept {
  static const EditorSceneEditingContext empty{};
  const EditorSceneEditingContext *context =
      EditorSceneEditingCommandsAccess::Resolve(commands);
  return context != nullptr ? *context : empty;
}
} // namespace

EditorSceneEditingCommands
BindEditorSceneEditingCommands(EditorSceneEditingContext context) {
  return EditorSceneEditingCommands{
      std::make_shared<EditorSceneEditingCommands::State>(std::move(context))};
}

EditorSceneEditingPreparedFrame PrepareEditorSceneEditingFrame(
    const EditorWorkspaceAttachment &attachment) {
  EditorSceneEditingPreparedFrame prepared{};
  const auto state =
      EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
  if (state == nullptr)
    return prepared;

  (void)state->Session.VisitPreparedFrame(
      [&prepared](EditorFeatureDetail::EditorWorkspacePreparedFrame frame) {
        const EditorFeatureDetail::EditorFeatureBindings &bindings =
            frame.Context;
        const EditorSceneEditingContext context =
            EditorFeatureDetail::MakeEditorSceneEditingContext(bindings);
        prepared.Commands = BindEditorSceneEditingCommands(context);
        prepared.AssetImportQueueCommands = context.AssetImportQueueCommands;
        prepared.SceneAvailable = context.Scene != nullptr;
        if (frame.LastAssetImportResult.has_value())
          prepared.LastAssetImportResult = frame.LastAssetImportResult;
        if (frame.LastSceneFileResult.has_value())
          prepared.LastSceneFileResult = frame.LastSceneFileResult;

        if (bindings.CommandHistory != nullptr) {
          EditorCommandHistory *history = bindings.CommandHistory;
          const std::function<bool()> attachmentActive =
              bindings.AttachmentActive;
          prepared.DocumentCommands.Undo = [history, attachmentActive]() {
            if (attachmentActive && !attachmentActive())
              return EditorCommandHistoryResult{};
            return history->Undo();
          };
          prepared.DocumentCommands.Redo = [history, attachmentActive]() {
            if (attachmentActive && !attachmentActive())
              return EditorCommandHistoryResult{};
            return history->Redo();
          };
        }
      });
  return prepared;
}

bool SelectEditorEntity(const EditorSceneEditingCommands &commands,
                        std::uint32_t stableEntityId) {
  return SelectEditorEntity(ContextOrEmpty(commands), stableEntityId);
}

EditorFileImportResult
ApplyEditorFileImportCommand(const EditorSceneEditingCommands &commands,
                             const EditorFileImportCommand &command) {
  return ApplyEditorFileImportCommand(ContextOrEmpty(commands), command);
}

EditorSceneFileResult
ApplyEditorSceneSaveCommand(const EditorSceneEditingCommands &commands,
                            const EditorSceneFileCommand &command) {
  return ApplyEditorSceneSaveCommand(ContextOrEmpty(commands), command);
}

EditorSceneFileResult
ApplyEditorSceneLoadCommand(const EditorSceneEditingCommands &commands,
                            const EditorSceneFileCommand &command) {
  return ApplyEditorSceneLoadCommand(ContextOrEmpty(commands), command);
}

EditorSceneFileResult
ApplyEditorNewSceneCommand(const EditorSceneEditingCommands &commands) {
  return ApplyEditorNewSceneCommand(ContextOrEmpty(commands));
}

EditorSceneFileResult
ApplyEditorCloseSceneCommand(const EditorSceneEditingCommands &commands) {
  return ApplyEditorCloseSceneCommand(ContextOrEmpty(commands));
}

EditorCommandStatus
ApplyEditorTransformEdit(const EditorSceneEditingCommands &commands,
                         const EditorTransformEditCommand &command) {
  return ApplyEditorTransformEdit(ContextOrEmpty(commands), command);
}

EditorCommandStatus ApplyEditorCameraControllerCommand(
    const EditorSceneEditingCommands &commands,
    const EditorCameraControllerCommand &command) {
  return ApplyEditorCameraControllerCommand(ContextOrEmpty(commands), command);
}

EditorCommandStatus
ApplyEditorPrimitiveViewCommand(const EditorSceneEditingCommands &commands,
                                const EditorPrimitiveViewCommand &command) {
  return ApplyEditorPrimitiveViewCommand(ContextOrEmpty(commands), command);
}
} // namespace Extrinsic::Runtime
