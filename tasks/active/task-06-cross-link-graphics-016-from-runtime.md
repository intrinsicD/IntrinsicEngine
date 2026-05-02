# Task 6 — Cross-link GRAPHICS-016 from runtime backlog

- Status: completed
- Owner: Claude (claude/next-active-task-NtmGp)
- Branch / PR: `claude/next-active-task-NtmGp`
- Next verification step: `python3 tools/docs/check_doc_links.py --root . --strict` and `python3 tools/agents/check_task_policy.py --root . --strict` after adding the runtime cross-link (both pass; 0 findings, 145 relative links resolved).

`tasks/backlog/runtime/README.md` has been added. It indexes the existing runtime backlog (`RORG-031-runtime-composition.md`) and cross-links GRAPHICS-016 with explicit notes that runtime owns live ECS access, extraction, sidecar/cache mappings, dirty-domain interpretation, deletion events, and compaction/relocation handoff; that graphics must not import live ECS ownership; and that GRAPHICS-016 is the first implementation gate before pass implementation. The full task content is not duplicated. Validators pass with zero findings.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Make the runtime-owned rendering extraction gate visible from the runtime backlog.

## Problem

GRAPHICS-016 is filed under `tasks/backlog/rendering` but its owner is `src/runtime` for extraction/wiring and `src/graphics/renderer` for consumed snapshot contracts. Runtime reviewers may miss it.

## Required changes

1. Create a small cross-link task or README note under `tasks/backlog/runtime/`.

   Preferred:
   - Create `tasks/backlog/runtime/RUNTIME-RENDERING-EXTRACTION-GATE.md` only if the repo already allows non-numbered cross-link notes.

   Alternative:
   - Update `tasks/backlog/runtime/README.md` if it exists.

   Alternative:
   - If no runtime README exists, create `tasks/backlog/runtime/README.md`.

2. The runtime note should:
   - Link to GRAPHICS-016.
   - State that runtime owns ECS access, extraction, sidecar mappings, dirty-domain interpretation, deletion events, and compaction-relocation handoff.
   - State that graphics must not import live ECS ownership.
   - State that GRAPHICS-016 must land before most rendering pass implementation work.

3. Do not duplicate the full task content.

## Acceptance criteria

- Runtime backlog readers can discover GRAPHICS-016.
- No duplicate implementation task is created.
- Links resolve.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- No C++ changes.
- No task ownership rewrite except cross-linking.

GRAPHICS-016 is runtime-owned but filed in rendering; cross-linking is the cleanest fix.
