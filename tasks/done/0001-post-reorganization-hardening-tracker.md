# HARDEN-001 — Post-Reorganization Hardening Tracker

## Goal

Track the post-RORG hardening phase that makes the reorganized repository clean, reliable, and ready for agent-driven scientific method implementation.

## Non-goals

- Do not reduce, migrate, or retire `src/legacy/` in this phase.
- Do not remove `src/legacy/`.
- Do not perform broad semantic rendering/runtime refactors unless they are required to fix a tracked test failure.
- Do not mix mechanical file moves with semantic behavior changes.

## Context

This tracker follows completion of the RORG source-layout reorganization. It covers test triage, supported test-label policy, Codex verification hardening, active `src_new` naming cleanup, final test taxonomy cleanup, layering allowlist tightening, and CI/documentation synchronization.

Legacy retirement is explicitly deferred. Any temporary legacy exception must remain tracked separately and must not become a broad migration-retirement effort under this tracker.

- **Status:** done; post-reorganization hardening phase closed and archived on 2026-04-30
- **Owner/agent:** repository maintainers / hardening agents
- **Branch:** `work`
- **PR:** TBD
- **Commit reference:** pending local review/commit
- **Next verification step:** no active post-reorganization hardening blocker remains; keep the default CPU-supported gate and opt-in GPU/runtime gate green for touched areas.
- **Closure evidence:** all `HARDEN-*` rows are `done`; final acceptance checklist is complete; `IntrinsicTests` builds; CPU-supported CTest and GPU/Vulkan/runtime opt-in CTest gates passed on 2026-04-30; strict task/docs and structural validators passed during final cleanup.
- **Status values:** `not-started`, `in-progress`, `blocked`, `done`, `deferred`

## Required changes

- Create this active tracker under `tasks/active/`.
- Record the full post-reorganization hardening scope and explicit non-goals.
- Track every `HARDEN-*` task with a status board and owner task ID.
- Record current full-test status, runtime/GPU failure list, temporary skips/quarantines, temporary naming aliases, and final acceptance checklist.
- Link this tracker from `tasks/README.md` or `docs/migration/active-status.md`.

## Tests

- Structural task policy validation must pass in strict mode.
- Documentation links must pass in strict mode after the tracker is linked.

## Docs

- `tasks/README.md` linked this tracker while the post-RORG hardening phase was active; after closure, it lists no active trackers and this record is archived under `tasks/done/`.
- Follow-up reports and docs created by later hardening tasks must link back to this tracker when they change active hardening status.

## Acceptance criteria

- [x] Tracker existed under `tasks/active/` while active and is archived at `tasks/done/0001-post-reorganization-hardening-tracker.md` after closure.
- [x] Tracker was linked from `tasks/README.md` while active; `tasks/README.md` now records that no active trackers remain.
- [x] Tracker explicitly states that legacy retirement is deferred.
- [x] Tracker contains no vague `fix later` entries; every known issue maps to a `HARDEN-*` task ID.
- [x] Current full-test status is recorded.
- [x] Runtime/GPU failure list is recorded.
- [x] Temporary skips/quarantines and naming aliases are recorded.
- [x] Final acceptance checklist is recorded.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No legacy retirement or `src/legacy/` shrink work.
- No source migration from `src/legacy/` into promoted layers.
- No test deletion to hide runtime/GPU failures.
- No broad or silent skips for deterministic logic failures.
- No undocumented temporary exceptions.

## Scope statement

Included in this phase:

1. Full `ctest` / runtime / GPU test triage.
2. Layering allowlist tightening, excluding broad legacy retirement.
3. Stronger Codex verification.
4. Remaining active `src_new` naming cleanup, especially shader asset paths.
5. Final test taxonomy cleanup.
6. Final CI consistency and documentation sync.

Excluded from this phase:

- Reducing the size of `src/legacy/`.
- Migrating legacy modules into promoted layers.
- Removing `src/legacy/`.
- Large semantic rendering/runtime refactors unless required to fix tracked tests.

## Status board

| ID | Title | Status | Owner / area | Evidence / next step |
|---|---|---|---|---|
| HARDEN-001 | Create post-reorganization hardening tracker | done | tasks/docs | Tracker created and linked from `tasks/README.md`; strict task/doc validation passed on 2026-04-29. |
| HARDEN-002 | Capture full CTest failure baseline | done | tests/reports | Baseline recorded in [`docs/reports/full-ctest-baseline-2026-04-29.md`](../../docs/reports/full-ctest-baseline-2026-04-29.md); exact configure blocked by missing `clang-20`, adjusted Clang baseline captured deterministic failures. |
| HARDEN-003 | Fix or quarantine VMA unmap assertion tests | done | graphics/vulkan/gpu tests | Fixed legacy `VulkanBuffer` mapped-allocation ownership and added a focused regression test; VMA assertion group passes. |
| HARDEN-004 | Fix runtime selection GPU sub-element expectation failures | done | runtime/graphics/ecs | Explicit GPU primitive hints are honored before whole-mesh CPU fallback; selection contract documented in [`docs/architecture/rendering-three-pass.md`](../../docs/architecture/rendering-three-pass.md). |
| HARDEN-005 | Split full CTest into explicit supported labels | done | tests/CI/docs | Label taxonomy is active; `unit|contract` and CPU-supported gates pass, GPU/Vulkan labels are opt-in, and obsolete wrapper `_NOT_BUILT` placeholders are removed from active CTest registration. |
| HARDEN-006 | Make default full CPU-supported CTest green | done | tests/CI/docs | Canonical CPU-supported gate is documented in `README.md`, `AGENTS.md`, `.codex/config.yaml`, and test strategy docs; CI full CPU workflows run the same CTest exclusion policy. |
| HARDEN-010 | Replace broad legacy layering allowlist with path-specific exceptions | done | tools/repo/docs | `tools/repo/layering_allowlist.yaml` now uses concrete legacy subtree globs per layer edge; no `src/legacy/**` wildcard remains. |
| HARDEN-011 | Add layering allowlist quality checker | done | tools/repo/CI | Added `tools/repo/check_layering_allowlist_quality.py` strict checker, wired into `ci-docs.yml`, and documented in `tools/repo/README.md` on 2026-04-29. |
| HARDEN-020 | Fix `.codex/config.yaml` verification command | done | agents/docs | `.codex/config.yaml` now configures `ci`, builds `IntrinsicTests`, and runs the canonical CPU-supported CTest gate; `AGENTS.md` and `docs/agent/contract.md` document the rule. |
| HARDEN-021 | Add Codex config validator | done | tools/agents/CI | `tools/agents/check_codex_config.py` validates C++23, `AGENTS.md`, real `IntrinsicTests` build target, CTest execution, and policy-light `.codex/config.yaml`; wired into `ci-docs.yml`. |
| HARDEN-030 | Classify all remaining `src_new` references | done | docs/migration | Audit created at [`docs/migration/src-new-reference-audit.md`](../../docs/migration/src-new-reference-audit.md); active-stale references are assigned to HARDEN-031, HARDEN-032, HARDEN-033, and HARDEN-041. |
| HARDEN-031 | Rename active shader asset `src_new` path | done | assets/graphics | Moved shader subdirectories to `assets/shaders/{common,culling,deferred,forward}/`; updated includes, test paths, and source comment without shader semantic edits. |
| HARDEN-032 | Rename stale active task/doc names containing `src-new` | done | docs/tasks | Updated active source-layer README terminology and canonical docs/task navigation links; strict task/doc checks passed on 2026-04-29. |
| HARDEN-033 | Add stale `src_new` reference checker | done | tools/repo/CI | Added `tools/repo/check_stale_src_new_references.py` with explicit allowlist and wired strict enforcement in `ci-docs.yml`. |
| HARDEN-040 | Audit remaining non-taxonomic test directories | done | tests/docs | Audit recorded in [`docs/reports/test-taxonomy-audit-2026-04-29.md`](../../docs/reports/test-taxonomy-audit-2026-04-29.md); wrapper directories inventoried and mapped to HARDEN-041/HARDEN-042. |
| HARDEN-041 | Move remaining test sources into taxonomy directories | done | tests/CMake | HARDEN-041C corrected relocated suite link targets to promoted `Extrinsic*` owners (`assets/core/rhi/graphics/runtime`) and removed stale runtime-integration gate/links to legacy targets; HARDEN-052 supplied the post-relocation build/CPU-gate proof. |
| HARDEN-042 | Remove or formalize old subsystem test subdirectories | done | tests/docs | Removed obsolete wrapper `CMakeLists.txt` stubs and relocated shared `MockRHI.hpp` into `tests/support/`; strict task/docs checks passed on 2026-04-29. |
| HARDEN-043 | Add strict test layout checker | done | tools/repo/CI | Added `tools/repo/check_test_layout.py`, wired strict CI docs validation, and documented usage in `tools/repo/README.md` on 2026-04-29. |
| HARDEN-050 | Align CI workflows with final supported test policy | done | CI/docs | `pr-fast.yml` unit/contract gate includes `--timeout 60`; HARDEN-050B follow-up updated `ci-sanitizers.yml` to run `-L "unit|contract|integration"` with canonical `-LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60`. |
| HARDEN-051 | Execute final post-reorganization hardening audit | done | tasks/audit | Final audit artifact re-executed on 2026-04-30; source-root stale-reference, offline configure, build, default CPU gate, and validators are recorded. The remaining GPU/runtime opt-in failures are split to HARDEN-053. |
| HARDEN-052 | Bootstrap offline dependency cache for CI preset | done | build/deps/docs | Bootstrap procedure documented; cache prime + offline `cmake --preset ci --fresh -DINTRINSIC_OFFLINE_DEPS=ON` passed locally with explicit compiler/scanner overrides, `IntrinsicTests` built, and the CPU-supported CTest gate passed with 1432 selected tests. |
| HARDEN-053 | Triage GPU/runtime opt-in gate failures | done | runtime/graphics/gpu tests | Resolved on 2026-04-30: `IntrinsicTests` builds, focused failure clusters pass, GPU/Vulkan/runtime opt-in gate passes with 1218 selected tests, and the default CPU-supported gate passes with 1432 selected tests. |

## Current full-test status

Known status as of 2026-04-29, carried forward from `tasks/done/final-reorganization-audit.md`:

- `ctest --test-dir build/ci --output-on-failure` was attempted during the final RORG audit and interrupted after 300 seconds.
- The run reached GPU/headless Vulkan runtime coverage and exposed failures outside the structural reorganization gate.
- HARDEN-002 captured the focused deterministic failure baseline in [`docs/reports/full-ctest-baseline-2026-04-29.md`](../../docs/reports/full-ctest-baseline-2026-04-29.md).
- HARDEN-005 established the supported CPU-default CTest gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed with 1240 tests on 2026-04-29.
- HARDEN-003 fixed the focused VMA unmap assertion group without deleting or quarantining tests.
- HARDEN-004 fixed the two deterministic runtime selection sub-element expectation failures and documented the selection ID contract.
- HARDEN-006 made the default local/CI CPU-supported test gate canonical across docs/config/workflows and verified it locally with explicit available Clang paths.
- HARDEN-053 resolved the final GPU/runtime opt-in blocker on 2026-04-30: `ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan|runtime' -LE 'slow|flaky-quarantine' --timeout 60` passed with 1218 selected tests, 0 failures, in 249.03 seconds.
- The default CPU-supported gate remained green after HARDEN-053: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed with 1432 selected tests, 0 failures, in 95.74 seconds; the two SLO/benchmark tests were skipped by test-internal conditions.
- Exact `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` is blocked on this machine because `clang-20`/`clang++-20` are not on `PATH`; the report used explicit `/usr/bin/clang` and `/usr/bin/clang++` overrides to build and test.
- Exact full single-threaded `ctest --test-dir build/ci --output-on-failure --timeout 60` reproduced known failures but was interrupted after 300 seconds; focused failing groups were repeated twice.

## Runtime/GPU failure list

| Failure group | Known tests / symptoms | Failure type | Determinism | Environment | Owner task |
|---|---|---|---|---|---|
| VMA unmap assertion | `TransferTest.*`, `GraphicsBackendHeadlessTest.*`, `AssetPipelineHeadlessTest.*` | assertion / subprocess abort in VMA unmap | deterministic across two focused runs | Vulkan-capable headless device; no window required in focused runs | Resolved by HARDEN-003. |
| Runtime selection sub-element expectations | `RuntimeSelection.ResolveGpuSubElementPick_MeshSurfacePrimitiveDoesNotFallbackToWholeMeshRaycast`, `RuntimeSelection.ResolveGpuSubElementPick_MeshLinePrimitiveRefinesNearestEndpointVertex` | deterministic logic expectation failure | deterministic across two focused runs | CPU-side selection resolution tests; no direct Vulkan requirement observed | Resolved by HARDEN-004. |
| Generated missing test executables | `ExtrinsicAssetTests_NOT_BUILT`, `ExtrinsicCoreTests_NOT_BUILT`, `ExtrinsicECSTests_NOT_BUILT`, `ExtrinsicGraphicsTests_NOT_BUILT`, `ExtrinsicRuntimeTests_NOT_BUILT` | CTest `Not Run` / missing executable | deterministic | no GPU requirement | Resolved by HARDEN-005 active-suite registration and HARDEN-041 taxonomy source relocation. |
| Label selection gaps | `ctest -L gpu`, `ctest -L runtime`, `ctest -L graphics` select no tests | taxonomy/CTest label wiring issue | directly observed | no GPU requirement for the label check itself | Resolved by HARDEN-005 label fixups. |
| Final GPU/runtime opt-in failures | 38 failures from graph/BDA data contracts, geometry reuse, dirty sync, panel registration, render extraction, render graph packetization, maintenance lane GPU tests, runtime layering contracts, and null renderer debug dump | deterministic test/production contract drift and production defects | deterministic in focused reruns | Vulkan-capable headless device for GPU groups; pure CPU for contract groups | Resolved by HARDEN-053. |

## Temporary test skips and quarantines

| Test or label | Reason | Introduced by | Removal condition | Owner task | Status |
|---|---|---|---|---|---|
| Old subsystem wrapper CTest registration: `tests/Asset/`, `tests/Core/`, `tests/ECS/`, `tests/Graphics/`, `tests/Runtime/` | Wrapper source relocation completed in HARDEN-041 and obsolete wrapper target stubs were removed in HARDEN-042; taxonomy-owned targets remain the only active CTest registration path. | HARDEN-005 | Completed by HARDEN-041 + HARDEN-042. | HARDEN-041, HARDEN-042 | resolved |

Any future `flaky-quarantine` or skip must be capability-based or tied to a deterministic task-specific removal condition. Broad silent skips are prohibited.

## Temporary naming aliases

| Alias / stale name | Current use | Introduced by | Removal condition | Owner task | Status |
|---|---|---|---|---|---|
| `src_new` references | Classified as active-stale, migration-ok, or historical-ok in [`docs/migration/src-new-reference-audit.md`](../../docs/migration/src-new-reference-audit.md). Active-stale references are cleaned up; historical/migration records remain allowlisted. | RORG migration history | Maintain strict stale-reference checker and explicit allowlist. | HARDEN-031, HARDEN-032, HARDEN-033, HARDEN-041 | resolved/allowlisted |
| Shader asset `src_new` root | Renamed to final shader-root subdirectories; active include/path leaks removed. | RORG migration history | None for shader path; stale docs/tasks continue under HARDEN-032/HARDEN-033. | HARDEN-031 | resolved |

## Final acceptance checklist

- [x] Default local/CI CPU-supported test gate is green.
- [x] GPU/runtime tests are fixed or capability-gated with explicit skip reasons.
- [x] Codex verification builds meaningful targets and runs tests.
- [x] Active `src_new` naming is gone or allowlisted as historical/migration-only.
- [x] Test taxonomy is strict and checked.
- [x] Layering allowlist is no longer migration-wide except where explicitly justified.
- [x] CI workflows and docs agree on developer and agent verification commands.
- [x] Docs links pass strict mode.
- [x] Task policy passes strict mode.
- [x] Method and benchmark validators pass strict mode.
- [x] `tasks/done/final-post-reorganization-hardening-audit.md` records final evidence for HARDEN-051.

## Evidence log

| Date | Task | Command | Result |
|---|---|---|---|
| 2026-04-29 | HARDEN-001 | `python3 tools/agents/check_task_policy.py --root . --strict` | Passed; 8 task files validated, 0 findings. |
| 2026-04-29 | HARDEN-001 | `python3 tools/docs/check_doc_links.py --root . --strict` | Passed; 104 relative links checked, no broken links. |
| 2026-04-29 | HARDEN-002 | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` | Failed locally: preset compiler names `clang-20`/`clang++-20` are not on `PATH`. |
| 2026-04-29 | HARDEN-002 | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++` | Passed; CMake reported Clang 22.0.0. |
| 2026-04-29 | HARDEN-002 | `cmake --build --preset ci --target IntrinsicTests` | Passed. |
| 2026-04-29 | HARDEN-002 | `ctest --test-dir build/ci --output-on-failure -R 'TransferTest|GraphicsBackendHeadlessTest|AssetPipelineHeadlessTest|RuntimeSelection.ResolveGpuSubElementPick' --timeout 60` | Repeated twice; 23 deterministic failures out of 34 selected tests. |
| 2026-04-29 | HARDEN-002 | `ctest --test-dir build/ci --output-on-failure -R 'NOT_BUILT|Extrinsic' --timeout 60` | 5 deterministic missing-executable failures. |
| 2026-04-29 | HARDEN-005 | `cmake --build --preset ci --target IntrinsicTests` | Passed; no work to do after label wiring changes. |
| 2026-04-29 | HARDEN-005 | `ctest --test-dir build/ci -N -R 'Extrinsic\|NOT_BUILT'` | Passed; selected 0 obsolete wrapper placeholder tests. |
| 2026-04-29 | HARDEN-005 | `ctest --test-dir build/ci -N -L <label>` for `unit`, `contract`, `integration`, `runtime`, `headless`, `graphics`, `gpu`, `vulkan`, `benchmark`, `slo`, `slow`, `flaky-quarantine` | Labels are independently selectable: `unit` 1213, `contract` 15, `integration`/`runtime`/`headless`/`graphics`/`gpu`/`vulkan` 1029 each, `benchmark`/`slo` 12 each, `slow`/`flaky-quarantine` 0. |
| 2026-04-29 | HARDEN-005 | `ctest --test-dir build/ci --output-on-failure -L 'unit\|contract' --timeout 60` | Passed; 1228 tests, 0 failures. |
| 2026-04-29 | HARDEN-005 | `ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60` | Passed; 1240 tests, 0 failures, 2 benchmark/SLO tests skipped by test-internal conditions. |
| 2026-04-29 | HARDEN-003 | `ctest --test-dir build/ci --output-on-failure -R 'TransferTest\|GraphicsBackendHeadlessTest\|AssetPipelineHeadlessTest' --timeout 60` | Passed; 22 tests, 0 failures, including `TransferTest.HostVisibleBufferUnmapIsSafeForPersistentVmaMapping`. |
| 2026-04-29 | HARDEN-004 | `ctest --test-dir build/ci --output-on-failure -R 'RuntimeSelection.ResolveGpuSubElementPick' --timeout 60` | Passed; 13 tests, 0 failures. |
| 2026-04-29 | HARDEN-006 | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` | Still fails locally because preset compiler names `clang-20`/`clang++-20` are not on `PATH`; CI image installs those names. |
| 2026-04-29 | HARDEN-006 | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ && cmake --build --preset ci --target IntrinsicTests && ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60` | Passed; Clang 22.0.0 local override, 1240 tests, 0 failures, 2 benchmark/SLO tests skipped by test-internal conditions. |
| 2026-04-29 | HARDEN-020 | `.codex/config.yaml` `workflow.verification_command` inspection | Uses `cmake --preset ci`, builds `IntrinsicTests`, and runs `ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60`; no `--target help` verification remains. |
| 2026-04-29 | HARDEN-021 | `python3 -m py_compile tools/agents/check_codex_config.py && python3 tools/agents/check_codex_config.py --root . --strict` | Passed; Codex config check complete with 0 findings. |
| 2026-04-29 | HARDEN-030 | `git grep -n "src_new\|src-new\|src new" -- .` | Classified in [`docs/migration/src-new-reference-audit.md`](../../docs/migration/src-new-reference-audit.md); no rename/cleanup performed in this audit task. |
| 2026-04-29 | HARDEN-031 | `git mv assets/shaders/src_new/{common,culling,deferred,forward} assets/shaders/{common,culling,deferred,forward}` plus include/path updates | Completed; no shader semantic edits. |
| 2026-04-29 | HARDEN-031 | `git grep -n "assets/shaders/src_new" -- . && exit 1 || true`; `git grep -n "shaders/src_new\|src_new/common\|SRC_NEW" -- assets src tests cmake && exit 1 || true` | Passed; no active stale shader asset path/include guard references remain. |
| 2026-04-29 | HARDEN-031 | `glslc` compile/preprocess check for moved shaders with `-I assets/shaders --target-env=vulkan1.3` | Include resolution passed; all moved shaders compiled except `deferred/lighting.frag`, which preprocesses successfully but has an unrelated pre-existing GLSL `const` reference-type compile diagnostic. |
| 2026-04-29 | HARDEN-031 | `ctest --test-dir build/ci --output-on-failure -R 'Shader\|RenderPass\|Graphics' --timeout 60` | Passed; 67 tests, 0 failures. |
| 2026-04-29 | HARDEN-032 | `python3 tools/agents/check_task_policy.py --root . --strict` | Passed; strict task schema validation including new active task file. |
| 2026-04-29 | HARDEN-032 | `python3 tools/docs/check_doc_links.py --root . --strict` | Passed; docs/task link integrity remained green after stale-name updates. |
| 2026-04-29 | HARDEN-032 | `rg -n "src_new|src-new|src new" src/app/README.md src/core/README.md src/ecs/README.md src/graphics/renderer/README.md src/platform/README.md src/runtime/README.md docs/index.md docs/architecture/patterns.md docs/architecture/task-graphs.md docs/roadmap.md tasks/backlog/README.md tasks/backlog/legacy-todo.md tasks/backlog/rendering/RORG-031-rendering-pipeline.md` | Passed; active-stale references removed, with only explicit historical migration-inventory mentions retained. |
| 2026-04-29 | HARDEN-033 | `python3 tools/repo/check_stale_src_new_references.py --root . --strict` | Passed; stale references now blocked outside explicit migration/historical allowlist entries. |
| 2026-04-29 | HARDEN-033 | `python3 tools/repo/check_stale_src_new_references.py --root . --strict` after CI follow-up allowlist update for `.github/workflows/ci-docs.yml` | Passed; workflow self-reference no longer causes strict-mode false positives. |
| 2026-04-29 | HARDEN-010 | `python3 tools/repo/check_layering.py --root src --strict` | Passed; strict layering remains green after replacing broad `src/legacy/**` with path-scoped legacy subtree exceptions. |
| 2026-04-29 | HARDEN-010 | `python3 tools/agents/check_task_policy.py --root . --strict && python3 tools/docs/check_doc_links.py --root . --strict` | Passed; task/docs validation remained green after HARDEN-010 task/docs updates. |
| 2026-04-29 | HARDEN-011 | `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict` | Passed; 81 entries checked, 0 findings. |
| 2026-04-29 | HARDEN-011 | `python3 tools/agents/check_task_policy.py --root . --strict && python3 tools/docs/check_doc_links.py --root . --strict` | Passed; task schema and docs links remained strict-green after HARDEN-011 updates. |
| 2026-04-29 | HARDEN-040 | `find tests -maxdepth 3 -type d | sort`; `find tests/Asset tests/Core tests/ECS tests/Graphics tests/Runtime -type f -name '*.cpp' | wc -l`; `for d in tests/Asset tests/Core tests/ECS tests/Graphics tests/Runtime; do echo "$(find "$d" -type f -name '*.cpp' | wc -l) $d"; done` | Audit evidence captured in `docs/reports/test-taxonomy-audit-2026-04-29.md`; 37 wrapper `*.cpp` files inventoried with follow-up mapping. |
| 2026-04-29 | HARDEN-041 | `task file creation` | Created `tasks/done/HARDEN-041-test-taxonomy-source-moves.md` with scoped mechanical-move plan and verification commands. |
| 2026-04-29 | HARDEN-041 | `cat docs/reports/test-taxonomy-audit-2026-04-29.md`; `sed -n '1,520p' tests/CMakeLists.txt` | Confirmed wrapper inventory exists but per-file destination mapping is not yet documented; HARDEN-041 task updated with explicit next-step gating to keep moves mechanical-only. |
| 2026-04-29 | HARDEN-041 | `find tests/Asset tests/Core tests/ECS tests/Graphics tests/Runtime -type f -name '*.cpp' | sort`; update `tasks/done/HARDEN-041-test-taxonomy-source-moves.md` | Completed inventory-backed 37-file source→destination move table to gate the upcoming mechanical-only relocation patch. |
| 2026-04-29 | HARDEN-041 | `git mv ...` (37 wrapper sources from `tests/{Asset,Core,ECS,Graphics,Runtime}` into `tests/{unit,contract,integration}/...`) | Completed pure mechanical relocation patch with no test-source semantic edits. |
| 2026-04-29 | HARDEN-041 | `cmake --build --preset ci --target IntrinsicTests` | Failed before configure in this environment because `build/ci` was absent. |
| 2026-04-29 | HARDEN-041 | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=$(which clang) -DCMAKE_CXX_COMPILER=$(which clang++)` | Failed: offline dependency cache missing (`external/cache/glm-src`), so build/CTest gates could not be executed locally. |
| 2026-04-29 | HARDEN-041 | `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed after relocation patch; task/docs strict checks remain green. |
| 2026-04-29 | HARDEN-041 | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` | Failed in this environment: offline dependency cache missing (`external/cache/glm-src`), so configure cannot complete to prove build/CTest gates. |
| 2026-04-29 | HARDEN-041 | `cmake --build --preset ci --target IntrinsicTests` | Failed because `build/ci` was not generated after configure failure (`build.ninja` missing). |
| 2026-04-29 | HARDEN-041 | `ctest --test-dir build/ci --output-on-failure -L 'unit\|contract' --timeout 60`; `ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60` | `ctest` executed but found no tests because configure/build did not complete in this offline-cache-limited environment. |
| 2026-04-29 | HARDEN-041 | `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed after relocation-status sync; strict task policy validated 14 task files with 0 findings and strict doc-link check validated 105 links with 0 broken links. |
| 2026-04-29 | HARDEN-042 | `git rm tests/Asset/CMakeLists.txt tests/Core/CMakeLists.txt tests/ECS/CMakeLists.txt tests/Graphics/CMakeLists.txt tests/Runtime/CMakeLists.txt && git mv tests/Graphics/MockRHI.hpp tests/support/MockRHI.hpp` | Passed; obsolete wrapper CMake stubs removed and shared graphics test helper moved to taxonomy support include path. |
| 2026-04-29 | HARDEN-042 | `rg -n "ExtrinsicAssetTests|ExtrinsicCoreTests|ExtrinsicECSTests|ExtrinsicGraphicsTests|ExtrinsicRuntimeTests" tests`; `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed; no remaining wrapper-target registration references under `tests`, strict task/doc checks green. |
| 2026-04-29 | HARDEN-043 | `python3 tools/repo/check_test_layout.py --root . --strict`; `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed; strict test taxonomy layout enforcement is active and docs/task strict checks remained green. |
| 2026-04-29 | HARDEN-050 | `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed after adding `--timeout 60` to `.github/workflows/pr-fast.yml` unit/contract CTest step and syncing HARDEN-050 task/tracker docs. |
| 2026-04-29 | HARDEN-050B | `.github/workflows/ci-sanitizers.yml` inspection/update; `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed after updating sanitizer CTest selection to `-L "unit\|contract\|integration" -LE "gpu\|vulkan\|slow\|flaky-quarantine" --timeout 60` and syncing HARDEN-050 task + tracker evidence. |
| 2026-04-29 | HARDEN-051 | `task file creation` | Created `tasks/done/HARDEN-051-final-hardening-audit-task.md` to scope final hardening audit closure evidence and keep execution as a follow-up task step. |

| 2026-04-30 | HARDEN-051B | `python3 tools/repo/check_stale_src_new_references.py --root . --strict`; `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON`; validator strict checks | Re-executed final hardening audit with command evidence in `tasks/done/final-post-reorganization-hardening-audit.md`; stale-reference checker is green (`findings=0`). Initial offline-cache blocker was split to and resolved by HARDEN-052. |

| 2026-04-30 | HARDEN-052 | `cmake --preset ci -DCMAKE_C_COMPILER=/root/.swiftly/bin/clang -DCMAKE_CXX_COMPILER=/root/.swiftly/bin/clang++`; `cmake --preset ci --fresh -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/root/.swiftly/bin/clang -DCMAKE_CXX_COMPILER=/root/.swiftly/bin/clang++` | Passed; online prime populated `external/cache/*-src`, then offline configure succeeded from a fresh preset state. |
| 2026-04-30 | HARDEN-052 | `cmake --preset ci --fresh -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps`; `cmake --build --preset ci --target IntrinsicTests`; `ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60` | Passed after test object libraries declared their direct C++ module/header dependencies; `IntrinsicTests` linked successfully and CPU-supported CTest reported `100% tests passed, 0 tests failed out of 1432` in 96.09 sec, with 2 benchmark/SLO tests skipped by test-internal conditions. |
| 2026-04-30 | HARDEN-041 | `cmake --build --preset ci --target IntrinsicTests`; `ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60` | Passed using the HARDEN-052 rebuilt `build/ci`; this closes the post-relocation CPU-supported gate evidence for HARDEN-041. |
| 2026-04-30 | HARDEN-051/HARDEN-053 | `ctest --test-dir build/ci --output-on-failure -L 'gpu\|vulkan\|runtime' -LE 'slow\|flaky-quarantine' --timeout 60` | Failed; `97% tests passed, 38 tests failed out of 1218` in 249.43 sec. HARDEN-051 audit execution is complete; deterministic triage/capability-gating is tracked by HARDEN-053. |
| 2026-04-30 | HARDEN-053 | `cmake --build --preset ci --target IntrinsicTests` | Passed; `IntrinsicTests` linked successfully after HARDEN-053 fixes. |
| 2026-04-30 | HARDEN-053 | `ctest --test-dir build/ci --output-on-failure -R 'Graph_Data\|BDA_GraphData\|BDA_PerEdgeAttr\|PropertySetDirtySync\|PanelRegistration' --timeout 60` | Passed; 54 tests, 0 failures. |
| 2026-04-30 | HARDEN-053 | `ctest --test-dir build/ci --output-on-failure -R 'GeometryReuseTest\|RenderExtraction\.FrameContext_DeferredDeletions_(DropNeverSubmittedSlotOnReuse\|SurviveSlotReuse)\|RenderGraphPacketTest\.(TwoRasterPasses_SameAttachments_Merge\|ThreeRasterPasses_SameAttachments_MergeAll\|TwoRasterPasses_ColorOnly_SameTarget_Merge\|TwoRasterPasses_SameAttachments_DifferentClearValues_NoMerge)\|MaintenanceLaneGpuTest\|RuntimeEngineLayering\|GraphicsRenderer\.NullRendererDebugDumpContainsCanonicalPassesAndDataflowOrder' --timeout 60` | Passed; 16 tests, 0 failures. |
| 2026-04-30 | HARDEN-053 | `ctest --test-dir build/ci --output-on-failure -L 'gpu\|vulkan\|runtime' -LE 'slow\|flaky-quarantine' --timeout 60` | Passed; 1218 tests, 0 failures, total real time 249.03 sec. |
| 2026-04-30 | HARDEN-053 | `ctest --test-dir build/ci --output-on-failure -LE 'gpu\|vulkan\|slow\|flaky-quarantine' --timeout 60` | Passed; 1432 tests, 0 failures, total real time 95.74 sec; `ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes` and `ArchitectureSLO.TaskSchedulerContentionAndWakeLatencyBudgets` skipped by test-internal conditions. |
| 2026-04-30 | HARDEN-053 | `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed after HARDEN-053 archival/tracker updates. |
| 2026-04-30 | task cleanup | `python3 tools/repo/check_layering.py --root src --strict`; `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict`; `python3 tools/repo/check_test_layout.py --root . --strict`; `python3 tools/repo/check_stale_src_new_references.py --root . --strict`; `python3 tools/agents/check_codex_config.py --root . --strict`; `python3 tools/agents/validate_method_manifests.py --root methods --strict`; `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict`; `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict` | Passed after moving completed task records to `tasks/done`; task policy validated 21 task files with 0 findings and doc links checked 105 relative links with no broken links. |
