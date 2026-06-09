# HARDEN-081 — ECS legacy component compatibility decisions

## Goal
- Resolve remaining legacy ECS component/system compatibility candidates by preferring promoted replacements, reassigned owners, or retirement over compatibility clutter.

## Non-goals
- No live runtime, graphics, physics, platform, app, or asset-service ownership in `src/ecs`.
- No generic ECS command buffer; runtime/editor command policy is owned by `RUNTIME-102`.
- No legacy component-name compatibility wrappers unless a consumer-grep gate proves they are necessary and a removal task is filed.

## Context
- Owner/layer: `ecs -> core`; geometry handles/types only when explicitly required by existing ECS component policy.
- `HARDEN-060..068` promoted scene, hierarchy, transform, events, geometry sources, render sync, bounds, physics authoring, and stable ID value types. The parity matrix still lists legacy `NameTag`, `AxisRotator`, DEC component wrappers, and shared feature-token/catalog behavior as unproven or undecided.
- Prefer existing promoted components: `MetaData::EntityName`, `Transform`, `GeometrySources`, `DirtyTags`, `StableId`, and runtime `EcsSystemBundle`.

## Value gate
- Current state: promoted ECS already owns the reusable data model for scene, hierarchy, transform, metadata, geometry sources, dirty domains, selection, physics authoring, and stable IDs.
- Improvement: removes demo/system-token clutter from ECS while preserving only component mappings that current promoted consumers need.
- Scope decision: `NameTag` should map to `MetaData::EntityName` unless a consumer proves otherwise; `AxisRotator`, DEC wrappers, and feature-token behavior are retired or reassigned by default.

## Required changes
- [ ] Inventory legacy ECS modules and non-legacy tests still importing bare `ECS` or legacy ECS component names.
- [ ] Decide whether legacy `NameTag` maps permanently to `MetaData::EntityName`, needs a compatibility migration helper, or is retired.
- [ ] Decide whether `AxisRotator` remains a sandbox/demo runtime system, moves to `app`, or is retired.
- [ ] Decide whether legacy DEC component wrappers have a promoted ECS authoring use or should be replaced by direct geometry/method data flow.
- [ ] Route shared system-feature token needs to `CORE-002` / `RUNTIME-099` rather than adding feature catalogs to ECS.

## Tests
- [ ] Add or update `unit;ecs` tests for any retained component compatibility behavior.
- [ ] Add layering regression coverage proving ECS remains free of runtime/graphics/platform/app imports.
- [ ] Add consumer-grep evidence for retired legacy-only components.

## Docs
- [ ] Update `docs/architecture/ecs.md` with the final decisions.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` ECS row.
- [ ] Update `tasks/backlog/ecs/README.md` with the task status.

## Acceptance criteria
- [ ] Each remaining legacy ECS component/system has a documented outcome: promoted replacement, reassigned owner, or explicit retirement.
- [ ] No new ECS dependency edge violates `AGENTS.md` §2.
- [ ] `LEGACY-006` has no unnamed ECS feature blocker after this task retires.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime, graphics, platform, app, live asset services, or physics solver handles into `src/ecs`.

## Maturity
- Target: `CPUContracted` for any retained ECS compatibility contract; explicit `Retired` decision for legacy-only demo behavior.
- No `Operational` follow-up is owed for CPU-only ECS compatibility decisions.
