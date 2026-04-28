# RORG-0000 Repository Reorganization Tracker

This tracker is the canonical migration status log for the IntrinsicEngine repository reorganization backlog.

- **Scope:** Track all RORG todos from this migration plan.
- **Status values:** `not-started`, `in-progress`, `blocked`, `done`.
- **Process rule:** Source tree movement must be **mechanical only** (path/layout updates) and reviewed separately from semantic code changes.

## Current branch / PR

- **Branch:** `work`
- **PR:** `TBD`

## Temporary compatibility shims

| Shim | Introduced by | Removal task | Status | Notes |
|---|---|---|---|---|
| _None yet_ | - | - | - | Add entries when wrappers/aliases are introduced. |

## Final cleanup blockers

| Blocker | Owner | Tracking task | Status | Notes |
|---|---|---|---|---|
| _None recorded yet_ | TBD | TBD | not-started | Add blockers as they appear. |

## Final-state checklist (target layout)

- [ ] `AGENTS.md` exists at repository root.
- [ ] Root docs/build files are normalized: `README.md`, `CMakeLists.txt`, `CMakePresets.json`.
- [ ] `src/` contains `legacy/`, `core/`, `assets/`, `ecs/`, `geometry/`, `graphics/`, `runtime/`, `platform/`, `app/`.
- [ ] `src/graphics/` contains `rhi/`, `vulkan/`, `framegraph/`, `renderer/`.
- [ ] `methods/` exists with `_template/`, `geometry/`, `rendering/`, `physics/`, `papers/`.
- [ ] `benchmarks/` exists with `geometry/`, `rendering/`, `datasets/`, `baselines/`, `reports/`, `runners/`.
- [ ] `tests/` contains `unit/`, `contract/`, `integration/`, `regression/`, `gpu/`, `benchmark/`, `support/`.
- [ ] `docs/` contains `index.md`, `architecture/`, `adr/`, `methods/`, `benchmarking/`, `agent/`, `migration/`, `api/`.
- [ ] `tasks/` contains `active/`, `backlog/`, `done/`, `templates/`.
- [ ] `tools/` contains `repo/`, `docs/`, `ci/`, `benchmark/`, `agents/`, `analysis/`.
- [ ] `.github/workflows/` contains split workflows (`pr-fast.yml`, `ci-linux-clang.yml`, `ci-sanitizers.yml`, `ci-docs.yml`, `ci-bench-smoke.yml`, `nightly-deep.yml`).
- [ ] `.github/pull_request_template.md` exists.
- [ ] `.codex/config.yaml` and `.claude/settings.json` are present and consistent with root contract.

## Todo status board

| ID | Title | Status | Branch / PR | Notes |
|---|---|---|---|---|
| RORG-000 | Create reorganization tracking task | done | current branch / TBD | Tracker created. |
| RORG-001 | Capture current repository inventory | done | current branch / TBD | `docs/migration/current-repo-inventory.md` added. |
| RORG-002 | Add a root final-state layout document | done | current branch / TBD | `docs/migration/target-repo-layout.md` added and linked from README. |
| RORG-003 | Add initial repo hygiene checks without enforcing them yet | done | current branch / TBD | Added warning-mode checks and strict flags under `tools/repo`, `tools/docs`, and `tools/ci`. |
| RORG-010 | Create canonical `AGENTS.md` | not-started | - |  |
| RORG-011 | Convert `CLAUDE.md` into a thin redirect | not-started | - |  |
| RORG-012 | Convert Copilot instructions into a thin redirect | not-started | - |  |
| RORG-013 | Fix `.codex/config.yaml` | not-started | - |  |
| RORG-014 | Create agent docs package | not-started | - |  |
| RORG-020 | Create `docs/index.md` | not-started | - |  |
| RORG-021 | Create architecture docs index and taxonomy | not-started | - |  |
| RORG-022 | Move ADRs into `docs/adr/` | not-started | - |  |
| RORG-023 | Create migration docs package | not-started | - |  |
| RORG-024 | Normalize root planning docs | not-started | - |  |
| RORG-025 | Add docs link validation to CI warning mode | not-started | - |  |
| RORG-030 | Create task directory structure | not-started | - |  |
| RORG-031 | Split current `TODO.md` into task files | not-started | - |  |
| RORG-032 | Add task validator | not-started | - |  |
| RORG-033 | Replace TODO policy script | not-started | - |  |
| RORG-040 | Create `methods/` root and package template | not-started | - |  |
| RORG-041 | Add method manifest schema | not-started | - |  |
| RORG-042 | Create method docs package | not-started | - |  |
| RORG-043 | Add canonical method API concept doc | not-started | - |  |
| RORG-044 | Add first sample method package without real algorithm code | not-started | - |  |
| RORG-050 | Create benchmark directory structure | not-started | - |  |
| RORG-051 | Add benchmark manifest schema and validator | not-started | - |  |
| RORG-052 | Add benchmark output JSON schema | not-started | - |  |
| RORG-053 | Add benchmark docs package | not-started | - |  |
| RORG-054 | Add minimal benchmark smoke runner skeleton | not-started | - |  |
| RORG-060 | Create new tests directory skeleton | not-started | - |  |
| RORG-061 | Add test classification policy | not-started | - |  |
| RORG-062 | Add CMake support for test subdirectories and labels | not-started | - |  |
| RORG-063 | Move Core unit tests | not-started | - |  |
| RORG-064 | Move Geometry unit tests | not-started | - |  |
| RORG-065 | Move Asset and ECS unit tests | not-started | - |  |
| RORG-066 | Move Graphics tests | not-started | - |  |
| RORG-067 | Move Runtime and app integration tests | not-started | - |  |
| RORG-068 | Move regression and benchmark tests | not-started | - |  |
| RORG-069 | Delete old root test clutter | not-started | - |  |
| RORG-070 | Create target tools subdirectories | not-started | - |  |
| RORG-071 | Move repo/policy tools | not-started | - |  |
| RORG-072 | Move analysis/performance tools | not-started | - |  |
| RORG-073 | Move performance regression script | not-started | - |  |
| RORG-074 | Add layering checker | not-started | - |  |
| RORG-075 | Add module inventory generator for final layout | not-started | - |  |
| RORG-076 | Add docs sync checker | not-started | - |  |
| RORG-080 | Add pull request template | not-started | - |  |
| RORG-081 | Create `pr-fast.yml` | not-started | - |  |
| RORG-082 | Replace compressed `build.yml` with `ci-linux-clang.yml` | not-started | - |  |
| RORG-083 | Add sanitizer workflow | not-started | - |  |
| RORG-084 | Add docs workflow | not-started | - |  |
| RORG-085 | Add benchmark smoke workflow | not-started | - |  |
| RORG-086 | Add nightly deep workflow | not-started | - |  |
| RORG-087 | Add workflow naming/check script | not-started | - |  |
| RORG-090 | Write source-tree move plan before moving files | not-started | - |  |
| RORG-091 | Add CMake path abstraction for source roots | not-started | - |  |
| RORG-092 | Move legacy non-Geometry source into `src/legacy/` | not-started | - |  |
| RORG-093 | Promote Geometry to `src/geometry/` | not-started | - |  |
| RORG-094 | Move `src_new/Core` to `src/core` | not-started | - |  |
| RORG-095 | Move `src_new/Assets` to `src/assets` | not-started | - |  |
| RORG-096 | Move `src_new/ECS` to `src/ecs` | not-started | - |  |
| RORG-097 | Move `src_new/Graphics/RHI` to `src/graphics/rhi` | not-started | - |  |
| RORG-098 | Move Vulkan backend to `src/graphics/vulkan` | not-started | - |  |
| RORG-099 | Move remaining graphics modules | not-started | - |  |
| RORG-100 | Move Platform to `src/platform` | not-started | - |  |
| RORG-101 | Move Runtime to `src/runtime` | not-started | - |  |
| RORG-102 | Move App/Sandbox to `src/app` | not-started | - |  |
| RORG-103 | Remove empty `src_new/` | not-started | - |  |
| RORG-104 | Tighten source layering checker to strict mode | not-started | - |  |
| RORG-110 | Rewrite README around final layout | not-started | - |  |
| RORG-111 | Add root allowlist and enforce root hygiene | not-started | - |  |
| RORG-112 | Remove compatibility wrappers after docs are updated | not-started | - |  |
| RORG-120 | Regenerate final module inventory | not-started | - |  |
| RORG-121 | Final docs link strictness | not-started | - |  |
| RORG-122 | Final method and benchmark manifest strictness | not-started | - |  |
| RORG-123 | Final task policy strictness | not-started | - |  |
| RORG-130 | Add architecture review checklist to CI and PR process | not-started | - |  |
| RORG-131 | Add method implementation review checklist | not-started | - |  |
| RORG-132 | Add benchmark review checklist | not-started | - |  |
| RORG-133 | Add final cleanup audit | not-started | - |  |
