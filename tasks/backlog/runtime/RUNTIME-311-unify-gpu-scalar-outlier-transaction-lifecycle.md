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
# RUNTIME-311 — Unify the GPU scalar/outlier transaction lifecycle (one lifecycle generic over N rings)

## Goal
Replace the duplicated scalar and outlier GPU transaction lifecycle with one
runtime-owned lifecycle over N output rings. Keep typed method calculation,
statistics, and publication adapters at their existing owners.

## Context
RUNTIME-298 review found drift in ring-generation validation and Accept callback
handling. Those defects are fixed there; this task owns the deferred unification.
Reuse `Runtime.PointScalarTransaction` and the outlier transaction's existing
capture, readback, generation and guarded-delivery mechanisms. Do not introduce
a service, registry, additional job hop or public template framework.

## Acceptance criteria
- [ ] One compiled lifecycle owns acquisition, polling, ring publication, Accept, Discard, cancellation and terminal delivery for one or N rings.
- [ ] Every ring generation, input/output revision and workspace attachment is checked before publication; stale cleanup preserves replacement rings.
- [ ] Start and Accept callbacks have the same replacement and exactly-once semantics across methods, including rejected submissions.
- [ ] Scalar and outlier panels retain complete typed results after terminal transitions, with statistics, backend identity and IO counters intact.
- [ ] Existing method math, atomic multi-output publication, Undo/Redo and renderer observation behavior are preserved; both duplicated lifecycles are removed.
- [ ] CPU contracts and real-device scalar/outlier transaction smokes pass; documentation and module inventory describe the shared owner.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'PointScalarTransaction|OutlierTransaction|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
cmake --build build/ci-vulkan -j$(nproc)
ctest --test-dir build/ci-vulkan -R 'PointScalarTransactionGpuSmoke|OutlierTransactionGpuSmoke' -L gpu -L vulkan --output-on-failure --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
