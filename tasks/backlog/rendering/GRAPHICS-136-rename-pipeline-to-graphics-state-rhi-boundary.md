---
id: GRAPHICS-136
theme: B
depends_on: [GRAPHICS-137]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.source-documentation
---
# GRAPHICS-136 — Rename `Pipeline*` to `GraphicsState*` at the RHI boundary

## Goal

- Mechanically rename the RHI-surface pipeline vocabulary
  (`PipelineDesc`, `PipelineHandle`, `PipelineTag`, `PipelineManager`,
  `PipelineLease`, `PipelineCompiledCallback`, and the `IDevice`
  create/validate methods) to `GraphicsState*`, so the public boundary stops
  asserting that a description maps to one native pipeline object
  (ADR-0028 Decision 1).

**Gate — do not start before the trigger.** This task depends on
`GRAPHICS-137` (the ADR-0028 killing-experiment spike): it executes only
after a second realization strategy has actually landed and the spike's
boundary finding is recorded below. Until then the rename is churn without a
truth change.

## Non-goals

- No `GraphicsStateResolver` layer, no backend key normalization, no
  desc-keyed runtime cache, no fallback-while-compiling machinery (each has
  its own ADR-0028 trigger).
- No semantic or behavioral change of any kind; this is a rename-only slice.
- No renaming of `graphics/vulkan` backend-internal Vulkan pipeline
  vocabulary (`VkPipeline` handling stays "pipeline" — that is the point of
  the boundary).
- No shader, recipe, or pass-order changes.

## Context

- Owning layer: `graphics/rhi`; consumers across `graphics/renderer` (passes,
  HZB, light clusters, compute parallel primitives), `graphics/framegraph`,
  and `runtime` GPU modules (clustering, progressive Poisson, texture bake,
  point-cloud consolidation).
- ADR-0028 records the invariant, the trigger, and the killing experiment
  this task is gated on.
- Recorded naming default: `GraphicsState*` covers the unified description,
  including compute dispatch state (`ComputeShaderPath` stays a field of
  `GraphicsStateDesc`); revisit the compute naming only if the trigger
  context demands it, and record the revision here before executing.
- Module renames (`RHI.PipelineManager` → `RHI.GraphicsStateManager`) change
  `.cppm` interfaces and BMIs: run the rename on a fresh configure and treat
  any post-rename SEGV/vtable anomaly as a stale-build suspect first
  (`intrinsicengine-stale-build-triage`).
- Killing-experiment outcome (fill before starting): _pending — record the
  `GRAPHICS-137` spike result (boundary holds/leaks + evidence) here._

## Required changes

- [ ] Record the fired trigger and the killing-experiment outcome in
      `## Context` (prerequisite; the task stays blocked without it).
- [ ] Rename exported types in `src/graphics/rhi/`: `PipelineDesc` →
      `GraphicsStateDesc`, `PipelineHandle`/`PipelineTag` →
      `GraphicsStateHandle`/`GraphicsStateTag`, `PipelineManager` →
      `GraphicsStateManager` (module + files `RHI.PipelineManager.*` →
      `RHI.GraphicsStateManager.*`, CMake `FILE_SET` updated), lease and
      callback types accordingly.
- [ ] Rename the `IDevice` surface methods that take or validate the desc
      (e.g. `CreatePipeline`/`ValidatePipelineDesc` →
      `CreateGraphicsState`/`ValidateGraphicsStateDesc`) and update the Null
      and Vulkan overrides.
- [ ] Update all consumer call sites (renderer passes, HZB, light clusters,
      compute parallel primitives, framegraph, runtime GPU modules) — rename
      only, no logic edits.
- [ ] Update `.cppm`/header synopses and comments that say "pipeline" where
      they describe the RHI surface (`repo.source-documentation`).
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Tests

- [ ] Full default CPU gate green after the rename (rename-only: no new
      tests owed, existing contract tests must pass unchanged).
- [ ] `grep -rn "PipelineDesc\|PipelineHandle\|PipelineManager" src/
      --include='*.cppm'` reports no hits outside `src/graphics/vulkan/`
      internals.
- [ ] On a Vulkan-capable host: opt-in `gpu;vulkan` smoke subset green
      (`ci-vulkan` preset); otherwise record the deferral in the retirement
      note.

## Docs

- [ ] `docs/architecture/graphics.md` and touched READMEs updated where they
      name the renamed types.
- [ ] ADR-0028 cross-reference intact; no new ADR owed (decision already
      recorded).
- [ ] `docs/api/generated/module_inventory.md` regenerated.

## Acceptance criteria

- [ ] ADR-0028 trigger + killing-experiment outcome recorded in `## Context`
      before any code change.
- [ ] RHI surface exposes only `GraphicsState*` vocabulary; "pipeline"
      survives only inside `src/graphics/vulkan/` internals.
- [ ] Behavior unchanged: default CPU gate green, layering gate green,
      module inventory regenerated, doc links clean.
- [ ] Single mechanical commit series; no semantic edits mixed in.

## Verification

```bash
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
grep -rn "PipelineDesc\|PipelineHandle\|PipelineManager" src/ --include='*.cppm' || true  # expect: graphics/vulkan internals only
```

On a Vulkan-capable host additionally:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Starting this task before the ADR-0028 trigger has fired and its
  killing-experiment outcome is recorded in `## Context`.
- Mixing any semantic, behavioral, or optimization change into the rename.
- Introducing resolver/cache/fallback machinery (separate ADR-0028 triggers).
- Renaming `graphics/vulkan` internal Vulkan pipeline vocabulary beyond what
  the RHI override signatures require.
- Weakening or relabeling any test to get the rename green.
