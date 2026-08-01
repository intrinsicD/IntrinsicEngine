module;

module Extrinsic.Runtime.EditorJobProjection;

import Extrinsic.Runtime.Private.EditorFeatures;

namespace Extrinsic::Runtime {
EditorJobScope ToEditorJobScope(GeometryElementDomain domain) noexcept {
  return EditorFeatureDetail::ToEditorJobScopeImpl(domain);
}

bool SameEditorJobOutput(const EditorJobIdentity &lhs,
                         const EditorJobIdentity &rhs) noexcept {
  return EditorFeatureDetail::SameEditorJobOutputImpl(lhs, rhs);
}

bool IsActiveEditorJobState(JobState state) noexcept {
  return EditorFeatureDetail::IsActiveEditorJobStateImpl(state);
}

bool IsFailedEditorJobState(JobState state) noexcept {
  return EditorFeatureDetail::IsFailedEditorJobStateImpl(state);
}
} // namespace Extrinsic::Runtime
