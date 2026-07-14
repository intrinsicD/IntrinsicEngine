---
id: GRAPHICS-090
theme: F
depends_on: [RUNTIME-113, RUNTIME-114]
maturity_target: Operational
---
# GRAPHICS-090 — Progressive render-data operational smoke

## Completion
- Completed: 2026-06-16. Commit/PR: this retirement commit.
- Maturity: `Operational`.
- Fix summary: extended the promoted runtime sandbox GPU smoke with a
  progressive render-data scene that observes pending/default mesh slots,
  then a ready generated mesh texture slot, plus ready graph edge
  property-buffer presentation, previous-output retention, unsupported/failure
  diagnostics, and material texture binding resolution counters.
- Evidence: `ci-vulkan` built the runtime sandbox GPU smoke target and the
  opt-in `gpu;vulkan` test
  `ProgressiveRenderDataReachesOperationalFrame` passed on this
  Vulkan-capable host.
- Boundary: graphics consumes runtime snapshots and material/asset ids only;
  it does not import live ECS/runtime ownership, `AssetService`, geometry
  algorithms, xatlas, or texture baking code.

## Goal
- Prove on a Vulkan-capable host that progressive render-data outputs are
  consumed by the promoted renderer for generated mesh texture slots and graph
  or point-cloud property-buffer presentation.

## Non-goals
- No descriptor, scheduler, importer, or UI implementation.
- No graphics-side property discovery or geometry algorithm execution.
- No mesh UV generation or texture baking in graphics.
- No displacement rendering proof.
- No broad PBR feature expansion beyond the slots made ready by runtime.

## Context
- Owning subsystem/layer: opt-in graphics/runtime Vulkan smoke. Graphics
  consumes snapshots, asset IDs, bindless indices, material records, and
  graphics-owned buffers; runtime owns live ECS, import, derived jobs, and
  extraction.
- `RUNTIME-113` supplies progressive presentation snapshots. `RUNTIME-114`
  supplies import enrichment and generated output binding. ADR-0021 defines
  the accepted cross-domain contract.
- `GRAPHICS-089` is a narrower generated-UV texture sampling proof. This task
  proves the broader progressive render-data contract after the runtime CPU
  contracts exist.

## Required changes
- [x] Add or update an opt-in `gpu;vulkan` smoke scene that starts from raw
      imported/renderable geometry and observes a later generated mesh texture
      slot becoming active.
- [x] Exercise at least one graph edge or vertex property-buffer presentation
      path, or one point-cloud point-domain property-buffer presentation path.
- [x] Assert renderer diagnostics distinguish default/pending slot state,
      ready generated texture state, property-buffer readiness, and failure or
      unsupported states.
- [x] Keep the smoke skipped or fail-closed with explicit diagnostics when the
      host lacks promoted Vulkan support.
- [x] Preserve layering: graphics consumes only snapshots/resources and never
      imports live ECS, runtime, `AssetService`, geometry backends, or xatlas.

## Tests
- [x] Add `gpu;vulkan` smoke coverage for progressive generated mesh texture
      slot activation.
- [x] Add `gpu;vulkan` smoke coverage for graph or point-cloud property-buffer
      presentation.
- [x] Preserve the default CPU-supported CTest gate.
- [x] Preserve relevant runtime descriptor, extraction, import, and graphics
      material/property-buffer contract tests.

## Docs
- [x] Update `tasks/backlog/rendering/README.md` or this task with any final
      Vulkan host prerequisites discovered during implementation.
- [x] Update graphics/runtime architecture docs only if operational smoke
      requires an ownership or snapshot-contract change.

## Acceptance criteria
- [x] `ci-vulkan` builds `IntrinsicTests` on the target host.
- [x] Opt-in `gpu;vulkan` CTest evidence shows a generated mesh texture slot
      replacing default/pending state after runtime makes it ready.
- [x] Opt-in `gpu;vulkan` CTest evidence shows graph or point-cloud
      property-buffer presentation consumed by the renderer.
- [x] Missing Vulkan capability reports a deterministic skip or fail-closed
      diagnostic instead of being recorded as passing operational proof.
- [x] Graphics remains free of live ECS, runtime, `AssetService`, geometry
      backend, and xatlas dependencies.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Progressive|RenderData|PropertyBuffer|Texture' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement UV atlas generation, texture baking, import scheduling, or
  UI commands in graphics.
- Do not import runtime, ECS, `AssetService`, geometry algorithms, or xatlas
  into graphics or Vulkan layers.
- Do not mark CPU/null evidence as operational Vulkan proof.
- Do not silently pass when Vulkan capability is unavailable.
- Do not implement displacement rendering in this task.

## Maturity
- Target: `Operational`.
- This task closes the backend smoke deferred by `RUNTIME-113` and
  `RUNTIME-114`; no additional `Operational` follow-up is owed if the smoke
  passes on a Vulkan-capable host.
