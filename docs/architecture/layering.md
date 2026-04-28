# Layering Rules

This document defines enforceable layer dependencies for IntrinsicEngine.

## Allowed dependencies

- `core` -> _none_
- `geometry` -> `core`
- `assets` -> `core`
- `ecs` -> `core` (+ geometry handles/types only when explicitly required)
- `graphics/rhi` -> `core`
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views
- `runtime` -> all lower layers; owns composition/wiring
- `app` -> `runtime`

## Prohibited dependencies

- Any lower layer importing a higher layer.
- Graphics importing live ECS/gameplay ownership types.
- App symbols imported by lower layers.
- Undocumented legacy compatibility shortcuts.

## Enforcement

- Policy source of truth: `AGENTS.md`.
- Scripted enforcement path: `tools/repo/check_layering.py` (introduced in RORG-074).
- Temporary exceptions must be documented in `tasks/active/0000-repo-reorganization-tracker.md` with removal task IDs.
