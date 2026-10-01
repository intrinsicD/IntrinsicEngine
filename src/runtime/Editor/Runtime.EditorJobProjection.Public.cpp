module;

#include <string>
#include <string_view>
#include <variant>
#include <vector>

module Extrinsic.Runtime.EditorJobProjection;

namespace Extrinsic::Runtime {
EditorJobScope ToEditorJobScope(GeometryElementDomain domain) noexcept {
  switch (domain) {
  case GeometryElementDomain::MeshVertex: return EditorJobScope::MeshVertex;
  case GeometryElementDomain::MeshEdge: return EditorJobScope::MeshEdge;
  case GeometryElementDomain::MeshHalfedge: return EditorJobScope::MeshHalfedge;
  case GeometryElementDomain::MeshFace: return EditorJobScope::MeshFace;
  case GeometryElementDomain::GraphNode: return EditorJobScope::GraphNode;
  case GeometryElementDomain::GraphHalfedge: return EditorJobScope::GraphHalfedge;
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

EditorOperationState ToEditorOperationState(JobState state) noexcept {
  switch (state) {
  case JobState::AwaitingDependencies:
  case JobState::Queued:
    return EditorOperationState::Queued;
  case JobState::Running:
  case JobState::AwaitingGate:
  case JobState::AwaitingApply:
    return EditorOperationState::Running;
  case JobState::Published:
    return EditorOperationState::Succeeded;
  case JobState::Cancelled:
    return EditorOperationState::Cancelled;
  case JobState::Rejected:
  case JobState::Dropped:
  case JobState::StaleDiscarded:
    return EditorOperationState::Failed;
  default:
    return EditorOperationState::None;
  }
}

EditorOperationProgress ProjectEditorOperationProgress(const EditorJobRecord &job) {
  EditorOperationProgress progress{};
  progress.State = ToEditorOperationState(job.State);
  if (progress.State == EditorOperationState::None)
    return progress;
  progress.Label = job.Name;
  progress.ElapsedSeconds = static_cast<double>(job.ElapsedMilliseconds) / 1000.0;
  progress.Diagnostic = job.Diagnostic;
  switch (progress.State) {
  case EditorOperationState::Queued:
    break; // nothing has run; the bar stays indeterminate
  case EditorOperationState::Succeeded:
    progress.Determinate = true;
    progress.Normalized = 1.0f;
    break;
  default:
    progress.Determinate = job.ProgressDeterminate;
    progress.Normalized = job.ProgressDeterminate ? job.NormalizedProgress : 0.0f;
    break;
  }
  if (progress.Diagnostic.empty() &&
      (progress.State == EditorOperationState::Failed ||
       progress.State == EditorOperationState::Cancelled))
    progress.Diagnostic = std::string{ToString(job.State)};
  return progress;
}

EditorOperationProgress
ResolveEditorOperationProgress(const std::vector<EditorJobRecord> &records,
                               const EditorOperationRunKey &key) {
  const EditorJobRecord *active = nullptr;
  const EditorJobRecord *terminal = nullptr;
  // Tokens grow with submission, so the higher index is the newer run.
  const auto newer = [](const EditorJobRecord *best, const EditorJobRecord &job) {
    return best == nullptr || job.Token.Index >= best->Token.Index;
  };
  for (const EditorJobRecord &job : records) {
    const bool matches =
        std::holds_alternative<EditorJobIdentity>(key)
            ? SameEditorJobOutput(job.Identity, std::get<EditorJobIdentity>(key))
            : (std::get<CommandCorrelationId>(key).IsValid() &&
               job.CorrelationId == std::get<CommandCorrelationId>(key).Value);
    if (!matches)
      continue;
    if (IsActiveEditorJobState(job.State)) {
      if (newer(active, job)) active = &job;
    } else if (ToEditorOperationState(job.State) != EditorOperationState::None) {
      if (newer(terminal, job)) terminal = &job;
    }
  }
  const EditorJobRecord *picked = active != nullptr ? active : terminal;
  return picked != nullptr ? ProjectEditorOperationProgress(*picked)
                           : EditorOperationProgress{};
}
} // namespace Extrinsic::Runtime
