---
id: GRAPHICS-138
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive measured refactor; reviewed diff and CPU/backend tests appropriate to the selected change.
contract_schema: 1
contracts: [repo.source-documentation, runtime.render-diagnostics-locality]
---
# GRAPHICS-138 — Reduce renderer interface compilation cost

## Goal
- Narrow the remaining expensive renderer interface/implementation dependency
  surface while preserving the same rendering capabilities and ownership.

## Context and decision boundaries
- BUILD-007's 2026-09-15 report identifies `Graphics.Renderer.cppm` and
  `Graphics.Renderer.cpp` as expensive producers. Reproduce the current cost
  before selecting a change; parallel-build durations alone do not prescribe Pimpl.
- Inventory actual public types, private state, includes/imports and consumers.
  Compare unused-import removal and narrowing existing owners before adding
  private implementation storage or another file. A split must remove dependency
  work, not duplicate parsing or introduce forwarding-only wrappers.
- Preserve RHI/backend separation, recipe composition, device capability gates,
  resource lifetime and diagnostics locality. This is not GRAPHICS-135's runtime
  scheduling experiment, GRAPHICS-105's material-authority refactor, or the
  trigger-gated GRAPHICS-137/136 backend/rename work.

## Acceptance criteria
- [ ] Freeze the current compiler graph/probe baseline and select one bounded
      interface/private-state change with actual consumer justification.
- [ ] Preserve rendering behavior and public diagnostics; no compatibility
      wrappers, new backend, or new frame/lifetime policy.
- [ ] Keep one implementation per mechanism and document file/line changes,
      including any new private header or implementation unit.
- [ ] Compiler-boundary checks, affected CPU tests and any GPU/lifetime tests
      required by the chosen change pass; generated inventory/docs are current.
- [ ] Reuse BUILD-007 tooling for matched timing evidence before a performance
      claim; reject the change if complexity or compilation cost increases.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'Render|Graphics|CompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
