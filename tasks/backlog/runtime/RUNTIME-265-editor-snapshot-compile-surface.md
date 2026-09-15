---
id: RUNTIME-265
theme: J
depends_on: [RUNTIME-264]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive compile-locality follow-up; reviewed diff, dependency checks and tests.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality, runtime.processing-compilation-locality]
---
# RUNTIME-265 — Narrow the editor snapshot and config dependency chain

## Goal
- Reduce the remaining scene-editing → workspace snapshot → context/session
  dependency cost without duplicating presentation records or changing behavior.

## Context and decision boundaries
- The 2026-09-15 BUILD-007 report identifies the workspace snapshot interface
  and context adapter as remaining expensive producers. Its config edit chain
  also passes through consolidation types and the editor session. RUNTIME-264
  removes lifecycle imports from service callers; establish a fresh baseline
  after that change rather than counting its benefit again.
- Inspect which consumers need complete records versus pointer/reference borrows.
  Reuse current family-owned prepared frames and their matching C++ linkage.
  Keep one definition per record, existing visitor lifetimes and epoch guards.
- Compare removal of unused imports, narrower existing record owners, and private
  implementation storage only where actual consumers justify it. No mandatory
  Pimpl, new generic facade, family registry, or compatibility wrapper.

## Acceptance criteria
- [ ] Identify the dominating dependency edges using the actual configured
      compiler graph and current source; choose a bounded consumer group.
- [ ] Remove unnecessary dependencies without copying records or transferring
      validation/config/publication authority to the app.
- [ ] Preserve config-file/UI/agent behavior, snapshot caching, stale/detach
      guards and command-time validation; relevant editor tests pass.
- [ ] Add/update compiler-boundary checks and architecture/owner documentation.
- [ ] Use the existing BUILD-007 measurement tooling for matched before/after
      evidence before claiming a timing gain; retire with an explicit negative
      result if no beneficial refactor survives the measured comparison.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'EditorCompilationLocality|SandboxEditor|Consolidation|RuntimeConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
