# Task 5 — Add rendering backlog README with dependency DAG

- Status: planned (queued for Codex)
- Owner: TBD
- Branch / PR: TBD
- Next verification step: `python3 tools/docs/check_doc_links.py --root . --strict` after creating `tasks/backlog/rendering/README.md`.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Create a rendering backlog README that makes the GRAPHICS task dependency order machine-readable enough for agents.

Create:

`tasks/backlog/rendering/README.md`

## Required content

1. State that GRAPHICS-001 is the canonical rendering backlog index.
2. State that RORG-031B, if present, is historical/superseded planning and not the active implementation path.
3. Add a dependency-ordered task DAG.

   Minimum DAG:
   - GRAPHICS-021 must complete before further rendering task churn.
   - GRAPHICS-016 must be the first implementation gate before graphics pass work.
   - GRAPHICS-002 depends on GRAPHICS-016 or must explicitly avoid touching runtime extraction.
   - GRAPHICS-003 depends on GRAPHICS-002.
   - GRAPHICS-004 depends on GRAPHICS-002.
   - GRAPHICS-005 depends on GRAPHICS-004.
   - GRAPHICS-006 depends on GRAPHICS-002.
   - GRAPHICS-007 depends on GRAPHICS-002 and GRAPHICS-004.
   - GRAPHICS-008 depends on GRAPHICS-003, GRAPHICS-006, GRAPHICS-007.
   - GRAPHICS-009 depends on GRAPHICS-008.
   - GRAPHICS-010 depends on GRAPHICS-002, GRAPHICS-003, GRAPHICS-007.
   - GRAPHICS-011 depends on GRAPHICS-010.
   - GRAPHICS-012 depends on GRAPHICS-002, GRAPHICS-007, GRAPHICS-008, GRAPHICS-010.
   - GRAPHICS-013 or its split replacements depend on GRAPHICS-003 and GRAPHICS-008/009 where needed.
   - GRAPHICS-014 depends on GRAPHICS-002, GRAPHICS-010, GRAPHICS-015 where texture/atlas resources are needed.
   - GRAPHICS-015 depends on GRAPHICS-006 where material texture references are involved.
   - GRAPHICS-017 depends on GRAPHICS-012 for picking handoff, but can define camera packet contracts earlier.
   - GRAPHICS-018 depends on stable CPU/null contracts from GRAPHICS-002/003/004/006/007/008/009/013.
   - GRAPHICS-019 can run as planning in parallel but must not put IO ownership into graphics.
   - GRAPHICS-020 is final retirement gating after parity tasks.

4. Add an "Agent selection rules" section:
   - Prefer the earliest unblocked task.
   - Do not start pass implementation before GRAPHICS-016 extraction ownership is resolved.
   - Do not mix docs-only cleanup with C++ behavior changes.
   - Do not copy legacy code into promoted graphics layers.

5. Add links to:
   - GRAPHICS-001
   - `AGENTS.md`
   - `docs/architecture/graphics.md`
   - `docs/architecture/rendering-three-pass.md`
   - `docs/migration/nonlegacy-parity-matrix.md`

## Acceptance criteria

- A human or agent can determine the next valid task without reading every file.
- The README does not conflict with GRAPHICS-001.
- Docs links resolve.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- No implementation changes.
- No moving tasks except links if necessary.

The current GRAPHICS-001 already has the intended order, but it is prose-only.
