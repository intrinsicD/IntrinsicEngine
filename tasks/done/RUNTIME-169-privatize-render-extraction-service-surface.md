---
id: RUNTIME-169
theme: F
depends_on:
  - CI-003
  - RUNTIME-163
maturity_target: Operational
---
# RUNTIME-169 — Privatize the RenderExtractionService surface

## Status

- Completed on 2026-07-16 at `Operational`.
- Implementation commit: `7c9ad87b`.
- Verification: focused CPU coverage passed `74/74`, runtime layering passed
  `22/22`, and the final default CPU-supported gate passed `3783/3783` after a
  successful `IntrinsicTests` build. Strict structural/review gates passed.

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
- Current consumer inventory: only `Runtime.Engine.cppm` and
  `Runtime.Engine.cpp` import the service in production; no app or test imports
  or instantiates it directly.
- Right-sized shape: keep the value member and separate implementation unit,
  attach both to `Runtime.Engine`, and import the public `RenderExtraction` and
  `RenderWorldPool` contracts directly without re-exporting them. Do not add a
  pimpl allocation, replacement partition, or compatibility module.
- Reintroduce a standalone service module only when a tracked non-Engine
  production consumer lands.

## Required changes
- [x] Confirm `RenderExtractionService` has no intended non-Engine production
      consumers.
- [x] Move its declaration to a private runtime header or otherwise make it
      implementation-local to `Runtime.Engine`.
- [x] Keep `RenderExtractionCache` and `RenderWorldPool` public contracts in
      their owning modules; do not re-export them through a private service seam.
- [x] Update source-scan tests that currently assert the service is a module.
- [x] Remove the `.cppm` from CMake module file sets if the module is retired.
- [x] Record before/after compile timing and import counts.

## Tests
- [x] Run runtime engine layering, render extraction, render-world pool, and
      sandbox acceptance tests.
- [x] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [x] Update runtime docs/readmes if they name
      `Extrinsic.Runtime.RenderExtractionService` as a module.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] `RenderExtractionService` remains Engine-owned but no longer expands the
      public module graph.
- [x] Extraction cache/pool/stat behavior is unchanged under focused tests.
- [x] `RUNTIME-166` remains the only owner of main RenderExtraction API slimming.

## Evidence

- Consumer inventory: the only two named-module production imports were
  `Runtime.Engine.cppm` and `Runtime.Engine.cpp`; no app or test imported or
  instantiated the service. The private declaration now has exactly one include
  owner, `Runtime.Engine.cppm`.
- Surface metrics: runtime modules `82 -> 81`, repository modules `389 -> 388`,
  runtime public `.cppm` entries `82 -> 81`, and named-module importers `2 -> 0`.
  The 88-line exported interface became an 80-line directive-free private
  header. Service-related import directives fell `6 -> 2`; explicit imports
  across the affected Engine/service units fell `142 -> 138` (Engine interface
  `46 -> 47`, Engine implementation `92 -> 91`, retired service interface
  `4 -> 0`).
- Compile diagnostics: the removed standalone interface had a 47.133s compiler
  edge in the local ninja log. The post-change service implementation edge was
  4.064s, and the Engine interface edge was 66.966s versus a prior 68.753s
  entry. These single-host observations are structural diagnostics only; no
  overall build-speed claim is made.
- Declaration and implementation review: the declaration is byte-identical
  after removing module directives/exports, and the implementation body is
  byte-identical after reattaching it to `Extrinsic.Runtime.Engine`. The
  by-value member position, borrowed cache address, pool ownership, and cache/
  renderer teardown order are unchanged.
- Focused extraction/pool/acceptance coverage passed `74/74`; the directly
  invoked `RuntimeEngineLayering.*` suite passed `22/22`; the final default
  CPU-supported gate passed `3783/3783` in 395.85 seconds after a successful
  `IntrinsicTests` build.
- Strict layering, test layout, task policy, doc links, root hygiene, PR
  contract, skill-mirror sync, diff checks, and the clean-workshop validator
  bundle passed. Three independent reviews found no remaining architecture,
  lifetime, scope, test, docs, or metrics blocker.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEnginePrivateGlue|RenderExtraction|RenderWorldPool|RuntimeSandboxAcceptance|ImGuiAdapterEngineWiring.FramePacingDiagnosticsPopulateOnNullBackend' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='RuntimeEngineLayering.*'
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
