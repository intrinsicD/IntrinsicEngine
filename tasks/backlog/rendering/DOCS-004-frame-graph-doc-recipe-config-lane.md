---
id: DOCS-004
theme: B
depends_on: []
---
# DOCS-004 — Promote frame-graph.md from stub and document the recipe-config lane

## Goal
- Turn the canonical-but-stub `docs/architecture/frame-graph.md` into a factual
  description of recipe-driven frame composition and the shipped recipe-config
  lane, so P5 ("frame composition is recipe-driven") has a canonical home agents
  are routed to.

## Non-goals
- Describing a not-yet-shipped engine-config file lane as if it exists
  (AGENTS.md §9 factual-current-state rule): state the recipe lane is
  preview-only and the default recipe is rebuilt per-frame until `GRAPHICS-106` /
  `RUNTIME-130` land.
- Moving recipe content authority away from the canonical doc into the
  legacy-background `rendering-three-pass.md`.

## Context
- `docs/architecture/frame-graph.md` is canonical per `docs/architecture/index.md`
  but is an ~18-line stub, while the recipe content currently lives in the
  legacy-background `rendering-three-pass.md`.
- The shipped recipe-config lane (`RenderRecipeConfig` schema id/version,
  side-effect-free `PreviewRenderRecipeConfig`, the fixed-core guard, diagnostic
  states) is undocumented in canonical architecture.
- Owner/layer: docs/architecture. Docs-only.

## Required changes
- [ ] Rewrite `frame-graph.md` to describe recipe-as-data: `FrameRecipeFeatures`
      gating, typed `FramePassId` / `FrameResourceId`, the per-frame
      `BuildDefaultFrameRecipe` path, and `DescribeDefaultFrameRecipe` introspection.
- [ ] Document the three edit lanes: config via `RenderRecipeConfig`, UI via the
      Sandbox editor preview, and agents via the side-effect-free preview + the
      fixed-core mutation guard.
- [ ] State current state factually: the recipe-config lane is preview-only and
      the default recipe is rebuilt per-frame until `GRAPHICS-106` / `RUNTIME-130`
      wire activation; cross-link those tasks.

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.
- [ ] `frame-graph.md` is no longer a stub.

## Docs
- [ ] This task is docs-only (`docs/architecture/frame-graph.md`, and
      cross-references in `index.md` if the classification needs a note).

## Acceptance criteria
- [ ] `frame-graph.md` describes recipe-driven composition and the three edit
      lanes, with factual current-state wording (preview-only).
- [ ] Doc-link check passes; skill mirrors (if mirrored) regenerate clean.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --quiet -- tools/agents/skills docs/agent || echo "mirror/doc drift to review"
```

## Forbidden changes
- Describing an unshipped engine-config file lane as current state.
- Moving recipe authority into legacy-background docs.
