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

## Context
- Owner/layer: `runtime` schedules work and applies results to ECS `GeometrySources`; `geometry`/`methods` own algorithm kernels; optional CUDA seams must be behind capability flags.
- `UI-004` promoted synchronous CPU K-Means over mesh vertices, graph nodes, and point-cloud points. The parity matrix and UI docs still defer CUDA/asynchronous scheduling, centroid entities, topology mutation, and broader algorithm execution.
- Reuse `Runtime.StreamingExecutor`, `Geometry.KMeans`, `Geometry.DomainViews`, `ECS.Component.DirtyTags`, and SandboxEditor processing discovery models.

## Value gate
- Current state: synchronous CPU K-Means is promoted and deterministic for mesh, graph, and point-cloud domains.
- Improvement: async execution is useful only if current editor operations can stall the frame or multiple promoted algorithms need the same scheduling/apply seam.
- Scope decision: retain a minimal queue only when current workflows justify it. Defer centroid entities, topology mutation, and CUDA unless a method/backend task supplies a concrete consumer.

## Required changes
- [ ] Define a runtime algorithm request/result taxonomy with stable target identity, source domain, cancellation, progress, completion, and fail-closed diagnostics.
- [ ] Move CPU K-Means execution onto the shared runtime queue only if the value gate proves async is needed; otherwise document synchronous execution as the intended endpoint.
- [ ] Add publication policies for labels/colors, optional centroid entities, and optional topology mutation tasks.
- [ ] Decide whether legacy CUDA K-Means is retained through a promoted optional compute backend, deferred to `GRAPHICS-086`, or explicitly retired.
- [ ] Ensure result apply runs on the main thread and stamps the correct geometry dirty domains.

## Tests
- [ ] Add `contract;runtime` tests for queued execution, cancellation/stale-target rejection, deterministic apply ordering, and dirty-domain stamping.
- [ ] Add tests for CPU K-Means parity with the synchronous path.
- [ ] Add optional CUDA tests only under `INTRINSIC_ENABLE_CUDA=ON` and skip gracefully when no driver is present.

## Docs
- [ ] Update `src/runtime/README.md`, `tasks/backlog/ui/README.md`, and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update method/geometry docs only if algorithm ownership or public method contracts change.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Runtime has one reusable algorithm execution queue for editor-driven geometry operations.
- [ ] CPU K-Means keeps deterministic outputs and no longer requires legacy `Runtime.PointCloudKMeans`.
- [ ] CUDA behavior is explicitly retained, deferred, or retired with a named task.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'KMeans|Streaming|SandboxEditorUi|Algorithm' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Running ECS mutations on worker threads.
- Making CUDA a default requirement.

## Maturity
- Target: `CPUContracted` for asynchronous CPU execution and apply semantics; optional CUDA `Operational` proof is separate and opt-in.
