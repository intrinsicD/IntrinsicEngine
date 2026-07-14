---
id: RUNTIME-103
theme: F
depends_on: []
---
# RUNTIME-103 — Geometry algorithm execution queue

## Goal
- Decide whether current editor geometry-processing workflows need asynchronous execution, and promote only the minimal runtime queue/result-apply seam required beyond synchronous CPU K-Means.

## Non-goals
- No geometry algorithm implementation in runtime; algorithms stay in `geometry` or `methods`.
- No CUDA dependency enabled by default.
- No GPU/RHI handles in ECS components.
- No replacement for method workflow tasks under `methods/`.
- No asynchronous geometry algorithm queue is owed after the value gate: the promoted synchronous CPU K-Means command is the intended endpoint for current workflows.

## Context
- Owner/layer: `runtime` schedules work and applies results to ECS `GeometrySources`; `geometry`/`methods` own algorithm kernels; optional CUDA seams must be behind capability flags.
- `UI-004` promoted synchronous CPU K-Means over mesh vertices, graph nodes, and point-cloud points. `GRAPHICS-086` retires legacy CUDA from the promoted default path because no current runtime/method/graphics consumer requires a compute backend; this task value-gated asynchronous scheduling, centroid entities, topology mutation, and broader algorithm execution.
- Reuse `Runtime.StreamingExecutor`, `Geometry.KMeans`, `Geometry.DomainViews`, `ECS.Component.DirtyTags`, and SandboxEditor processing discovery models.

## Value gate
- Current state: synchronous CPU K-Means is promoted and deterministic for mesh, graph, and point-cloud domains.
- Evidence: `ApplySandboxEditorKMeansCommand(...)` validates the selected entity/domain, collects finite CPU positions, calls `Geometry.KMeans` with the CPU backend, publishes label/color properties, stamps `DirtyVertexAttributes`, and marks editor history dirty in one main-thread command. Existing `SandboxEditorUi` contract tests cover the supported domains, deterministic publication, dirty-domain stamping, and fail-closed diagnostics.
- Scope decision: no asynchronous runtime geometry algorithm queue is retained for current workflows. A queue would add cancellation/stale-target/apply-order surface without a current multi-algorithm consumer or measured editor stall case. Centroid entities, topology mutation, broader algorithms, and optional compute backends require future value-gated method/runtime tasks with concrete workloads. CUDA is not inherited from legacy `Runtime.PointCloudKMeans`.

## Required changes
- [x] Recorded that no request/result queue taxonomy is added because the value gate does not justify asynchronous geometry execution for current workflows.
- [x] Kept CPU K-Means on the existing synchronous runtime editor command and documented that endpoint.
- [x] Recorded that label/color publication is already promoted; centroid entities and topology mutation are future value-gated work only when a concrete consumer exists.
- [x] Applied the `GRAPHICS-086` CUDA removal decision: do not retain legacy CUDA K-Means in the promoted default path; reopen optional compute only through a future method/backend task with a concrete workload.
- [x] Confirmed the current command applies results on the main thread and stamps `DirtyVertexAttributes` for mesh, graph, and point-cloud domains.

## Tests
- [x] No queued execution/cancellation/apply-order tests were added because no queue API is retained.
- [x] Existing `SandboxEditorUi` contract tests cover CPU K-Means deterministic command behavior, supported domains, fail-closed diagnostics, property publication, and dirty-domain stamping.
- [x] No CUDA tests were added; future compute work requires a new opt-in method/backend task.

## Docs
- [x] Updated `src/runtime/README.md`, `tasks/backlog/ui/README.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [x] No method/geometry docs changed because algorithm ownership and public method contracts did not change.
- [x] No module inventory regeneration is required because no public module surfaces changed.

## Acceptance criteria
- [x] The value gate records synchronous execution as the intended endpoint with the deciding evidence.
- [x] CPU K-Means keeps deterministic outputs and no longer requires legacy `Runtime.PointCloudKMeans`.
- [x] CUDA follows the `GRAPHICS-086` removal decision; any future CUDA behavior requires a new named task and opt-in verification plan.

## Status
- Completed 2026-06-15 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Decision: no runtime algorithm execution queue or `Operational` async follow-up is owed for current workflows. The existing synchronous CPU K-Means command remains the promoted runtime/editor endpoint; future asynchronous scheduling, centroid entities, topology mutation, broader algorithms, or compute backends require new value-gated tasks with concrete consumers.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'KMeans|Streaming|SandboxEditorUi|Algorithm' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Verification results
```bash
cmake --build --preset ci --target IntrinsicTests
bash -lc "set -o pipefail; ctest --test-dir build/ci --output-on-failure -R 'KMeans|Streaming|SandboxEditorUi|Algorithm' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 | tee /tmp/runtime-103-focused-ctest.log"
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Result: `IntrinsicTests` built; focused CTest passed 48/48; task policy,
task-state links, session-brief freshness, doc links, docs-sync diff, layering,
and test-layout checks passed.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Running ECS mutations on worker threads.
- Making CUDA a default requirement.

## Maturity
- Target reached: `CPUContracted` for the promoted synchronous CPU K-Means command and scheduling decision; no asynchronous `Operational` follow-up is owed, and no CUDA `Operational` proof is owed after the `GRAPHICS-086` removal decision.

## Slice plan
- **Slice A — decision gate.** Completed by recording that synchronous CPU K-Means suffices for current editor workflows; no request/result queue taxonomy is added.
- **Slice B — retired by value gate.** Queue routing and apply semantics are not implemented because no async queue API is retained.
- **Slice C — retired.** CUDA backend integration is not owned here; `GRAPHICS-086` removes legacy CUDA from the promoted default path, so future opt-in compute work must open a new method/backend task with a concrete workload.
