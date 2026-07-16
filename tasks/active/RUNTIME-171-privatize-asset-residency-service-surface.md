---
id: RUNTIME-171
theme: F
depends_on:
  - CI-003
  - RUNTIME-164
maturity_target: Operational
---
# RUNTIME-171 — Privatize the AssetResidencyService surface

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- Next gate: replace the standalone module with an Engine-private declaration
  attached to `Extrinsic.Runtime.Engine`, then build the focused asset
  residency, handoff, and runtime contract targets.

## Goal
- Keep `AssetResidencyService` as an Engine-owned implementation service while
  removing its low-fanout exported module surface and preserving GPU asset cache
  and model handoff behavior.

## Non-goals
- No asset service, GPU asset cache, material binding, or model handoff behavior
  changes.
- No live asset-service traffic into `graphics/assets`.
- No public `Engine` facade changes.

## Context
- Owner/layer: `runtime`; runtime owns cross-layer asset-to-graphics wiring.
- Local 2026-07-10 triage measured `Runtime.AssetResidencyService.cppm` at up
  to 23.732s, with nine imports.
- `RUNTIME-164` extracted this service from `Engine`; this task preserves that
  ownership split but removes unnecessary public module exposure if feasible.
- Current consumer inventory: only `Runtime.Engine.cppm` and
  `Runtime.Engine.cpp` import the named module. The apparent third production
  consumer, `Runtime.Engine.FrameLoop.Internal.hpp`, is include-only Engine
  implementation glue that borrows the service type; no app or test imports or
  instantiates the service directly.
- Right-sized shape: keep the service as a by-value Engine member in the same
  position, keep its separate implementation unit and direct implementation
  imports, and attach its declaration/implementation to `Runtime.Engine`.
  Import the owning asset, graphics, handoff, and queue contracts directly
  without re-exporting them. Do not add a pimpl allocation, replacement
  partition, or compatibility module.
- Reintroduce a standalone service module only when a tracked non-Engine
  production consumer lands.

## Required changes
- [ ] Inventory current consumers and decide whether all production uses can be
      narrowed to Engine-private source/header glue.
- [ ] Move service declarations to private runtime headers or reduce the module
      to a minimal facade if external runtime code still needs it.
- [ ] Avoid re-exporting `GpuAssetCache`, model texture handoff, model scene
      handoff, or object-space normal queue modules through the service seam.
- [ ] Update source-scan tests and CMake file sets.
- [ ] Record before/after compile timing, interface lines, and import count.

## Tests
- [ ] Run asset residency, model scene/texture handoff, runtime engine layering,
      and sandbox acceptance coverage.
- [ ] Run strict layering and default CPU-supported CTest.

## Docs
- [ ] Update runtime/assets architecture docs if the service stops being a
      named module.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] Runtime still owns asset-to-GPU residency and teardown ordering.
- [ ] `AssetResidencyService` no longer broadens the public module graph unless
      an explicit consumer inventory justifies it.
- [ ] No lower layer gains live runtime or asset-service ownership.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsAssetsUnitTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'AssetResidency|AssetModel|GpuAssetCache|RuntimeEnginePrivateGlue|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='RuntimeEngineLayering.*'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Moving asset residency into graphics or app.
- Changing material binding, generated texture readiness, or teardown behavior.
- Re-exporting lower-layer modules for convenience.

## Maturity
- Target: `Operational`; this preserves an already operational runtime
  composition service, so no `Operational` follow-up is owed.
