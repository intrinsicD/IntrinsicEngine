---
id: GEOM-032
theme: none
depends_on: []
---
# GEOM-032 — Bool property access contract

## Goal
- Define and test the `Geometry.Properties` contract for `bool` properties so
  callers cannot accidentally treat `std::vector<bool>` proxy storage as normal
  contiguous boolean memory.

## Non-goals
- No replacement of the entire property storage container family.
- No erased metadata catalog; `GEOM-033` owns public descriptors.
- No renderer/runtime/ECS/assets/platform/app integration.
- No broad rewrite of algorithms that do not use `bool` properties.

## Context
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `std::vector<bool>` has proxy-reference semantics and does not provide the
  same contiguous pointer/span behavior as ordinary `std::vector<T>`.
- `Geometry.Properties` already special-cases some data/span paths for `bool`,
  but the public contract is not documented or covered as its own API policy.
- Reuse existing `Property<T>` and `ConstProperty<T>` accessors where possible;
  add narrowly scoped helpers only if tests show the current API cannot express
  safe bool reads and writes.

## Required changes
- [ ] Audit every `bool` specialization or `if constexpr` path in
      `Geometry.Properties`.
- [ ] Define which operations are supported for `bool` properties: scalar
      get/set, resize, clear, vector access, span access, and raw data access.
- [ ] Keep raw pointer/span access unavailable or explicitly fail-closed for
      `bool` storage.
- [ ] Add narrowly named bool-safe accessors only if existing scalar access is
      insufficient for callers.
- [ ] Avoid duplicating a separate boolean property container unless the audit
      proves the standard proxy storage cannot satisfy existing use cases.

## Tests
- [ ] Add `unit;geometry` tests for creating, resizing, reading, and writing
      `bool` properties.
- [ ] Add tests proving raw pointer/span access is unavailable or returns the
      documented fail-closed value for `bool`.
- [ ] Add tests proving ordinary non-bool property span/data access still works.
- [ ] Add compile-time coverage where practical so unsupported bool span/data
      APIs fail in the intended way.

## Docs
- [ ] Update
      [`docs/architecture/geometry-api-style.md`](../../../docs/architecture/geometry-api-style.md)
      with the bool property caveat and supported operation table.
- [ ] Update
      [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md)
      if property-storage docs describe contiguous data access.
- [ ] Regenerate
      [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md)
      if exported signatures change.

## Acceptance criteria
- [ ] `bool` property behavior is deterministic, documented, and covered by
      focused tests.
- [ ] Callers cannot receive a misleading raw `bool*` or contiguous
      `std::span<bool>` from proxy storage.
- [ ] Non-bool property access remains unchanged.
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
- Exposing raw `bool*` or pretending `std::vector<bool>` is ordinary contiguous
  storage.
- Adding higher-layer dependencies from `src/geometry`.

## Maturity
- Target: `CPUContracted` (property-type contract with CPU tests).
- No `Operational` follow-up is owed; this task has no backend seam.
