---
id: GEOM-027
theme: none
depends_on: []
---
# GEOM-027 — Property name lifetime contract

## Goal
- Normalize `Geometry.Properties` name accessors so property names have one
  public lifetime contract and cannot accidentally return references to
  temporary views.

## Non-goals
- No const-correctness migration for property lookup; `GEOM-030` owns that.
- No erased property metadata catalog; `GEOM-033` owns that.
- No renderer/runtime/ECS/assets/platform/app integration.

## Context
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `PropertyStorageBase::Name()` and `PropertyStorage<T>::Name()` return
  `std::string_view`, while `PropertyBuffer<T>::Name()`,
  `ConstPropertyBuffer<T>::Name()`, `Property<T>::Name()`, and
  `ConstProperty<T>::Name()` expose `const std::string&`.
- The wrapper signatures are a latent template-instantiation hazard and make
  name lifetime harder to document for callers.
- Use the existing `Geometry.Properties` storage ownership; do not duplicate
  property-name storage or add a new registry.

## Required changes
- [ ] Audit every exported name accessor in
      `src/geometry/Geometry.Properties.cppm` and the matching implementation
      unit.
- [ ] Pick one public contract for property names, preferring
      `std::string_view` unless the audit proves an owning reference is
      required.
- [ ] Update `PropertyBuffer<T>`, `ConstPropertyBuffer<T>`, `Property<T>`, and
      `ConstProperty<T>` name accessors to match that contract.
- [ ] Update geometry call sites only where the signature change requires it;
      do not opportunistically rewrite unrelated property code.
- [ ] Fix the stale closing namespace comment in
      `Geometry.Properties.cppm` as a same-file mechanical cleanup.

## Tests
- [ ] Add or update `unit;geometry` coverage that instantiates
      `Property<T>::Name()` and `ConstProperty<T>::Name()` for at least one
      scalar property and one vector-valued property.
- [ ] Add a regression check that a copied property handle still reports the
      same name while the owning `PropertySet` remains alive.
- [ ] Build `IntrinsicTests` with the `ci` preset to force C++ module
      instantiation of the changed exported templates.

## Docs
- [ ] Update
      [`docs/architecture/geometry-api-style.md`](../../../docs/architecture/geometry-api-style.md)
      with the final property-name lifetime rule.
- [ ] Update
      [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md)
      if its property-storage section mentions name ownership or lifetimes.
- [ ] Regenerate
      [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md)
      because this changes the exported module surface.

## Acceptance criteria
- [ ] No exported `Geometry.Properties` wrapper returns a reference to a
      `std::string_view` result.
- [ ] The property-name lifetime contract is documented in architecture docs.
- [ ] Existing property call sites still compile without duplicating name
      storage.
- [ ] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|Properties' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding duplicate property-name storage outside the existing property registry.
- Changing mutable/const property lookup behavior in this task.

## Maturity
- Target: `CPUContracted` (public API cleanup with focused CPU tests).
- No `Operational` follow-up is owed; this task has no backend seam.
