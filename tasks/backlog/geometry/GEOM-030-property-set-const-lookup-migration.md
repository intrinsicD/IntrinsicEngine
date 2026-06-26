---
id: GEOM-030
theme: none
depends_on: [GEOM-029]
---
# GEOM-030 — Property set const lookup migration

## Goal
- Remove the mutable property lookup path from `const PropertySet` so const
  geometry views cannot hand out mutable `Property<T>` handles.

## Non-goals
- No default/null validity work for `ConstPropertySet`; `GEOM-029` owns that.
- No erased property metadata catalog; `GEOM-033` owns that.
- No broad geometry algorithm rewrites beyond call-site migration required by
  the const-correctness change.
- No renderer/runtime/ECS/assets/platform/app ownership changes.

## Context
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `PropertySet::Get<T>(std::string_view) const` currently forwards through
  `const_cast` and returns mutable `Property<T>`.
- This undermines borrowed const views and makes it too easy for read-only
  algorithms to mutate shared property storage.
- `GEOM-029` must land first so the replacement const access path has a safe
  invalid/default contract.

## Slice plan
- **Slice A.** Add an explicit const lookup API, or change the existing const
  overload to return `ConstProperty<T>` if the call-site audit proves the source
  break is manageable in one review.
- **Slice B.** Migrate geometry, tests, and lower-layer consumers that only read
  properties to the const-returning path.
- **Slice C.** Remove the `const_cast` implementation and any temporary mutable
  compatibility shim introduced during the migration.

## Required changes
- [ ] Audit all `PropertySet::Get<T>` use on const objects, including domain
      views, UV atlas inputs, normal recompute paths, runtime readback helpers,
      and tests.
- [ ] Provide a const lookup path that returns `ConstProperty<T>` or an
      equivalent read-only handle.
- [ ] Update read-only call sites to use the const handle without copying
      property storage.
- [ ] Keep mutating call sites on non-const `PropertySet` and make their
      mutability explicit.
- [ ] Delete the `const_cast` path from `Geometry.Properties.cppm` once all
      in-repo callers compile.

## Tests
- [ ] Add `unit;geometry` tests proving a `const PropertySet&` cannot produce a
      mutable `Property<T>` through the public API.
- [ ] Add tests that `ConstProperty<T>` lookup preserves read access to names,
      size, spans or values supported by the property type.
- [ ] Update affected geometry tests so read-only algorithms use const property
      handles.
- [ ] Build `IntrinsicTests` with the `ci` preset to catch source breakage in
      all module importers.

## Docs
- [ ] Update
      [`docs/architecture/geometry-api-style.md`](../../../docs/architecture/geometry-api-style.md)
      with the `Property` vs `ConstProperty` rule.
- [ ] Update
      [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md)
      where borrowed domain views or property sharing describe mutation.
- [ ] Regenerate
      [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md)
      because this changes the exported module surface.

## Acceptance criteria
- [ ] No `const_cast` is used to implement public const property lookup.
- [ ] Const property views provide read access without copying property
      storage.
- [ ] Mutating property access remains available only through non-const property
      owners or explicitly mutable borrowed views.
- [ ] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|DomainViews|Normals|UvAtlas|Readback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Copying property storage merely to regain mutability.
- Adding higher-layer dependencies from `src/geometry`.

## Maturity
- Target: `CPUContracted` (const-correct API migration with CPU tests).
- No `Operational` follow-up is owed; this task has no backend seam.
