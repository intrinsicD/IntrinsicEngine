# Source-Tree Reorganization

## Goal

Transition from the current mixed source roots to the final explicit source layout while preserving buildability and reviewability.

## Current migration shape

- Legacy root: `src/`
- Promotion root: `src_new/`
- Target root model: `src/{legacy,core,assets,ecs,geometry,graphics,runtime,platform,app}`

## Execution rules

- Perform mechanical file moves separately from semantic code changes.
- Prefer `git mv` for path transitions to preserve history.
- Update CMake, docs, scripts, and CI references in the same move PR.
- Run the strongest relevant verification subset for every move.

## Planned phases

1. Establish path and policy scaffolding (docs, task tracking, guard scripts).
2. Introduce path abstraction in build scripts where required.
3. Move legacy source under `src/legacy/` and promote canonical roots.
4. Remove `src_new/` after promoted modules are stable.
5. Tighten layering/docs/task checks to strict mode.

## Risks and mitigations

- **Risk:** Broken include/CMake paths after moves.
  - **Mitigation:** Move table driven updates and immediate compile/test checks.
- **Risk:** Mixed mechanical and semantic changes hide regressions.
  - **Mitigation:** Enforce split PR policy and tracker review checklist.
- **Risk:** Drift between docs and actual tree.
  - **Mitigation:** Update migration docs and run link/inventory checkers each step.
