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
| RORG-010 | Create canonical `AGENTS.md` | done | current branch / TBD | Root contract created with required sections and invariants. |
| RORG-011 | Convert `CLAUDE.md` into a thin redirect | done | current branch / TBD | Reduced to an authoritative redirect to `/AGENTS.md` plus doc links. |
| RORG-012 | Convert Copilot instructions into a thin redirect | done | current branch / TBD | Replaced with thin redirect to `/AGENTS.md` and agent docs. |
| RORG-013 | Fix `.codex/config.yaml` | done | current branch / TBD | Updated to C++23 + valid CI preset verification command + AGENTS pointer; removed duplicated style policy. |
| RORG-014 | Create agent docs package | done | work / TBD | Added `docs/agent/*` expanded workflow package and linked from `AGENTS.md`. |
| RORG-020 | Create `docs/index.md` | done | current branch / TBD | Added `docs/index.md` with required sections; linked from `README.md`. |
| RORG-021 | Create architecture docs index and taxonomy | done | work / TBD | Added canonical architecture docs and status taxonomy in `docs/architecture/index.md`. |
| RORG-022 | Move ADRs into `docs/adr/` | done | current branch / TBD | Moved ADRs to `docs/adr/`, added ADR index/template, and updated links. |
| RORG-023 | Create migration docs package | done | work / TBD | Added `docs/migration/index.md`, `legacy-retirement.md`, `source-tree-reorganization.md`, and `src-new-status.md`; kept generated inventory path stable pending generator migration task. |
| RORG-024 | Normalize root planning docs | done | work / TBD | Moved root planning docs under `docs/` and `tasks/`; removed root planning markdown clutter. |
| RORG-025 | Add docs link validation to CI warning mode | done | work / TBD | Added `.github/workflows/ci-docs.yml` with PR trigger and warning-mode docs link check; strict mode deferred to RORG-121. |
| RORG-030 | Create task directory structure | done | current branch / TBD | Added `tasks/README.md`, status READMEs, and task templates under `tasks/templates/`. |
| RORG-031 | Split current `TODO.md` into task files | done | work / TBD | Added structured backlog seed tasks under architecture/rendering/runtime/src-new/geometry/ui and converted legacy backlog file to archive index. |
| RORG-032 | Add task validator | done | work / TBD | Added `tools/agents/validate_tasks.py` with warning/strict modes and required-section checks. |
| RORG-033 | Replace TODO policy script | done | work / TBD | Added `tools/agents/check_task_policy.py`; kept `tools/agents/check_todo_active_only.sh` as the canonical wrapper with `tools/check_todo_active_only.sh` retained as a compatibility entrypoint; CI now calls the new checker directly. |
| RORG-040 | Create `methods/` root and package template | done | work / TBD | Added `methods/` skeleton, template package, and pipeline documentation in `methods/README.md`. |
| RORG-041 | Add method manifest schema | done | work / TBD | Added `docs/methods/method-manifest-schema.md` and `tools/agents/validate_method_manifests.py` with strict mode and path checks. |
| RORG-042 | Create method docs package | done | work / TBD | Added `docs/methods/` package and linked it from method workflow/docs index. |
| RORG-043 | Add canonical method API concept doc | done | work / TBD | Added `docs/architecture/method-api-contract.md` and linked from architecture index. |
| RORG-044 | Add first sample method package without real algorithm code | done | work / TBD | Added `methods/geometry/_example_vector_heat/` scaffold package, manifest, and placeholder tests/benchmarks docs. |
| RORG-050 | Create benchmark directory structure | done | work / TBD | Added `benchmarks/` scaffold, CMake integration, and category READMEs. |
| RORG-051 | Add benchmark manifest schema and validator | done | work / TBD | Added `docs/benchmarking/benchmark-manifest-schema.md` and `tools/benchmark/validate_benchmark_manifests.py` with strict mode, duplicate-ID checks, and metric validation. |
| RORG-052 | Add benchmark output JSON schema | done | work / TBD | Added `docs/benchmarking/result-json-schema.md`, `tools/benchmark/validate_benchmark_results.py`, and a validated example result payload. |
| RORG-053 | Add benchmark docs package | done | work / TBD | Added `docs/benchmarking/` docs package (`index`, `overview`, `dataset-policy`, `metrics`, `baselines`, `ci-policy`, `report-template`) and linked from docs index/workflow docs. |
| RORG-054 | Add minimal benchmark smoke runner skeleton | done | work / TBD | Added `IntrinsicBenchmarkSmoke`, smoke dataset/baseline manifests, and JSON-emitting runner skeleton (CPU-only). |
| RORG-060 | Create new tests directory skeleton | done | work / TBD | Added `tests/{unit,contract,integration,regression,gpu,benchmark,support}` skeleton with subsystem READMEs; existing build/test wiring left unchanged. |
| RORG-061 | Add test classification policy | done | work / TBD | Added `docs/architecture/test-strategy.md` with required categories, naming convention, and CTest label mapping policy. |
| RORG-062 | Add CMake support for test subdirectories and labels | done | work / TBD | Added migration-aware source resolution in `tests/CMakeLists.txt` that prefers new categorized paths and falls back to legacy root paths; added CTest labels (`unit`, subsystem labels) to test executables. |
| RORG-063 | Move Core unit tests | done | work / TBD | Moved root core unit tests into `tests/unit/core/` with `git mv`; `tests/CMakeLists.txt` migration resolver already preferred categorized paths, so build wiring stayed stable and labels remain `unit,core`. |
| RORG-064 | Move Geometry unit tests | done | work / TBD | Moved targeted geometry unit tests from `tests/` root to `tests/unit/geometry/` via `git mv`; migration-aware resolver in `tests/CMakeLists.txt` preserved build wiring and `unit,geometry` labeling. |
| RORG-065 | Move Asset and ECS unit tests | done | work / TBD | Moved ECS unit tests (`Test_RuntimeECS.cpp`, `Test_EntityCommands.cpp`) to `tests/unit/ecs/` and asset-core boundary tests (`Test_CoreAssets.cpp`, `Test_CoreAssetSafety.cpp`) to `tests/unit/assets/`; updated `tests/CMakeLists.txt` test object wiring and labels. |
| RORG-066 | Move Graphics tests | done | work / TBD | Moved selected graphics/runtime rendering tests into `tests/unit/graphics`, `tests/integration/graphics`, and `tests/contract/graphics`; updated migration-aware test source resolution + labels. |
| RORG-067 | Move Runtime and app integration tests | not-started | - |  |
| RORG-068 | Move regression and benchmark tests | not-started | - |  |
| RORG-069 | Delete old root test clutter | not-started | - |  |
| RORG-070 | Create target tools subdirectories | done | work / TBD | Added `tools/{repo,docs,ci,benchmark,agents,analysis}/README.md` with current ownership and planned migration map. |
| RORG-071 | Move repo/policy tools | done | work / TBD | Moved repo/policy scripts to canonical owning paths (`tools/repo/*`, `tools/agents/*`) and retained root-path wrappers for one release cycle compatibility. |
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
