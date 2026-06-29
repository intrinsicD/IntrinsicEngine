---
id: PROC-010
theme: H
depends_on: []
---
# PROC-010 — Encode P1/P3/P5 research-engine invariants in AGENTS.md + review checklist

## Goal
- Promote the research-engine design principles from descriptive architecture
  prose into binding, always-on contract invariants + per-PR checklist rows, so
  an agent following `AGENTS.md` literally cannot ship UI-only,
  abstraction-heavy, or non-recipe features without tripping a gate (P6 makes
  P1/P3/P5 enforceable the same way §6 already makes P2/P4 enforceable).

## Non-goals
- Adding a CI validator or a global backend-preference map for these principles
  (P1: ceremony without shipped value — prose + checklist rows are the right
  weight until a second GPU-capable dispatch family exists).
- Editing engine code.
- Applying the wording before the repository owner approves it (this task is
  authored as **draft-for-review**; the `Required changes` below are applied
  only after sign-off on the proposed text in `## Proposed wording (for review)`).

## Context
- The distilled principles are describable from `docs/architecture/*` but are
  binding nowhere in the contract. P2 (CPU→GPU method ordering) and P4 are
  enforceable via `AGENTS.md` §6 + the review checklists; P1/P3/P5 have no such
  authority.
- P1 (research-pragmatism) appears only reactively as
  "ceremony-without-shipped-value" in `docs/agent/agent-output-review-checklist.md`
  (weekly audit), not as a forward design rule.
- The owning surfaces are `AGENTS.md` §5 (coding rules) and
  `docs/agent/review-checklist.md` (per-PR rows), mirrored into skills by
  `tools/agents/sync_skills.py --write`.
- Optionally proposes a new convergence theme so the backlog map reflects the
  research-control-surface direction (see the wording section); the substantive
  tasks (`GRAPHICS-106`, `RUNTIME-130`, `CORE-003`, `RUNTIME-131`, `GEOM-052`,
  `GRAPHICS-107`, `RUNTIME-132`, `DOCS-003`, `DOCS-004`) are filed or retired
  under existing themes B/F and can be re-homed or cited under it on approval.

## Proposed wording (for review)

### A. Add to `AGENTS.md` §5 (Coding rules) — three always-on bullets

- **Research pragmatism (P1).** This is research-driving software: prefer the
  smallest construct that does the job. Plain `struct`s and free functions are
  the default for data-driven code (configs, params/result records, CPU/GPU
  descriptors, packed buffers). Introduce an interface, factory, wrapper,
  builder, or backend seam only when a *present* second caller, a layering
  boundary, a test-double surface, or a config/UI/agent-controllable variant
  axis requires it — one implementation is not a seam. Robustness means
  fail-closed and deterministic, not defensive ceremony.
- **Config lane is a first-class control surface (P3).** Engine-tunable behavior
  must be reachable through the config tree by config files, agents/CLI, **and**
  the UI as co-equal surfaces — never UI-only. New tuning state is expressed as
  serializable config that round-trips to a file and is applied through a
  side-effect-free preview/validate-then-apply path (the `RenderRecipeConfig`
  schema-id + version + diagnostics shape is the reference model). UI panels and
  agents drive the same validated apply path; a UI handler must not poke a
  subsystem through a private path the config lane cannot reproduce.
- **Recipe-driven frames and a readable main loop (P5).** Frame composition is
  data-driven: passes/resources are described by recipe data (`FrameRecipe*`),
  default recipes are derived/loaded at init, and the engine update loop reads as
  an ordered list of named phases (see `docs/architecture/frame-graph.md`). Do
  not hardcode pass order or composition behind imperative branches that the
  recipe data cannot express or introspect.

### B. Add to `docs/agent/review-checklist.md` — three per-PR rows

- [ ] Data-driven additions use plain structs/free functions; any new
      interface/factory/wrapper/backend seam has a present second caller, a
      layering boundary, a test double, or a config/UI/agent variant axis
      justifying it (P1).
- [ ] New engine-tunable state is reachable from config files and an agent/CLI
      path (not UI-only) and round-trips through the config lane with a
      side-effect-free preview/validate step (P3).
- [ ] Frame/composition changes are expressed as recipe data and stay
      introspectable; the main loop remains an ordered, readable phase list (P5).

### C. (Optional, owner decision) New convergence theme in `tasks/backlog/README.md`

> ### Theme I — Research control surface (P1)
> Make the engine fully controllable as research software: a first-class config
> lane (config files + agents/CLI + UI as co-equal surfaces), recipe-driven frame
> composition wired end-to-end, and CPU/GPU backend hooks present by convention.
> Members: `GRAPHICS-106`, `RUNTIME-130`, `CORE-003`, `RUNTIME-131`, `GEOM-052`,
> `GRAPHICS-107`, `RUNTIME-132`, `DOCS-003`, `DOCS-004`.

If approved, add `"I": "Research control surface"` to `THEME_NAMES` in
`tools/agents/generate_session_brief.py` and re-home the listed tasks to
`theme: I`.

## Required changes
- [ ] After owner approval, add the three §5 bullets (section A) to `AGENTS.md`.
- [ ] After owner approval, add the three per-PR rows (section B) to
      `docs/agent/review-checklist.md`.
- [ ] If the owner approves section C, add Theme I to `tasks/backlog/README.md`
      and `THEME_NAMES`, and re-home the listed tasks' front-matter `theme: I`.
- [ ] Re-run `python3 tools/agents/sync_skills.py --write` to refresh skill mirrors.
- [ ] Regenerate `python3 tools/agents/generate_session_brief.py` if theme
      front-matter changed.

## Tests
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.
- [ ] `git diff --quiet -- tools/agents/skills` after `sync_skills.py --write`
      (mirrors are in sync; `ci-docs.yml` enforces this).

## Docs
- [ ] `AGENTS.md` §5 carries the three new bullets (section A).
- [ ] `docs/agent/review-checklist.md` carries the three matching rows (section B).
- [ ] Skill mirrors regenerated; session brief regenerated if themes changed.

## Acceptance criteria
- [ ] P1/P3/P5 are stated as always-on `AGENTS.md` invariants with matching per-PR
      checklist rows.
- [ ] Skill mirrors and (if changed) the session brief are in sync; `ci-docs`
      structural checks pass.
- [ ] No engine code touched; no CI validator added for these principles.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --stat -- tools/agents/skills
```

## Forbidden changes
- Applying the wording before owner approval.
- Adding a CI validator, reflection layer, or global backend-preference map for
  these principles.
- Touching engine code.
