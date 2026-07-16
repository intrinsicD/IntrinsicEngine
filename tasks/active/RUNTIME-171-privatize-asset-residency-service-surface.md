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

- Implementation and verification completed on 2026-07-16; owner: Codex;
  branch: `codex/arch-006-completion`.
- Next gate: commit the verified technical slice, then retire this task at
  `Operational` with the implementation commit recorded.

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
- Pre-change consumer inventory: only `Runtime.Engine.cppm` and
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
- [x] Inventory current consumers and decide whether all production uses can be
      narrowed to Engine-private source/header glue.
- [x] Move service declarations to private runtime headers or reduce the module
      to a minimal facade if external runtime code still needs it.
- [x] Avoid re-exporting `GpuAssetCache`, model texture handoff, model scene
      handoff, or object-space normal queue modules through the service seam.
- [x] Update source-scan tests and CMake file sets.
- [x] Record before/after compile timing, interface lines, and import count.

## Tests
- [x] Run asset residency, model scene/texture handoff, runtime engine layering,
      and sandbox acceptance coverage.
- [x] Run strict layering and default CPU-supported CTest.

## Docs
- [x] Update runtime/assets architecture docs if the service stops being a
      named module.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] Runtime still owns asset-to-GPU residency and teardown ordering.
- [x] `AssetResidencyService` no longer broadens the public module graph unless
      an explicit consumer inventory justifies it.
- [x] No lower layer gains live runtime or asset-service ownership.

## Evidence

- Consumer inventory: the two pre-change named-module importers were
  `Runtime.Engine.cppm` and `Runtime.Engine.cpp`; no app or test imported or
  instantiated the service. The directive-free private declaration now has
  exactly one include owner, `Runtime.Engine.cppm`.
- Surface metrics: runtime modules `81 -> 80`, repository modules `388 -> 387`,
  named-module importers `2 -> 0`, and the combined named interface surface
  fell from 557 lines / 56 imports (Engine plus service) to 491 lines / 51
  imports (Engine). The retired 71-line exported service interface became a
  59-line private header with no imports; across the affected Engine/service
  units, imports fell `159 -> 153` and export-imports fell `6 -> 2`.
- Compile diagnostics: the task baseline records the standalone BMI at up to
  23.732 seconds; this fresh build recorded the removed BMI edge at 27.362
  seconds, the post-change service implementation edge at 6.559 seconds, and
  the post-change Engine BMI at 72.681 seconds. These single-host timings are
  structural diagnostics only; there is no matched aggregate build-speed
  claim.
- Declaration and implementation review: after removing module directives and
  exports, the service declaration is text-equivalent; after reattaching the
  implementation to `Extrinsic.Runtime.Engine`, all 165 implementation lines
  and direct implementation imports are unchanged. The by-value member
  position and cache/listener/handoff lifetimes are unchanged.
- Focused residency/model/cache/private-glue/acceptance coverage passed
  `79/79`; direct `RuntimeEngineLayering.*` coverage passed `21/21`; a full
  `IntrinsicTests` build completed, and the final default CPU-supported gate
  passed `3784/3784` in 398.08 seconds.
- Strict layering, allowlist quality, task policy/maturity, doc links, test
  layout, root hygiene, PR contract, skill-mirror sync, module-inventory
  freshness, diff checks, and the clean-workshop automated bundle passed.
  Manual clean-workshop rows found no public API leak, renderer/pass/recipe
  growth, scaffold debt, or temporary exception. Three independent reviews
  found no remaining architecture, lifetime, scope, test, docs, or metrics
  blocker.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicAssetUnitTests IntrinsicGraphicsAssetsUnitTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
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
