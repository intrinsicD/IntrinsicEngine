# Source-Tree Reorganization

## Goal

Maintain the final explicit source layout while preserving buildability and reviewability for any remaining path cleanup.

## Current migration shape

- Source root model: `src/{core,assets,ecs,geometry,physics,graphics,runtime,platform,app}`

## Execution rules

- Perform mechanical file moves separately from semantic code changes.
- Prefer `git mv` for path transitions to preserve history.
- Update CMake, docs, scripts, and CI references in the same move PR.
- Run the strongest relevant verification subset for every move.

## Planned phases

1. Establish path and policy scaffolding (docs, task tracking, guard scripts).
2. Introduce path abstraction in build scripts where required.
3. Retire the legacy compatibility tree after promoted parity exists.
4. Tighten layering/docs/task checks to strict mode.

## Risks and mitigations

- **Risk:** Broken include/CMake paths after moves.
  - **Mitigation:** Move table driven updates and immediate compile/test checks.
- **Risk:** Mixed mechanical and semantic changes hide regressions.
  - **Mitigation:** Enforce split PR policy and tracker review checklist.
- **Risk:** Drift between docs and actual tree.
  - **Mitigation:** Update migration docs and run link/inventory checkers each step.
