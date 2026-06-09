# HARDEN-081 — ECS legacy component compatibility decisions

## Status
- Status: done.
- Completed: 2026-06-09.
- Owner/agent: Codex.
- Branch: `main`.
- Final implementation commit: this retirement commit.
- Maturity: `Retired` for legacy-only demo/compatibility behavior; `CPUContracted` for the retained promoted naming contract.
- Summary: Resolved the remaining legacy ECS compatibility candidates without adding new ECS modules. Legacy `NameTag` maps to the existing `Extrinsic.ECS.Component.MetaData::EntityName` bootstrap naming contract; `AxisRotator` is retired as demo behavior unless a future runtime/app sample accepts it; DEC operator caches remain geometry/method/runtime sidecars instead of canonical ECS components; and feature-token/catalog behavior stays outside ECS under the already-retired `CORE-002`/runtime-owned scheduling decisions.

## Goal
- Resolve remaining legacy ECS component/system compatibility candidates by preferring promoted replacements, reassigned owners, or retirement over compatibility clutter.

## Non-goals
- No live runtime, graphics, physics, platform, app, or asset-service ownership in `src/ecs`.
- No generic ECS command buffer; runtime/editor command policy is owned by `RUNTIME-102`.
- No legacy component-name compatibility wrappers unless a consumer-grep gate proves they are necessary and a removal task is filed.

## Context
- Owner/layer: `ecs -> core`; geometry handles/types only when explicitly required by existing ECS component policy.
- `HARDEN-060..068` promoted scene, hierarchy, transform, events, geometry sources, render sync, bounds, physics authoring, and stable ID value types.
- Inventory evidence: `src/legacy/ECS/` contains the legacy candidates `ECS:Components.NameTag`, `ECS:Components.AxisRotator`, `ECS:Systems.AxisRotator`, `ECS:Components.DEC`, and feature-token-adjacent transform system registration. `git grep -nE '^\s*(export\s+)?import\s+ECS\b' -- 'src/**' 'tests/**' ':!src/legacy/ECS/**'` still reports 62 external legacy `ECS` imports, all in legacy graphics/runtime subtrees or legacy compatibility tests; those consumer migrations remain owned by `LEGACY-012` and subtree deletion tasks, not by new canonical ECS compatibility wrappers.
- Promoted replacement evidence: default ECS bootstrap already creates `MetaData::EntityName`; transform hierarchy/bounds/render-sync system passes already use explicit FrameGraph pass names and runtime activation through `Extrinsic.Runtime.EcsSystemBundle`; geometry DEC operators already live under `Geometry.DEC` and method/runtime consumers can hold sidecars without adding ECS-owned solver/cache state.

## Value gate
- Current state: promoted ECS already owns the reusable data model for scene, hierarchy, transform, metadata, geometry sources, dirty domains, selection, physics authoring, and stable IDs.
- Improvement: removes demo/system-token clutter from ECS while preserving only component mappings that current promoted consumers need.
- Scope decision: `NameTag` permanently maps to `MetaData::EntityName`; `AxisRotator`, DEC wrappers, and feature-token behavior are retired from canonical ECS or reassigned to higher-layer sidecars by default.

## Required changes
- [x] Inventory legacy ECS modules and non-legacy tests still importing bare `ECS` or legacy ECS component names.
- [x] Decide whether legacy `NameTag` maps permanently to `MetaData::EntityName`, needs a compatibility migration helper, or is retired.
- [x] Decide whether `AxisRotator` remains a sandbox/demo runtime system, moves to `app`, or is retired.
- [x] Decide whether legacy DEC component wrappers have a promoted ECS authoring use or should be replaced by direct geometry/method data flow.
- [x] Route shared system-feature token needs to `CORE-002` / `RUNTIME-099` rather than adding feature catalogs to ECS.

## Tests
- [x] No new retained compatibility behavior was added, so no new unit test was required. Existing `Test.ECS.SceneBootstrap`, `Test.ECS.TransformHierarchy`, `Test.ECS.BoundsPropagation`, `Test.ECS.RenderSync`, `Test.ECS.LayeringBoundaries`, and runtime `EcsSystemBundle` tests remain the evidence for promoted naming/system behavior.
- [x] Layering regression coverage remains `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` plus `python3 tools/repo/check_layering.py --root src --strict`.
- [x] Consumer-grep evidence is recorded in Context: remaining bare `ECS` imports are legacy subtrees or legacy compatibility tests, so `LEGACY-012` owns migration/removal and no new promoted ECS compatibility module is owed.

## Docs
- [x] Update `docs/architecture/ecs.md` with the final decisions.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` ECS row.
- [x] Update `tasks/backlog/ecs/README.md` with the task status.

## Acceptance criteria
- [x] Each remaining legacy ECS component/system has a documented outcome: promoted replacement, reassigned owner, or explicit retirement.
- [x] No new ECS dependency edge violates `AGENTS.md` §2.
- [x] `LEGACY-006` has no unnamed ECS feature blocker after this task retires; it remains blocked only by explicit legacy consumer-grep and compatibility-test cleanup owned by `LEGACY-012`/subtree deletion gates.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
git diff --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime, graphics, platform, app, live asset services, or physics solver handles into `src/ecs`.

## Maturity
- Completed: `Retired` for legacy-only `AxisRotator`, ECS DEC wrapper, and ECS feature-token/catalog behavior.
- Completed: `CPUContracted` for the retained naming contract through existing `MetaData::EntityName` bootstrap coverage.
- no `Operational` follow-up is owed for CPU-only ECS compatibility decisions.
