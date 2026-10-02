module;

#include <algorithm>
#include <optional>
#include <span>
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

std::optional<EditorJobDomain> EditorJobDomainOfBackend(const std::string_view backend) noexcept {
  const auto contains = [backend](const std::string_view word) {
    return std::ranges::search(backend, word, [](const char a, const char b) {
             return (a >= 'A' && a <= 'Z' ? char(a - 'A' + 'a') : a) == b;
           }).begin() != backend.end();
  };
  if (contains("vulkan") || contains("gpu"))
    return EditorJobDomain::GpuCompute;
  if (contains("cpu"))
    return EditorJobDomain::Cpu;
  if (contains("auto"))
    return EditorJobDomain::Auto;
  return std::nullopt;
}

std::string_view ToString(const EditorJobDomain domain) noexcept {
  switch (domain) {
  case EditorJobDomain::Cpu: return "cpu";
  case EditorJobDomain::GpuCompute: return "gpu_compute";
  case EditorJobDomain::GpuGraphics: return "gpu_graphics";
  case EditorJobDomain::Auto: return "auto";
  }
  return "unknown";
}

EditorJobRecord MakeEditorJobRecord(const JobSnapshot &job,
                                    const EditorJobIdentity &identity,
                                    const EditorJobOutcome *outcome) {
  EditorJobRecord record{
      .Token = job.Token,
      .Identity = identity,
      .CorrelationId = job.CorrelationId,
      .Name = job.DebugName,
      .State = job.State,
      .RequestedJobDomain = identity.RequestedDomain,
      .NormalizedProgress = job.Progress.Normalized,
      .ProgressDeterminate = job.Progress.Determinate,
      .ElapsedMilliseconds = job.ElapsedMilliseconds,
  };
  if (outcome != nullptr) {
    record.ResolvedJobDomain = outcome->ResolvedDomain;
    record.Diagnostic = outcome->Diagnostic;
  }
  // A CPU request has nowhere to fall back to.
  if (!record.ResolvedJobDomain && identity.RequestedDomain == EditorJobDomain::Cpu)
    record.ResolvedJobDomain = EditorJobDomain::Cpu;
  return record;
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

EditorRunCancelCount CancelEditorRuns(const EditorJobCommandSurface &surface,
                                      const std::span<const JobToken> runs) {
  EditorRunCancelCount count{};
  if (!surface.SnapshotAll || !surface.Cancel) {
    count.Unavailable = true;
    return count;
  }
  const auto listed = [&](const JobToken token) {
    return std::find(runs.begin(), runs.end(), token) != runs.end();
  };
  const std::vector<EditorJobRecord> jobs = surface.SnapshotAll();
  for (const JobToken run : runs) {
    const auto head = std::find_if(jobs.begin(), jobs.end(),
                                   [&](const EditorJobRecord &job) { return job.Token == run; });
    if (head != jobs.end() && head->Identity.Run.IsValid() && head->Identity.Run != run &&
        listed(head->Identity.Run))
      continue; // reached through the run it joined
    for (const EditorJobRecord &job : jobs) {
      if ((job.Token != run && job.Identity.Run != run) || !IsActiveEditorJobState(job.State))
        continue;
      switch (surface.Cancel(job.Token)) {
      case EditorJobCancelStatus::Requested: ++count.Requested; break;
      case EditorJobCancelStatus::Unavailable: count.Unavailable = true; break;
      default: ++count.Refused; break;
      }
    }
  }
  return count;
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
