# DOCS-001 — Reduce `docs/architecture/graphics.md` to contract + status

## Goal
- [ ] Reshape [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md) (currently ~793 lines, much of it multi-paragraph prose embedded in single bullet items) into a short canonical contract that a contributor can read in five minutes, plus a short pointer list to the deeper material it currently inlines.
- [ ] Move the inlined narrative (Vulkan operational gate decisions, GRAPHICS-017Q/018Q/032/033/etc. clarification prose, runtime/editor handoff inventories) out of the canonical architecture doc and into:
  - ADRs under [`docs/adr/`](../../../docs/adr/) for irreversible architecture decisions; or
  - migration notes under [`docs/migration/`](../../../docs/migration/) for time-bounded transition state; or
  - the originating tasks under `tasks/done/` (where the prose was authored as part of the slice).
- [ ] Leave `docs/architecture/graphics.md` factual about *current* state of canonical graphics layers, per `AGENTS.md` §9 doc sync policy.

## Non-goals
- [ ] Do not change `src/graphics/*` source.
- [ ] Do not introduce new architecture invariants. `AGENTS.md` remains authoritative.
- [ ] Do not delete the inlined prose; relocate it. Information preservation matters because parts of the prose record decisions that are not captured anywhere else.
- [ ] Do not reduce `docs/architecture/graphics.md` to a stub. The canonical contract content (sublayer split, dependency rules, frame lifecycle outline) must remain.
- [ ] Do not perform the same reduction on other architecture docs in this task. If `docs/architecture/runtime.md` (19 lines), `docs/architecture/geometry.md` (77 lines), or `docs/architecture/ecs.md` (14 lines) need work, that is a separate task.

## Context
- Owner/layer: docs only (`docs/architecture/`, `docs/adr/`, `docs/migration/`).
- Current state (2026-05-16):
  - `docs/architecture/graphics.md` is 793 lines.
  - Many of its bullet items are 30–80-line single paragraphs with embedded enumerations of "follow-up clarifications" (e.g. one bullet under `## GPU scene ownership` covers GRAPHICS-017Q camera-controller decisions, gizmo hit testing ownership, editor handoff, modifier-key behavior, transform application ownership, undo policy, and legacy `Graphics.TransformGizmo` parity in a single ~150-line paragraph).
  - The document mixes three kinds of content that should live in different places:
    1. **Canonical contract** (sublayer dependency rules, runtime/RHI frame lifecycle, render-graph ownership).
    2. **Decision records** (GRAPHICS-033 nine-step operational gate, validation-layer policy, fallback-reason taxonomy, sampler-anisotropy policy — each is effectively an ADR embedded in prose form).
    3. **Migration/handoff inventories** (GRAPHICS-017Q camera/gizmo handoff details, GRAPHICS-018Q/033/etc. clarification follow-ups, runtime/editor handoff matrices that duplicate what `docs/migration/nonlegacy-parity-matrix.md` already records).
- Symptoms of this drift:
  - The document is hard to read end-to-end and almost impossible to keep current.
  - Architectural contracts and ephemeral decisions are not visually distinguishable.
  - Cross-references from canonical sources point at run-on paragraphs that are likely to rot.
- Reference for target shape: [`docs/architecture/overview.md`](../../../docs/architecture/overview.md) (22 lines) and [`docs/architecture/layering.md`](../../../docs/architecture/layering.md) (~41 lines) demonstrate the intended concision for canonical architecture docs.
- ADR template: [`docs/adr/`](../../../docs/adr/) already has 3 records (0001..0003). Each new extracted decision becomes an ADR (`0004`, `0005`, ...).
- Migration target: [`docs/migration/`](../../../docs/migration/) already hosts the legacy retirement and parity matrix documents; extracted handoff inventories live alongside them.

## Required changes

### Slice 1 — inventory and classify (no edits to `graphics.md` yet)
- [ ] Read `docs/architecture/graphics.md` end-to-end and classify each section/bullet as one of:
      `canonical-contract` | `decision-record` | `migration-inventory` | `obsolete`.
- [ ] Record the classification as a table in this task file under a new "Classification" subsection appended to the Context section above. The table columns are: line-range, current heading, classification, target destination (`graphics.md (keep)` | `docs/adr/NNNN-*.md` | `docs/migration/<name>.md` | `tasks/done/<id>.md` cross-link).
- [ ] Slice-1 commit is doc-only and changes only this task file.

### Slice 2 — extract decision records to ADRs
- [ ] For each row classified `decision-record`, author an ADR under `docs/adr/` (`0004-*`, `0005-*`, ...). Use the existing ADR pattern. The ADR captures the decision and its rationale; the original prose becomes the ADR's body.
- [ ] Update `docs/adr/index.md` to list the new records.
- [ ] Update `docs/architecture/graphics.md` to replace each extracted block with a one-line pointer (`See [ADR-NNNN](../adr/NNNN-*.md).`).
- [ ] Slice-2 commit per ADR (each ADR + its `graphics.md` pointer update is a single commit).

### Slice 3 — extract migration inventories
- [ ] For each row classified `migration-inventory`, either fold the content into an existing `docs/migration/*` file (preferred) or author a new `docs/migration/<topic>-handoff-inventory.md` file. Cross-link from `docs/migration/index.md`.
- [ ] Update `docs/architecture/graphics.md` to replace each extracted block with a one-line pointer to the migration doc.
- [ ] Slice-3 commit per migration file extracted.

### Slice 4 — final tightening
- [ ] After slices 1–3, `docs/architecture/graphics.md` should fit on roughly two screens of reading. Tighten any remaining prose to one-sentence-per-bullet form. Keep the canonical sublayer split, the dependency rules, the frame lifecycle outline, the renderer/RHI seam, and the GPU scene ownership contract.
- [ ] Add a "Pointers" section at the bottom listing every ADR and migration doc extracted in slices 2 and 3.
- [ ] Slice-4 commit is the final `graphics.md` tightening.

## Tests
- [ ] No code is produced by this task. No automated tests.
- [ ] Each slice must pass `python3 tools/docs/check_doc_links.py --root .` (no broken relative links).
- [ ] Each slice must pass `python3 tools/agents/check_task_policy.py --root . --strict`.
- [ ] Final `graphics.md` line count target: ≤ 250 lines. (Current: 793.)

## Docs
- [ ] [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md) reduced to canonical contract + pointers.
- [ ] [`docs/adr/index.md`](../../../docs/adr/index.md) lists each new ADR.
- [ ] [`docs/migration/index.md`](../../../docs/migration/index.md) lists any new migration docs.
- [ ] [`docs/architecture/index.md`](../../../docs/architecture/index.md) status note for `graphics.md` reflects the reduction.

## Acceptance criteria
- [ ] Final `docs/architecture/graphics.md` is ≤ 250 lines.
- [ ] No prose paragraph in the final `graphics.md` exceeds 5 lines (single-paragraph clarifications relocate to ADRs/migration docs).
- [ ] Every relocated decision is captured in either an ADR (decision records), a migration doc (handoff inventories), or a `tasks/done/` cross-link (slice-specific clarifications). No content is lost.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes with no broken relative links.
- [ ] Per `AGENTS.md` §5 ("keep patches small and scoped to one task when possible") each slice (1, 2-per-ADR, 3-per-migration-doc, 4) lands as its own commit/PR.
- [ ] No `src/graphics/*` source or behavior changes in any slice.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Per-slice line-count check:
wc -l docs/architecture/graphics.md

# Confirm no source changes leaked in:
git diff --stat -- 'src/**'
```

## Forbidden changes
- Mixing mechanical relocation with semantic refactor of the architecture contract.
- Deleting decision content without relocating it to an ADR or migration doc.
- Folding multiple ADRs into a single ADR to keep the count low; one decision per ADR.
- Bundling multiple slices into a single commit.
- Changing `src/graphics/*` source under cover of this doc task.
- Reducing `graphics.md` past the canonical contract floor (sublayer split, dependency rules, frame lifecycle outline, renderer/RHI seam, GPU scene ownership contract must remain).
