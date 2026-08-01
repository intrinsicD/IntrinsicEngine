module;

#include <cstdint>
#include <memory>
#include <utility>

module Extrinsic.Runtime.SceneEditingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;

namespace Extrinsic::Runtime {
struct EditorSceneEditingCommands::State {
  explicit State(EditorSceneEditingContext context)
      : Context(std::move(context)) {}

  EditorSceneEditingContext Context{};
};

EditorSceneEditingCommands::EditorSceneEditingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorSceneEditingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

EditorSceneEditingCommands::operator const EditorSceneEditingContext &()
    const noexcept {
  static const EditorSceneEditingContext empty{};
  return m_State != nullptr ? m_State->Context : empty;
}

EditorSceneEditingCommands
BindEditorSceneEditingCommands(EditorSceneEditingContext context) {
  return EditorSceneEditingCommands{
      std::make_shared<EditorSceneEditingCommands::State>(std::move(context))};
}

const char *
DebugNameForEditorAssetPayloadKind(EditorAssetPayloadKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorAssetPayloadKindImpl(kind);
}

const char *
DebugNameForEditorPrimitiveKind(RefinedPrimitiveKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorPrimitiveKindImpl(kind);
}

const char *DebugNameForEditorCameraControllerKind(
    Core::Config::CameraControllerKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorCameraControllerKindImpl(kind);
}

bool SelectEditorEntity(const EditorSceneEditingContext &context,
                        std::uint32_t stableEntityId) {
  return EditorFeatureDetail::SelectEditorEntityImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context),
      stableEntityId);
}

EditorFileImportResult
ApplyEditorFileImportCommand(const EditorSceneEditingContext &context,
                             const EditorFileImportCommand &command) {
  return EditorFeatureDetail::ApplyEditorFileImportCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorSceneFileResult
ApplyEditorSceneSaveCommand(const EditorSceneEditingContext &context,
                            const EditorSceneFileCommand &command) {
  return EditorFeatureDetail::ApplyEditorSceneSaveCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorSceneFileResult
ApplyEditorSceneLoadCommand(const EditorSceneEditingContext &context,
                            const EditorSceneFileCommand &command) {
  return EditorFeatureDetail::ApplyEditorSceneLoadCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorSceneFileResult
ApplyEditorNewSceneCommand(const EditorSceneEditingContext &context) {
  return EditorFeatureDetail::ApplyEditorNewSceneCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

EditorSceneFileResult
ApplyEditorCloseSceneCommand(const EditorSceneEditingContext &context) {
  return EditorFeatureDetail::ApplyEditorCloseSceneCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

EditorCommandStatus
ApplyEditorTransformEdit(const EditorSceneEditingContext &context,
                         const EditorTransformEditCommand &command) {
  return EditorFeatureDetail::ApplyEditorTransformEditImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorCameraControllerCommand(
    const EditorSceneEditingContext &context,
    const EditorCameraControllerCommand &command) {
  return EditorFeatureDetail::ApplyEditorCameraControllerCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus
ApplyEditorPrimitiveViewCommand(const EditorSceneEditingContext &context,
                                const EditorPrimitiveViewCommand &command) {
  return EditorFeatureDetail::ApplyEditorPrimitiveViewCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}
} // namespace Extrinsic::Runtime
