---
id: GRAPHICS-145
theme: B
depends_on: [GRAPHICS-144]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive follow-up; scoped source review, exact compiler traces and matched benchmark records own the evidence.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-145 — Reduce renderer implementation frontend work

## Goal
Reduce repeated C++ frontend/template work in the renderer implementation without
changing rendering behavior or adding another interface or lifetime owner. This is
an operator-directed compile/reuse follow-up outside the standing product focus.

## Starting evidence and scope
- GRAPHICS-144 already shares standard declarations and narrows GLM at the interface.
  Preserve that work; do not repeat its whole-target improvement as this task's gain.
- Its [report and retained implementation trace](../../../ara/evidence/tables/graphics144_shared_std_measurement.md)
  bind source `5537a4dda`. Implementation-edit medians remain around 12.9 seconds.
  A separate trace records 13.554 seconds Frontend versus 0.906 Backend; parsing
  NullRenderer occupies 6.374 seconds and constraint checking totals 5.495 seconds.
  These phases overlap. They identify investigation targets, not an isolated cause
  or proof that a particular algorithm/header substitution helps.
- Inspect actual template/constraint instantiations, inline class bodies and existing
  helper owners. Compare simpler equivalent standard-algorithm calls and bounded
  existing implementation ownership before adding files, Pimpl or forwarding.
  Preserve comparator/projection semantics, deterministic ordering, resource lifetime,
  recipes, diagnostics, failures and backend behavior. No broad algorithm rewrite.
- RUNTIME-267, UI-037, GRAPHICS-105 and BUILD-006 retain their independent scope.

## Acceptance criteria
- [ ] Freeze current immediate-before source; reproduce the implementation trace and
      rank concrete costly expressions/functions without summing nested phase totals.
- [ ] Review a small alternative with Claude and reuse existing owners. Reject a
      source split or abstraction that merely repeats parsing or moves the same work.
- [ ] Implement a beneficial bounded change or retain a measured no-change verdict;
      verify affected semantics and preserve current renderer/compiler-boundary tests.
- [ ] Freeze matched implementation and genuine importer/target measurements before
      timing; retain all samples, compiler flags/dependency identities and negative
      results. Do not extrapolate isolated producer costs to whole-engine builds.
- [ ] Pass relevant focused/full CPU checks; use fresh minimum-supported Clang if
      module attachment changes. Resolve review, synchronize docs and retire with
      exact source/measurement evidence. GPU behavior changes require their own gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'Renderer|RenderWorld|Visualization|CompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
