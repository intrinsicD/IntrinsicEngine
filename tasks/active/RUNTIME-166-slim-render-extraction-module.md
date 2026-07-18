---
id: RUNTIME-166
theme: F
depends_on:
  - CI-003
  - BUILD-004
maturity_target: Operational
---
# RUNTIME-166 — Slim and partition the RenderExtraction module

## Goal
- Reduce `Extrinsic.Runtime.RenderExtraction` interface compile cost and improve
  cold-build parallelism by hiding `RenderExtractionCache` private state and
  splitting its independent implementation domains without changing extraction
  behavior or public data contracts.

## Non-goals
- No renderer/RHI/ECS ownership change and no new extraction feature.
- No semantic change to residency, deferred retirement, adapter application,
  statistics, selection, or render-world submission.
- No compatibility re-export or duplicate extraction path.
- No edit to `Runtime.Engine` decomposition owned by `RUNTIME-146..151`.

## Context
- Owner/layer: `runtime`; the module may import ECS/graphics lower layers, while
  graphics remains unaware of live ECS ownership.
- The 2026-07-09 `CI-003` audit measured
  `Runtime.RenderExtraction.cppm` at 106.935s (867 lines, 36 imports/exports),
  the third-largest module-interface compile hotspot. The implementation is
  3,474 lines.
- The interface exposes public snapshot/stat/availability contracts but also
  embeds extensive private residency sidecars, pack buffers, retire queues,
  adapter registries, and heavy graphics/ECS types in
  `RenderExtractionCache`'s layout. Those private declarations force importers
  to parse dependencies that only the implementation needs.
- A private implementation object and non-exported module partitions are
  established repository patterns. No ADR is needed unless implementation
  discovers a public ownership/API decision.
- Historical evidence in
  `tools/analysis/build_time_baseline_2026-04-05.md` shows implementation-only
  touches rebuilding in 16s while module-interface touches cascaded for
  3m23s–4m19s. Before/after claims use `CI-003` telemetry, not one local build.
- `BUILD-004` owns normalization of multi-output module compile edges and
  source-complete hotspot reporting. This task consumes that repaired evidence
  so its before/after claim is not based on duplicated `.pcm`/`.o` rows or a
  source-incomplete baseline.

## Status
- In progress; owner: Codex team; branch:
  `codex/runtime-166-slim-render-extraction`; activated 2026-07-18.
- Next gate: record the public/private import and declaration inventory before
  the first mechanical implementation-storage slice.

## Required changes
- [ ] Inventory interface declarations/imports into public contract, required
      complete public types, and private implementation-only types; record the
      inventory in this task before editing.
- [ ] Hide `RenderExtractionCache` private sidecars, scratch buffers, retire
      queues, and adapter state behind implementation storage so their headers/
      modules leave the exported interface.
- [ ] Move non-trivial control flow and all private helper declarations that do
      not need public visibility into implementation units or non-exported
      partitions.
- [ ] Split the implementation into independently compilable domains at natural
      boundaries (base extraction/submission, geometry residency/retirement,
      visualization/spatial adapters) without duplicating state ownership.
- [ ] Preserve public API and value/lifetime semantics; if PImpl changes
      special-member requirements, define them explicitly in the implementation
      and update direct construction tests.
- [ ] Re-audit every interface import and global-module-fragment include,
      retaining only declarations required by the public surface.
- [ ] Record before/after interface lines/imports, clean compile duration,
      downstream rebuild edges after an implementation-only touch, and full
      clean-gate duration against `CI-003`.

## Tests
- [ ] Existing RenderExtraction unit/contract/integration tests pass unchanged
      except for mechanical import updates.
- [ ] Add or retain lifetime tests covering construct, move if supported,
      shutdown/drain, and destruction with pending deferred-retire state.
- [ ] Run the full default CPU gate and layering check.
- [ ] Compare clean and implementation-touch compile metrics to the named
      baseline with at least five comparable samples before claiming a gain.

## Docs
- [ ] Update runtime architecture/module documentation if implementation
      partitions or the public import surface change.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Update `tasks/backlog/runtime/README.md` and regenerate
      `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] `Runtime.RenderExtraction.cppm` contains no private residency/retire/
      adapter storage declarations that can live behind implementation state.
- [ ] Interface import count and clean interface compile time decrease against
      the recorded baseline, with no increase in downstream rebuild edges for
      implementation-only edits.
- [ ] Extraction behavior, diagnostics, residency lifetimes, and public API are
      unchanged under focused and full CPU tests.
- [ ] The module remains `Operational` through the existing Engine/runtime
      composition path.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderExtraction|RenderWorldPool|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40 --json-out build/ci/compile_hotspots_report.json --baseline-json tools/analysis/compile_hotspot_baseline.json
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Moving live ECS ownership into graphics or exposing `Vk*` through runtime/RHI
  surfaces.
- Mixing behavior changes with the mechanical implementation/interface split.
- Leaving exported forwarding shims for private helper types.
- Claiming compile-time improvement without comparable baseline results.

## Maturity
- Target: `Operational`; this is a behavior-preserving decomposition of the
  active runtime extraction path, so no new capability follow-up is owed.
