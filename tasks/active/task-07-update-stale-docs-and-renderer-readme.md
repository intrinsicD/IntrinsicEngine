# Task 7 — Update stale docs and renderer README ownership contract

- Status: completed
- Owner: Claude (claude/next-active-task-mtji0)
- Branch / PR: `claude/next-active-task-mtji0`
- Next verification step: `python3 tools/docs/check_doc_links.py --root . --strict` and `python3 tools/agents/check_task_policy.py --root . --strict` after rewriting stale rendering docs (both pass; 0 findings).

`docs/architecture/rendering-three-pass.md` "Where Active Work Lives" now points at `tasks/backlog/rendering/GRAPHICS-001-rendering-parity-inventory.md` and `tasks/backlog/rendering/README.md` as the canonical near-term execution sources, retains `docs/roadmap.md` for medium/long-horizon planning, and keeps the historical migration narrative link as archival. `docs/roadmap.md` no longer references stale `tasks/backlog/legacy-todo.md` anchors (B4, C4, C9, F1–F7) or the missing `adr-o2-pragmatic-medium-runtime-refactor.md` ADR; runtime/frame-pipeline execution now points at the runtime backlog index and `GRAPHICS-016`, the GPU-driven pipeline plan cross-references the structured GRAPHICS backlog, and the UI Architecture & Feature Wiring section is rewritten as plain completion notes. `src/graphics/renderer/README.md` ownership contract no longer claims graphics may depend on ECS component contracts; instead it states that runtime owns ECS access/extraction, graphics consumes immutable snapshots/views and owns GPU resources, graphics must not import live ECS ownership or store graphics GPU handles in canonical ECS components, and the architecture references now include `AGENTS.md`, `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, and `docs/migration/nonlegacy-parity-matrix.md` (with the parity matrix labeled historical/advisory). Validators pass with zero findings.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Align rendering docs and renderer README with the current agent contract and structured rendering backlog.

## Required changes

1. Update `docs/architecture/rendering-three-pass.md`:
   - Replace stale "Where Active Work Lives" reference to `tasks/backlog/legacy-todo.md` with `tasks/backlog/rendering/GRAPHICS-001-rendering-parity-inventory.md` and `tasks/backlog/rendering/README.md`.
   - Keep `docs/roadmap.md` for medium/long horizon only.

2. Update `docs/roadmap.md`:
   - Remove or rewrite stale references to `tasks/backlog/legacy-todo.md` anchors such as B4, C4, C9 where those anchors no longer exist.
   - Point rendering execution work to structured GRAPHICS tasks.
   - Keep roadmap language high-level and future-facing.

3. Update `src/graphics/renderer/README.md`:
   - Remove or rewrite any claim that graphics may depend on ECS ownership/contracts.
   - State the current intended boundary:
     - runtime owns ECS access and extraction;
     - graphics consumes immutable snapshots/views and owns GPU resource/state transitions;
     - graphics/rendergraph must not import live ECS or runtime ownership.
   - Update architecture references to include:
     - `AGENTS.md`
     - `docs/architecture/graphics.md`
     - `docs/architecture/rendering-three-pass.md`
     - `docs/migration/nonlegacy-parity-matrix.md`
   - Keep historical docs clearly labeled as historical/advisory if referenced.

## Acceptance criteria

- No active rendering doc sends agents to `legacy-todo.md` for near-term execution.
- renderer README no longer contradicts `AGENTS.md`.
- Historical docs are labeled as historical/advisory when linked.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- No C++ changes.
- No shader changes.
- No task scope changes except doc references.

The current renderer README still says graphics may depend on ECS component contracts, which conflicts with the newer boundary.
