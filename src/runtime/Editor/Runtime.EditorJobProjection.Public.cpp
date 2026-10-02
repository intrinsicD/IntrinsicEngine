module;

#include <algorithm>
#include <optional>
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

std::optional<EditorJobRecord>
FindEditorOperationRun(const std::vector<EditorJobRecord> &records,
                       const EditorOperationRunKey &key) {
  const EditorJobRecord *active = nullptr;
  const EditorJobRecord *terminal = nullptr;
  // Tokens grow with submission, so the higher index is the newer run.
  const auto newer = [](const EditorJobRecord *best, const EditorJobRecord &job) {
    return best == nullptr || job.Token.Index >= best->Token.Index;
  };
  for (const EditorJobRecord &job : records) {
    bool matches = false;
    if (const auto *identity = std::get_if<EditorJobIdentity>(&key))
      matches = identity->Scope != EditorJobScope::Unknown &&
                job.Identity.Scope != EditorJobScope::Unknown &&
                SameEditorJobOutput(job.Identity, *identity);
    else if (const auto *correlation = std::get_if<EditorRunCorrelation>(&key))
      matches = correlation->IsValid() && job.CorrelationId == correlation->Value;
    else if (const auto *output = std::get_if<EditorOutputRef>(&key))
      // A correlation-only record has entity 0, so a real entity never matches it; the scope
      // may legitimately be Unknown (an unresolved positions domain).
      matches = output->EntityId != 0u && !output->OutputName.empty() &&
                job.Identity.EntityId == output->EntityId &&
                job.Identity.OutputName == output->OutputName;
    else
      matches = job.Token == std::get<JobToken>(key);
    if (!matches)
      continue;
    if (IsActiveEditorJobState(job.State)) {
      if (newer(active, job)) active = &job;
    } else if (ToEditorOperationState(job.State) != EditorOperationState::None) {
      if (newer(terminal, job)) terminal = &job;
    }
  }
  const EditorJobRecord *picked = active != nullptr ? active : terminal;
  if (picked == nullptr)
    return std::nullopt;
  return *picked;
}

EditorOperationProgress
ResolveEditorOperationProgress(const std::vector<EditorJobRecord> &records,
                               const EditorOperationRunKey &key) {
  const std::optional<EditorJobRecord> run = FindEditorOperationRun(records, key);
  return run.has_value() ? ProjectEditorOperationProgress(*run)
                         : EditorOperationProgress{};
}

std::string_view ToString(const EditorJobCancelStatus status) noexcept {
  switch (status) {
  case EditorJobCancelStatus::Requested: return "requested";
  case EditorJobCancelStatus::NotActive: return "not_active";
  case EditorJobCancelStatus::NotEditorJob: return "not_editor_job";
  case EditorJobCancelStatus::Unavailable: return "unavailable";
  }
  return "unavailable";
}
} // namespace Extrinsic::Runtime
