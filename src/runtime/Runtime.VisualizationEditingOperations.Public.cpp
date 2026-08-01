module;

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.VisualizationEditingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;

namespace Extrinsic::Runtime {
struct EditorVisualizationEditingCommands::State {
  explicit State(EditorVisualizationEditingContext context)
      : Context(std::move(context)) {}

  EditorVisualizationEditingContext Context{};
};

EditorVisualizationEditingCommands::EditorVisualizationEditingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorVisualizationEditingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

EditorVisualizationEditingCommands::
operator const EditorVisualizationEditingContext &() const noexcept {
  static const EditorVisualizationEditingContext empty{};
  return m_State != nullptr ? m_State->Context : empty;
}

EditorVisualizationEditingCommands BindEditorVisualizationEditingCommands(
    EditorVisualizationEditingContext context) {
  return EditorVisualizationEditingCommands{
      std::make_shared<EditorVisualizationEditingCommands::State>(
          std::move(context))};
}

bool IsEditorTextureBakeServiceAttached(
    const EditorVisualizationEditingContext &context) noexcept {
  return (!context.AttachmentActive || context.AttachmentActive()) &&
         context.TextureBake != nullptr;
}

const char *DebugNameForEditorVisualizationColorSource(
    Graphics::Components::VisualizationConfig::ColorSource source) noexcept {
  return EditorFeatureDetail::DebugNameForEditorVisualizationColorSourceImpl(
      source);
}

const char *DebugNameForEditorVisualizationDomain(
    Graphics::Components::VisualizationConfig::Domain domain) noexcept {
  return EditorFeatureDetail::DebugNameForEditorVisualizationDomainImpl(domain);
}

const char *DebugNameForEditorVisualizationRecipeKind(
    VisualizationRecipeKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorVisualizationRecipeKindImpl(
      kind);
}

const char *DebugNameForEditorVisualizationPropertyDomain(
    EditorVisualizationPropertyDomain domain) noexcept {
  return EditorFeatureDetail::DebugNameForEditorVisualizationPropertyDomainImpl(
      domain);
}

const char *DebugNameForEditorVisualizationPropertyPreset(
    EditorVisualizationPropertyPreset preset) noexcept {
  return EditorFeatureDetail::DebugNameForEditorVisualizationPropertyPresetImpl(
      preset);
}

const char *DebugNameForEditorVisualizationTarget(
    EditorVisualizationTarget target) noexcept {
  return EditorFeatureDetail::DebugNameForEditorVisualizationTargetImpl(target);
}

const char *DebugNameForEditorPropertyCatalogDomain(
    EditorPropertyCatalogDomain domain) noexcept {
  return EditorFeatureDetail::DebugNameForEditorPropertyCatalogDomainImpl(
      domain);
}

const char *DebugNameForEditorBoundRenderStateRowKind(
    EditorBoundRenderStateRowKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorBoundRenderStateRowKindImpl(
      kind);
}

bool IsEditorTextureBakeTargetCompatible(
    const EditorTextureBakeTarget &target,
    Geometry::PropertyValueKind valueKind, PropertyTextureBakeStorage storage,
    PropertyTextureBakeEncoding encoding) noexcept {
  return EditorFeatureDetail::IsEditorTextureBakeTargetCompatibleImpl(
      target, valueKind, storage, encoding);
}

PropertyTextureBakeRepresentation ResolveEditorTextureBakeTargetRepresentation(
    Geometry::PropertyValueKind valueKind,
    PropertyTextureBakeStorage requestedStorage,
    PropertyTextureBakeEncoding requestedEncoding,
    std::span<const EditorTextureBakeTarget> targets) noexcept {
  return EditorFeatureDetail::ResolveEditorTextureBakeTargetRepresentationImpl(
      valueKind, requestedStorage, requestedEncoding, targets);
}

EditorCommandStatus
ApplyEditorRenderHintCommand(const EditorVisualizationEditingContext &context,
                             const EditorRenderHintCommand &command) {
  return EditorFeatureDetail::ApplyEditorRenderHintCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorVisualizationConfigCommand(
    const EditorVisualizationEditingContext &context,
    const EditorVisualizationConfigCommand &command) {
  return EditorFeatureDetail::ApplyEditorVisualizationConfigCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorVisualizationPropertyCommand(
    const EditorVisualizationEditingContext &context,
    const EditorVisualizationPropertyCommand &command) {
  return EditorFeatureDetail::ApplyEditorVisualizationPropertyCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorVisualizationRecipeCommand(
    const EditorVisualizationEditingContext &context,
    const EditorVisualizationRecipeCommand &command) {
  return EditorFeatureDetail::ApplyEditorVisualizationRecipeCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorVertexChannelBindingCommand(
    const EditorVisualizationEditingContext &context,
    const EditorVertexChannelBindingCommand &command) {
  return EditorFeatureDetail::ApplyEditorVertexChannelBindingCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorGeometryPresentationSlotDefaultCommand(
    const EditorVisualizationEditingContext &context,
    const EditorGeometryPresentationSlotDefaultCommand &command) {
  return EditorFeatureDetail::
      ApplyEditorGeometryPresentationSlotDefaultCommandImpl(
          EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorCommandStatus ApplyEditorGeometryPresentationSlotPropertyCommand(
    const EditorVisualizationEditingContext &context,
    const EditorGeometryPresentationSlotPropertyCommand &command) {
  return EditorFeatureDetail::
      ApplyEditorGeometryPresentationSlotPropertyCommandImpl(
          EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorTextureBakeCommandResult
ApplyEditorTextureBakeCommand(const EditorVisualizationEditingContext &context,
                              const EditorTextureBakeCommand &command) {
  return EditorFeatureDetail::ApplyEditorTextureBakeCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

TextureBakeMutationResult
RenameEditorBakedTexture(const EditorVisualizationEditingContext &context,
                         std::uint32_t stableEntityId,
                         std::string_view currentName,
                         std::string_view newName) {
  return EditorFeatureDetail::RenameEditorBakedTextureImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), stableEntityId,
      currentName, newName);
}

TextureBakeMutationResult
RemoveEditorBakedTexture(const EditorVisualizationEditingContext &context,
                         std::uint32_t stableEntityId,
                         std::string_view outputName) {
  return EditorFeatureDetail::RemoveEditorBakedTextureImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), stableEntityId,
      outputName);
}

TextureBakeMutationResult SetEditorBakedTextureTargets(
    const EditorVisualizationEditingContext &context,
    const EditorTextureBakeTargetUpdateRequest &request) {
  return EditorFeatureDetail::SetEditorBakedTextureTargetsImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), request);
}
} // namespace Extrinsic::Runtime
