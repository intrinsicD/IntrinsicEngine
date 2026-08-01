module;

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.VisualizationEditingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
namespace {
EditorVisualizationEditingContext MakeExpiredVisualizationEditingContext(
    EditorVisualizationEditingContext context) {
  context.AttachmentActive = [] { return false; };
  return EditorFeatureDetail::MakeEditorVisualizationEditingContext(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}
} // namespace

struct EditorVisualizationEditingCommands::State {
  explicit State(EditorVisualizationEditingContext context)
      : Context(std::move(context)),
        ExpiredContext(MakeExpiredVisualizationEditingContext(Context)) {}

  EditorVisualizationEditingContext Context{};
  EditorVisualizationEditingContext ExpiredContext{};
};

EditorVisualizationEditingCommands::EditorVisualizationEditingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorVisualizationEditingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

const EditorVisualizationEditingContext *
EditorVisualizationEditingCommandsAccess::Resolve(
    const EditorVisualizationEditingCommands &commands) noexcept {
  if (commands.m_State == nullptr)
    return nullptr;
  return commands.IsBound() ? &commands.m_State->Context
                            : &commands.m_State->ExpiredContext;
}

namespace {
const EditorVisualizationEditingContext &ContextOrEmpty(
    const EditorVisualizationEditingCommands &commands) noexcept {
  static const EditorVisualizationEditingContext empty{};
  const EditorVisualizationEditingContext *context =
      EditorVisualizationEditingCommandsAccess::Resolve(commands);
  return context != nullptr ? *context : empty;
}
} // namespace

EditorVisualizationEditingCommands BindEditorVisualizationEditingCommands(
    EditorVisualizationEditingContext context) {
  return EditorVisualizationEditingCommands{
      std::make_shared<EditorVisualizationEditingCommands::State>(
          std::move(context))};
}

EditorVisualizationEditingPreparedFrame PrepareEditorVisualizationEditingFrame(
    const EditorWorkspaceAttachment &attachment) {
  EditorVisualizationEditingPreparedFrame prepared{};
  const auto state =
      EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
  if (state == nullptr)
    return prepared;

  (void)state->Session.VisitPreparedFrame(
      [&prepared](EditorFeatureDetail::EditorWorkspacePreparedFrame frame) {
        prepared.Commands = BindEditorVisualizationEditingCommands(
            EditorFeatureDetail::MakeEditorVisualizationEditingContext(
                frame.Context));
      });
  return prepared;
}

bool IsEditorTextureBakeServiceAttached(
    const EditorVisualizationEditingContext &context) noexcept {
  return (!context.AttachmentActive || context.AttachmentActive()) &&
         context.TextureBake != nullptr;
}

bool IsEditorTextureBakeServiceAttached(
    const EditorVisualizationEditingCommands &commands) noexcept {
  return IsEditorTextureBakeServiceAttached(ContextOrEmpty(commands));
}

EditorCommandStatus ApplyEditorRenderHintCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorRenderHintCommand &command) {
  return ApplyEditorRenderHintCommand(ContextOrEmpty(commands), command);
}

EditorCommandStatus ApplyEditorVisualizationConfigCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorVisualizationConfigCommand &command) {
  return ApplyEditorVisualizationConfigCommand(ContextOrEmpty(commands),
                                               command);
}

EditorCommandStatus ApplyEditorVisualizationPropertyCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorVisualizationPropertyCommand &command) {
  return ApplyEditorVisualizationPropertyCommand(ContextOrEmpty(commands),
                                                 command);
}

EditorCommandStatus ApplyEditorVisualizationRecipeCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorVisualizationRecipeCommand &command) {
  return ApplyEditorVisualizationRecipeCommand(ContextOrEmpty(commands),
                                               command);
}

EditorCommandStatus ApplyEditorVertexChannelBindingCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorVertexChannelBindingCommand &command) {
  return ApplyEditorVertexChannelBindingCommand(ContextOrEmpty(commands),
                                                command);
}

EditorCommandStatus ApplyEditorGeometryPresentationSlotDefaultCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorGeometryPresentationSlotDefaultCommand &command) {
  return ApplyEditorGeometryPresentationSlotDefaultCommand(
      ContextOrEmpty(commands), command);
}

EditorCommandStatus ApplyEditorGeometryPresentationSlotPropertyCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorGeometryPresentationSlotPropertyCommand &command) {
  return ApplyEditorGeometryPresentationSlotPropertyCommand(
      ContextOrEmpty(commands), command);
}

EditorTextureBakeCommandResult ApplyEditorTextureBakeCommand(
    const EditorVisualizationEditingCommands &commands,
    const EditorTextureBakeCommand &command) {
  return ApplyEditorTextureBakeCommand(ContextOrEmpty(commands), command);
}

TextureBakeMutationResult RenameEditorBakedTexture(
    const EditorVisualizationEditingCommands &commands,
    std::uint32_t stableEntityId,
    std::string_view currentName,
    std::string_view newName) {
  return RenameEditorBakedTexture(ContextOrEmpty(commands), stableEntityId,
                                  currentName, newName);
}

TextureBakeMutationResult RemoveEditorBakedTexture(
    const EditorVisualizationEditingCommands &commands,
    std::uint32_t stableEntityId,
    std::string_view outputName) {
  return RemoveEditorBakedTexture(ContextOrEmpty(commands), stableEntityId,
                                  outputName);
}

TextureBakeMutationResult SetEditorBakedTextureTargets(
    const EditorVisualizationEditingCommands &commands,
    const EditorTextureBakeTargetUpdateRequest &request) {
  return SetEditorBakedTextureTargets(ContextOrEmpty(commands), request);
}
} // namespace Extrinsic::Runtime
