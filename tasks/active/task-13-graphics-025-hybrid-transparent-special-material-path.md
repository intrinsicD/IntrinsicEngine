# Task 13 — Add missing task: hybrid/transparent/special-material forward path

- Status: completed (2026-05-02)
- Owner: Codex (current branch)
- Branch / PR: current branch / TBD
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict`.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Create a future-facing task for the real hybrid rendering path and transparent/special-material forward overlays.

Create:

`tasks/backlog/rendering/GRAPHICS-025-hybrid-transparent-special-material-path.md`

## Goal

Define the follow-up architecture and implementation path for the real hybrid renderer beyond the current deferred-backed staging mode.

## Context

`docs/architecture/rendering-three-pass.md` currently treats Hybrid as deferred-backed staging. Future renderer work needs a clear owner for transparent materials, special forward-only materials, alpha blending/OIT, and hybrid composition without bloating GRAPHICS-008 or GRAPHICS-009.

## Required scope

- Define hybrid path semantics:
  - deferred opaque base
  - forward overlays for transparent/special materials
  - line/point/debug overlays remain independent
- Define material classification:
  - opaque
  - alpha-mask
  - transparent
  - unlit
  - special forward-only
- Define resource requirements:
  - SceneColorHDR
  - SceneDepth
  - optional velocity/history buffers if future TAA/motion vectors are referenced
  - OIT resources if selected later
- Define non-goals:
  - no clustered lighting yet
  - no IBL/area-light implementation
  - no full transparency implementation unless split into subtask
- Cross-link:
  - GRAPHICS-006 material registry
  - GRAPHICS-007 draw buckets
  - GRAPHICS-008 surface/G-buffer
  - GRAPHICS-009 lighting/shadows
  - GRAPHICS-013A postprocess
  - future transparency/OIT task if created

## Tests

- Planning task only initially:
  - task policy and docs-link checks.
- Future implementation subtasks must add `contract;graphics` and optional `gpu;vulkan` tests.

## Docs

- Update rendering-three-pass hybrid section to point to this task.

## Acceptance criteria

- Hybrid is no longer an unowned TODO hidden in architecture prose.
- Deferred, forward, and hybrid terms are not ambiguous for future Codex work.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No implementation.
- No shader changes.
- No lighting feature expansion.
