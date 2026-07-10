---
id: RUNTIME-169
theme: F
depends_on:
  - CI-003
  - RUNTIME-163
maturity_target: Operational
---
# RUNTIME-169 — Privatize the RenderExtractionService surface

## Goal
- Remove `Extrinsic.Runtime.RenderExtractionService` as a standalone exported
  module surface and keep it as Engine-private service glue, without changing
  render extraction cache, pool, statistics, or frame-index behavior.

## Non-goals
- No changes to `Extrinsic.Runtime.RenderExtraction` public data contracts.
- No extraction behavior, renderer, ECS, or asset residency changes.
- No overlap with `RUNTIME-166`, which owns slimming the main
  `Runtime.RenderExtraction` module.

## Context
- Owner/layer: `runtime`; the service is an Engine-owned composition object.
- Local 2026-07-10 triage measured `Runtime.RenderExtractionService.cppm` at up
  to 42.444s with only Engine-side production consumers.
- `RUNTIME-163` extracted this service to reduce `Engine` size; this follow-up
  keeps the ownership split while avoiding a low-value exported module surface.

## Required changes
- [ ] Confirm `RenderExtractionService` has no intended non-Engine production
      consumers.
- [ ] Move its declaration to a private runtime header or otherwise make it
      implementation-local to `Runtime.Engine`.
- [ ] Keep `RenderExtractionCache` and `RenderWorldPool` public contracts in
      their owning modules; do not re-export them through a private service seam.
- [ ] Update source-scan tests that currently assert the service is a module.
- [ ] Remove the `.cppm` from CMake module file sets if the module is retired.
- [ ] Record before/after compile timing and import counts.

## Tests
- [ ] Run runtime engine layering, render extraction, render-world pool, and
      sandbox acceptance tests.
- [ ] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [ ] Update runtime docs/readmes if they name
      `Extrinsic.Runtime.RenderExtractionService` as a module.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] `RenderExtractionService` remains Engine-owned but no longer expands the
      public module graph.
- [ ] Extraction cache/pool/stat behavior is unchanged under focused tests.
- [ ] `RUNTIME-166` remains the only owner of main RenderExtraction API slimming.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RenderExtraction|RenderWorldPool|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing extraction behavior or public `Runtime.RenderExtraction` types.
- Reintroducing extraction state directly into `Runtime.Engine`.
- Using this task to implement `RUNTIME-166`.

## Maturity
- Target: `Operational`; this is a private-shape change to an operational Engine
  composition service, so no `Operational` follow-up is owed.
