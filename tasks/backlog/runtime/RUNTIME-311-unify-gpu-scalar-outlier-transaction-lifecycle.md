---
id: RUNTIME-311
theme: I
depends_on: [RUNTIME-298]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up; evidence is the diff, tests, and CI
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, repo.source-documentation]
---
# RUNTIME-311 — Unify the two-phase GPU Run/Accept transaction lifecycle (one lifecycle generic over N rings)

## Goal
Replace the five hand-copied two-phase GPU Run/Accept transaction lifecycles
(scalar, outlier, vertex normals, property smoothing, GPU positions) with one
runtime-owned lifecycle over N output rings. Keep typed method calculation,
statistics, and publication adapters at their existing owners.

## Context
RUNTIME-298 review found drift in ring-generation validation and Accept callback
handling. Those defects are fixed there; this task owns the deferred unification.
The 2026-10-01 duplication audit (re-verified at `665c693dd`) found the same
helper set copied in five files, not two:

| Transaction | File |
| --- | --- |
| Point scalar (density/spacing/weights/keypoints) | `Runtime.PointScalarTransaction.cpp` |
| Outliers | `Runtime.GeometryProcessingOperations.Outliers.cpp` |
| Vertex normals | `Runtime.GeometryProcessingOperations.Normals.cpp` (`BeginAccept`, accept job, run job) |
| Property smoothing | `Runtime.MeshFieldOperations.Smoothing.cpp` (`BeginAccept`, accept job, run job) |
| GPU positions (accept-only variant) | `Runtime.GeometryProcessingOperations.GpuPositions.cpp` |

All under `src/runtime/Editor/Operations/`. The copied pieces are `Current`, `Deliver`,
`Finish(phase, status, message)`, `Poll`, `CompleteRun`, `CompleteAccept`, `BeginAccept` and the
AutoAccept tail inside `PublishCompletion`. The Normals and Smoothing AutoAccept tails were
byte-identical at the audit revision; re-confirm before editing.

Reuse `Runtime.PointScalarTransaction` and the outlier transaction's existing
capture, readback, generation and guarded-delivery mechanisms, and
`GpuFrontReadback`/`PollGpuFrontReadback` (`Runtime.GeometryProcessingOperations.GpuFront.hpp`)
as the readback primitive. Do not introduce a service, registry, additional job hop or
public template framework. GpuPositions has no Run phase; the lifecycle must express an
accept-only transaction without a dummy run.

Sequencing: land the shared lifecycle with scalar and outliers first, then migrate normals,
smoothing and GPU positions as separate reviewed slices. The Sandbox row that drives these
transactions is [UI-071](../ui/UI-071-gpu-transaction-controls-and-refusal-presentation.md);
the per-job setup/completion prologue that precedes them is
[RUNTIME-313](RUNTIME-313-queued-editor-job-setup-and-completion-helper.md).

## Acceptance criteria
- [ ] One compiled lifecycle owns acquisition, polling, ring publication, Accept, Discard, cancellation and terminal delivery for one or N rings, including the accept-only (no Run phase) shape.
- [ ] Every ring generation, input/output revision and workspace attachment is checked before publication; stale cleanup preserves replacement rings.
- [ ] Start and Accept callbacks have the same replacement and exactly-once semantics across methods, including rejected submissions.
- [ ] Scalar, outlier, normals, smoothing and GPU-positions panels retain complete typed results after terminal transitions, with statistics, backend identity and IO counters intact.
- [ ] Existing method math, atomic multi-output publication, Undo/Redo and renderer observation behavior are preserved; all five duplicated lifecycles (including the byte-identical AutoAccept tails) are removed.
- [ ] CPU contracts and real-device scalar, outlier, normals, smoothing and GPU-positions transaction smokes pass (register a `gpu;vulkan` smoke for any of the five that lacks one before migrating it); documentation and module inventory describe the shared owner.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'PointScalarTransaction|OutlierTransaction|NormalTransaction|PropertySmoothingTransaction|GpuPositionsAccept|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
cmake --build build/ci-vulkan -j$(nproc)
ctest --test-dir build/ci-vulkan -R 'PointScalarTransactionGpuSmoke|OutlierTransactionGpuSmoke|NormalTransaction|PropertySmoothingTransaction|GpuPositionsAccept' -L gpu -L vulkan --output-on-failure --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
