---
id: RUNTIME-313
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive consolidation follow-up of the 2026-10-01 duplication audit; evidence is the diff, contract tests, review and CI.
contract_schema: 1
contracts: [runtime.processing-compilation-locality, geometry.property-coherence, repo.source-documentation]
---
# RUNTIME-313 — One shared setup/completion helper for queued editor jobs

## Goal
Replace the hand-written queued-job prologue and epilogue in the editor processing
operations with one compiled helper at the canonical owner, and make the drift between
the copies impossible rather than merely fixed.

## Context
Source: 2026-10-01 duplication/consistency audit (finding 1.2), re-verified at `665c693dd`.
Each `Apply*` function in `src/runtime/Editor/Operations/` builds an `EditorJobIdentity`, calls
`MeshSupport::FindActiveEditorJob` + `IsActiveEditorJobState`, calls
`GuardEditorProcessingResult(sink)`, makes a `make_shared<bool> delivered` flag and a `pending`
copy, then defines its own `ValidateBeforeApply`, `PublishCompletion` and
`FinalizeUnpublishedOnMainThread`.

Sites (CPU-job setup; line numbers are at the audit revision and will drift):
`...Bilateral.cpp` (~335), `...Outliers.cpp` (~667), `...Density.cpp` (~278),
`...DensityWeights.cpp` (~204), `...Descriptors.cpp` (~291), `...Construction.cpp` (~565),
`...Keypoints.cpp` (~466), `...Normals.cpp` (~1424), plus `Runtime.RegistrationOperations.CoherentPointDrift.cpp`
and `Runtime.PointSamplingOperations.cpp` (not re-read; confirm they match the pattern).

Drift list (current, re-verified):
- Already-active message: the point operations write their own "... job for this output is already
  active." string (Bilateral, Outliers, Density via `Join`, DensityWeights, Descriptors, Keypoints,
  Construction, scalar transaction, Smoothing) instead of
  `MeshSupport::BuildActiveDerivedJobMessage` (`...MeshSupport.cpp:445`), which the topology,
  curvature and ICP paths use. Duplicate status was unified to `Pending` in RUNTIME-312 for
  keypoints; the helper must keep one status for all.
- `ValidateBeforeApply`: Bilateral returns `JobApplyValidation::Cancelled` when the job was
  abandoned; Outliers returns only `Current`/`StaleGeneration` and ignores `Abandoned`.
- `FinalizeUnpublishedOnMainThread`: Bilateral sets `*delivered = true` before calling the sink;
  Outliers calls the sink without setting it. Message wording for the stale/cancelled case differs
  per copy ("... cancelled or its source became stale; previous output retained.").
- Failure routing differs: Bilateral prefers `w->MainFailure`, Outliers prefers `w->Result` when it
  is `GeometryProcessingFailed`.

Owner/placement: beside `MakeFramedGpuJobDesc` (`src/runtime/Editor/internal/Runtime.EditorFramedGpuJob.hpp`)
or `Runtime.GeometryProcessingOperations.JobFailure.hpp`. Use the existing `JobDesc` fields; add no
service, registry, extra job hop or public template framework. The helper owns: the duplicate
check with one status and one message builder, deliver-once flag, and stale/cancel finalize with
one wording. Typed work, validation of method inputs and publication stay in each operation.

Relationship: [RUNTIME-311](RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md) owns the
GPU Run/Accept transaction that follows this setup; keep the helper usable by it. The agent lane's
`FinishApply` (`src/runtime/Agent/internal/Runtime.AgentOperations.Detail.hpp`) maps a `Pending`
duplicate to `result_unavailable`; the single status must keep that mapping.

- Inherited from RUNTIME-279 (2026-10-02):
  - Cancelling by output identity also cancels a newer run on the same output; key the cancel to the call's own run.
  - `RunWasCancelled` can relabel a real failure when an older cancelled job on that output is retained.
  - A reaped `jobs_wait` answer can carry a non-terminal `state` and skips the scene-epoch check.
## Implementation log
- Shared owner: `MeshSupport::ActiveOutputJobRefusal`, `ValidateQueuedJob`, `QueuedJobDelivery<Result>`
  (deliver-once; `Publish`, `Finalize`/`FinalizeFrom`, `Rejected`) declared in
  `Runtime.GeometryProcessingOperations.JobFailure.hpp`, non-template parts compiled in `...MeshSupport.cpp`.
- User-visible status/message changes (one row per migrated operation):

| Operation (label) | Duplicate submission | Cancelled/stale finalize | Rejected submission | Slice |
|---|---|---|---|---|
| Outlier estimation | "An outlier job for this output is already active." -> "Outlier estimation already has an active <state> job (job i:g)." (Pending, unchanged) | "Outlier job was cancelled or its source became stale; previous output retained." -> "Outlier estimation was cancelled or its source became stale; nothing was applied." (StaleEntity) | "Outlier job submission was rejected." -> "Outlier estimation job submission was rejected."; now also delivered once through the callback | 1 |
| Normal estimation (CPU) | same pattern, label "Normal estimation" | same pattern | same pattern | 1 |
| Density / Radii estimation | "A density/radii job ..." -> "<Density/Radii> estimation already has an active ..." | "<Noun> job was cancelled ..." -> "<Noun> estimation was cancelled ..." | "<Noun> job submission ..." -> "<Noun> estimation job submission ..." | 1 |

- Validation: Outliers, Density/Radii and Normals now answer `Cancelled` for an abandoned run instead of
  ignoring the flag. Not observable for these single-stage jobs (the flag is set only by their own
  finalizer, after which nothing revalidates); it unifies the rule for multi-stage runs. The same holds
  for setting the delivered flag in finalize: these jobs already suppressed a second delivery through
  their publisher.

## Acceptance criteria
- [ ] One compiled helper owns the active-job check (one status, `BuildActiveDerivedJobMessage` wording), deliver-once, and unpublished finalize; the ~10 hand-written copies are removed or reduced to typed callbacks.
- [ ] Every migrated operation honours `Abandoned` in validation and sets/clears the delivered flag identically; a test per drift item above fails on the old behaviour.
- [ ] A duplicate submission returns the same status and wording for every operation, and the agent lane still reports it as `result_unavailable`.
- [ ] Existing publication, Undo/Redo, stale-source and cancel behaviour are unchanged; no new per-job indirection or public surface.
- [ ] Documentation (owner routes / sandbox-editor boundaries) and module inventory name the shared owner.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'GeometryProcessing|Bilateral|Outlier|PointDensity|Keypoint|Descriptor|Construction|Normal|CoherentPointDrift|PointSampling|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
