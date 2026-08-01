module;

#include <string>

module Extrinsic.Runtime.EditorJobProjection;

namespace Extrinsic::Runtime {
EditorJobScope ToEditorJobScope(GeometryElementDomain domain) noexcept {
  switch (domain) {
  case GeometryElementDomain::MeshVertex: return EditorJobScope::MeshVertex;
  case GeometryElementDomain::MeshEdge: return EditorJobScope::MeshEdge;
  case GeometryElementDomain::MeshHalfedge: return EditorJobScope::MeshHalfedge;
  case GeometryElementDomain::MeshFace: return EditorJobScope::MeshFace;
  case GeometryElementDomain::GraphNode: return EditorJobScope::GraphNode;
  case GeometryElementDomain::GraphEdge: return EditorJobScope::GraphEdge;
  case GeometryElementDomain::PointCloudPoint: return EditorJobScope::PointCloudPoint;
  default: return EditorJobScope::Unknown;
  }
}

bool SameEditorJobOutput(const EditorJobIdentity &lhs,
                         const EditorJobIdentity &rhs) noexcept {
  return lhs.EntityId == rhs.EntityId && lhs.Scope == rhs.Scope &&
         lhs.OutputSemantic == rhs.OutputSemantic && lhs.OutputName == rhs.OutputName;
}

bool IsActiveEditorJobState(JobState state) noexcept {
  switch (state) {
  case JobState::AwaitingDependencies:
  case JobState::Queued:
  case JobState::Running:
  case JobState::AwaitingGate:
  case JobState::AwaitingApply:
    return true;
  default:
    return false;
  }
}

bool IsFailedEditorJobState(JobState state) noexcept {
  switch (state) {
  case JobState::Rejected:
  case JobState::Dropped:
  case JobState::Cancelled:
  case JobState::StaleDiscarded:
    return true;
  default:
    return false;
  }
}
} // namespace Extrinsic::Runtime
