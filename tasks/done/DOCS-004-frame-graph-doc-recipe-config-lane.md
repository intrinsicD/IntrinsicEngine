---
id: DOCS-004
theme: B
depends_on: []
completed_on: 2026-06-28
---
# DOCS-004 — Promote frame-graph.md from stub and document the recipe-config lane

## Goal
- [x] Turn the canonical-but-stub `docs/architecture/frame-graph.md` into a factual
  description of recipe-driven frame composition and the shipped recipe-config
  lane, so P5 ("frame composition is recipe-driven") has a canonical home agents
  are routed to.

## Non-goals
- Describing a not-yet-shipped engine-config file lane as if it exists
  (AGENTS.md §9 factual-current-state rule). `GRAPHICS-106` and `RUNTIME-130`
  are now retired, so this task records the current preview/apply lane instead:
  config can install a constrained `FrameRecipeOverride`, while the default
  `FrameRecipe*` path remains the per-frame live driver.
- Moving recipe content authority away from the canonical doc into the
  legacy-background `rendering-three-pass.md`.

## Context
- `docs/architecture/frame-graph.md` is canonical per `docs/architecture/index.md`
  but is an ~18-line stub, while the recipe content currently lives in the
  legacy-background `rendering-three-pass.md`.
- The shipped recipe-config lane (`RenderRecipeConfig` schema id/version,
  side-effect-free `PreviewRenderRecipeConfig`, the fixed-core guard, diagnostic
  states) is undocumented in canonical architecture.
- Status: retired 2026-06-28. `frame-graph.md` now documents `FrameRecipe*` as
  the live per-frame composition driver, `RenderRecipeConfig` as the external
  config overlay, the runtime/editor/agent edit lanes, boot and hot
  `render.default_recipe_config_path` activation, and the fixed-core guard that
  prevents arbitrary pass injection.
- Commit: this commit (`Promote frame graph recipe docs`).
- Owner/layer: docs/architecture. Docs-only.

## Required changes
- [x] Rewrite `frame-graph.md` to describe recipe-as-data: `FrameRecipeFeatures`
      gating, typed `FramePassId` / `FrameResourceId`, the per-frame
      `BuildDefaultFrameRecipe` path, and `DescribeDefaultFrameRecipe` introspection.
- [x] Document the three edit lanes: config via `RenderRecipeConfig`, UI via the
      Sandbox editor preview, and agents via the side-effect-free preview + the
      fixed-core mutation guard.
- [x] State current state factually: the recipe-config lane can install a
      constrained renderer override through runtime, and the default recipe is
      still rebuilt per-frame from the live `FrameRecipe*` path.

## Tests
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.
- [x] `frame-graph.md` is no longer a stub.

## Docs
- [x] This task is docs-only (`docs/architecture/frame-graph.md`, and
      cross-references in `index.md` if the classification needs a note).

## Acceptance criteria
- [x] `frame-graph.md` describes recipe-driven composition and the three edit
      lanes, with factual current-state wording.
- [x] Doc-link check passes; skill mirrors (if mirrored) regenerate clean.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --quiet -- tools/agents/skills docs/agent || echo "mirror/doc drift to review"
```

## Forbidden changes
- Describing an unshipped engine-config file lane as current state.
- Moving recipe authority into legacy-background docs.
