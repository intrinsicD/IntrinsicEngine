module;

module Extrinsic.Runtime.EditorCommon;

import Extrinsic.Runtime.Private.EditorFeatures;

namespace Extrinsic::Runtime {
const char *
DebugNameForEditorDiagnosticCode(EditorDiagnosticCode code) noexcept {
  return EditorFeatureDetail::DebugNameForEditorDiagnosticCodeImpl(code);
}

const char *
DebugNameForEditorCommandStatus(EditorCommandStatus status) noexcept {
  return EditorFeatureDetail::DebugNameForEditorCommandStatusImpl(status);
}

const char *DebugNameForEditorGeometryDomain(
    ECS::Components::GeometrySources::Domain domain) noexcept {
  return EditorFeatureDetail::DebugNameForEditorGeometryDomainImpl(domain);
}

const char *
DebugNameForEditorDomainWindowKind(const EditorDomainWindowKind kind) noexcept {
  return EditorFeatureDetail::DebugNameForEditorDomainWindowKindImpl(kind);
}
} // namespace Extrinsic::Runtime
