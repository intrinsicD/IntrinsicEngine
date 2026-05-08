# HARDEN-065 — Keep Sandbox app on the runtime boundary

## Goal
Refactor the generic `Sandbox` app so app code consumes runtime-facing APIs only and keeps specialized engine behavior out of `Sandbox::App`.

## Non-goals
- Do not add sandbox gameplay, editor, physics, or demo-scene behavior.
- Do not change runtime frame-loop semantics.
- Do not change renderer/backend behavior beyond preserving the previous reference configuration values.

## Context
`AGENTS.md` defines `app -> runtime only`. `src/app/Sandbox/main.cpp` previously imported lower-layer core config modules directly to build `EngineConfig`, even though `Sandbox::App` itself was correctly policy-light.

## Required changes
- Add a runtime-owned app-facing helper for the reference engine configuration.
- Update `src/app/Sandbox/main.cpp` to import `Extrinsic.Runtime.Engine` and `Extrinsic.Sandbox` only.
- Remove direct `ExtrinsicCore` linkage from the sandbox executable.
- Update app/runtime docs to describe the boundary.

## Tests
- Build the sandbox target.
- Run the source layering check.
- Run task and documentation validators.

## Docs
- Update `src/runtime/README.md`.
- Update `src/app/README.md`.
- Update `src/app/Sandbox/README.md`.

## Acceptance criteria
- [x] `Sandbox::App` remains empty/policy-light.
- [x] `src/app/Sandbox/main.cpp` no longer imports lower-layer core config modules.
- [x] Sandbox obtains reference configuration through runtime.
- [x] App docs state that app code depends on runtime only.

## Verification
```bash
cmake --build --preset ci --target ExtrinsicSandbox
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- No app-specific feature logic in `Sandbox::App`.
- No lower-layer convenience imports from `src/app`.
- No runtime behavior changes unrelated to the app-facing configuration seam.

## Execution log
- 2026-05-08: Added `Runtime::CreateReferenceEngineConfig()` and switched sandbox main to use it.
- 2026-05-08: Removed direct `ExtrinsicCore` sandbox linkage and updated runtime/app docs.
- 2026-05-08: Ran strict layering, task policy, and docs-link checks successfully. After `HARDEN-066` fixed a stale test source name that blocked CMake regeneration, `cmake --build --preset ci --target ExtrinsicSandbox` completed successfully.

## Completion metadata
- Completion date: 2026-05-08.
- Commit reference: pending current workspace/PR.
- Follow-up: None.

