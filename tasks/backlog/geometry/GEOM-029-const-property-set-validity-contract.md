---
id: GEOM-029
theme: none
depends_on: []
---
# GEOM-029 — Const property set validity contract

## Goal
- Give `ConstPropertySet` an explicit invalid/default state contract so
  default-constructed const property views cannot silently dereference null
  storage.

## Non-goals
- No removal of mutable lookup from `const PropertySet`; `GEOM-030` owns that.
- No erased property metadata catalog; `GEOM-033` owns that.
- No ownership changes to mesh, graph, point-cloud, or UV atlas data.
- No renderer/runtime/ECS/assets/platform/app integration.

## Context
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `ConstPropertySet` has a default constructor that leaves its borrowed
  `PropertySet` pointer null, but its methods assume a valid source.
- Some public request/result records use default `ConstPropertySet` values, so
  the invalid state should be deliberate and testable instead of accidental.
- This task should reuse `ConstProperty<T>` invalid-handle behavior rather than
  inventing a parallel optional wrapper.

## Required changes
- [ ] Add an explicit validity query to `ConstPropertySet` such as `IsValid()`
      and `explicit operator bool()`.
- [ ] Define behavior for invalid/default `ConstPropertySet` methods:
      `Size()`, `Empty()`, `Contains()`, `Get()`, and `Names()`.
- [ ] Prefer safe empty-view semantics for lookup-style calls unless the audit
      proves a debug assertion is required for an invariant-only method.
- [ ] Audit default `ConstPropertySet` members in geometry APIs, including UV
      atlas inputs, and keep their intended optional semantics intact.
- [ ] Avoid duplicating property set storage or adding ownership to
      `ConstPropertySet`.

## Tests
- [ ] Add `unit;geometry` tests for default-constructed `ConstPropertySet`.
- [ ] Add tests for a valid borrowed `ConstPropertySet` over an empty
      `PropertySet`.
- [ ] Add tests for a valid borrowed `ConstPropertySet` with at least one
      property and typed const lookup.
- [ ] Add tests that copied `ConstPropertySet` views keep the same validity
      behavior as their source view.

## Docs
- [ ] Update
      [`docs/architecture/geometry-api-style.md`](../../../docs/architecture/geometry-api-style.md)
      with the default/invalid borrowed-view rule for property APIs.
- [ ] Update
      [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md)
      where it describes property sharing or borrowed domain views.
- [ ] Regenerate
      [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md)
      if exported signatures change.

## Acceptance criteria
- [ ] Default `ConstPropertySet` behavior is explicit, documented, and covered
      by tests.
- [ ] Optional const property inputs can be represented without null
      dereference risk.
- [ ] Valid borrowed const property views preserve existing read behavior.
- [ ] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|Properties|UvAtlas' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making `ConstPropertySet` own or copy property storage.
- Changing mutable/const lookup behavior beyond the validity contract.

## Maturity
- Target: `CPUContracted` (borrowed-view validity contract with CPU tests).
- No `Operational` follow-up is owed; this task has no backend seam.
