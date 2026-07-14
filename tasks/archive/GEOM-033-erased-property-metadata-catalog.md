---
id: GEOM-033
theme: none
depends_on: [GEOM-027, GEOM-030, GEOM-032]
---
# GEOM-033 — Erased property metadata catalog

## Goal
- Expose a small, geometry-owned erased property metadata catalog so consumers
  can inspect property names, value types, sizes, and supported access modes
  without duplicating `Geometry.Properties` internals.

## Non-goals
- No renderer, runtime, ECS, UI, or asset binding implementation in this task.
- No GPU upload or readback path changes.
- No replacement of typed `Property<T>` and `ConstProperty<T>` accessors.
- No new dependency from `src/geometry` to higher layers.

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Migrate geometry property and topology utilities`).
- Maturity: `CPUContracted`.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `PropertySet::Properties()` currently exposes names only, which is enough for
  simple enumeration but not enough for runtime/UI binding, debug inspectors, or
  method diagnostics that need erased type information.
- The catalog must build on existing `PropertyStorageBase` type information and
  the const-correct lookup contract from `GEOM-030`.
- `GEOM-027` should land first so metadata names have a stable lifetime rule;
  `GEOM-032` should land first so bool/proxy storage is represented honestly.

## Slice plan
- **Slice A.** Add a narrow descriptor record and enumeration API in
  `Geometry.Properties` using existing storage metadata.
- **Slice B.** Add focused tests for scalar, vector, and bool properties,
  including const enumeration.
- **Slice C.** Update lower-layer docs and only then let higher-layer tasks
  consume the catalog in their own scopes.

## Required changes
- [x] Define an exported descriptor record, such as `PropertyDescriptor`, with
      only metadata `Geometry.Properties` can state truthfully today.
- [x] Include property name, erased value identity or kind, element count,
      mutability/read-only view context, and whether contiguous span/data access
      is supported.
- [x] Reuse existing typed storage and type-erasure helpers; do not introduce a
      second registry or duplicate per-property metadata state.
- [x] Add descriptor enumeration on `PropertySet` and `ConstPropertySet` or an
      equivalent borrowed const catalog path.
- [x] Keep typed lookup as the authority for reading/writing values; descriptors
      are for inspection and binding decisions only.

## Tests
- [x] Add `unit;geometry` tests for descriptor enumeration on an empty property
      set.
- [x] Add descriptor tests for at least `float`, `glm::vec3`, and `bool`
      properties.
- [x] Add tests proving descriptor enumeration through a const view cannot hand
      out mutable storage.
- [x] Add tests that descriptor counts and names stay stable after adding
      multiple properties.

## Docs
- [x] Update
      [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md)
      with the descriptor/canonical typed-access relationship.
- [x] Update
      [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md)
      with the property catalog contract.
- [x] Update higher-layer architecture docs only to point at the geometry-owned
      catalog contract; do not document unimplemented runtime/UI bindings here.
- [x] Regenerate
      [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md)
      because this changes the exported module surface.

## Acceptance criteria
- [x] Callers can enumerate property descriptors without knowing the concrete
      value type at compile time.
- [x] Descriptors expose enough information to decide whether typed lookup,
      const lookup, or contiguous span/data access is legal.
- [x] Descriptor enumeration does not duplicate storage state and does not
      weaken const-correctness.
- [x] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|Properties|Runtime|Binding' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime, graphics, ECS, UI, assets, platform, or app from geometry.
- Using erased descriptors as a replacement for typed value access.

## Maturity
- Target: `CPUContracted` (geometry-owned metadata contract with CPU tests).
- No `Operational` follow-up is owed; higher-layer consumers must land under
  their own tasks.
