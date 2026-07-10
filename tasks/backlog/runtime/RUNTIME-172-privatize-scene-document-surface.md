---
id: RUNTIME-172
theme: F
depends_on:
  - CI-003
  - RUNTIME-148
maturity_target: Operational
---
# RUNTIME-172 — Privatize the SceneDocument surface

## Goal
- Remove or sharply reduce the exported `Extrinsic.Runtime.SceneDocument`
  module surface by making scene document orchestration Engine-private while
  preserving save/load/new/close behavior and diagnostics.

## Non-goals
- No scene serialization format changes.
- No undo/redo, selection, stable lookup, render extraction, or asset pipeline
  behavior changes.
- No new scene editor UX.

## Context
- Owner/layer: `runtime`; scene document orchestration composes lower runtime
  services and remains outside lower layers.
- Local 2026-07-10 triage measured `Runtime.SceneDocument.cppm` at up to
  23.763s with low fanout and 11 imports.
- `RUNTIME-148` extracted the subsystem from `Engine`; this task keeps that
  decomposition but reconsiders whether it needs an exported module boundary.

## Required changes
- [ ] Inventory all consumers of `Runtime.SceneDocument` and classify which are
      Engine-only, test-only, or true public runtime API needs.
- [ ] Move Engine-only declarations to private runtime header/source glue or
      reduce the module to a minimal public document-event facade.
- [ ] Preserve existing scene file event, queued operation, and diagnostics
      behavior.
- [ ] Update tests to use public Engine/scene-document APIs or an explicit test
      seam rather than importing private internals.
- [ ] Record before/after module/interface metrics.

## Tests
- [ ] Run runtime scene lifecycle, scene serialization, selection stable lookup,
      sandbox editor scene-file, and engine layering tests.
- [ ] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [ ] Update runtime scene-document documentation if the module surface changes.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] Scene save/load/new/close behavior and diagnostics are unchanged.
- [ ] Engine-private orchestration does not require a broad exported module
      unless the consumer inventory justifies it.
- [ ] Tests cover behavior through stable public seams.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SceneDocument|SceneSerialization|RuntimeSceneLifecycle|SelectionStableLookup|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing scene serialization schema or persistence scope.
- Hiding behavior regressions behind source-scan test updates.
- Moving scene document ownership into `app`.

## Maturity
- Target: `Operational`; this preserves current runtime scene-document behavior,
  so no `Operational` follow-up is owed.
