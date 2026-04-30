# Layering Rules

This document defines enforceable layer dependencies for IntrinsicEngine.

## Allowed dependencies

- `core` -> _none_
- `geometry` -> `core`
- `assets` -> `core`
- `ecs` -> `core` (+ geometry handles/types only when explicitly required)
- `graphics/rhi` -> `core`
- `graphics/assets` -> `core`, asset IDs (`Asset.Registry` types only — no live `AssetService` traffic), `graphics/rhi`
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views
- `runtime` -> all lower layers; owns composition/wiring
- `app` -> `runtime`

## Prohibited dependencies

- Any lower layer importing a higher layer.
- Graphics importing live ECS/gameplay ownership types.
- App symbols imported by lower layers.
- Runtime imports in lower layers (`core`, `geometry`, `assets`, `ecs`, `graphics/*`, `platform`).
- Undocumented legacy compatibility shortcuts.

## Enforcement

- Policy source of truth: `AGENTS.md`.
- Scripted enforcement path: `tools/repo/check_layering.py`.
- Temporary exceptions are tracked in `tools/repo/layering_allowlist.yaml` and must include task IDs and expiry notes.
- The checker runs in warning mode by default and supports `--strict` for CI hard-fail.

### Local verification

```bash
python3 tools/repo/check_layering.py --root src
python3 tools/repo/check_layering.py --root src --strict
```

### Migration note

`src/legacy` may temporarily violate final boundaries only when the dependency is allowlisted and tracked in a current task under `tasks/active/` with a removal task.
