---
id: RUNTIME-198
theme: B
depends_on: [RUNTIME-192, RUNTIME-193, RUNTIME-194, RUNTIME-197]
maturity_target: Retired
---
# RUNTIME-198 — Data-driven visualization recipes

## Status

- Promoted to active on 2026-07-28 after `RUNTIME-192`, `RUNTIME-193`,
  `RUNTIME-194`, and `RUNTIME-197` retired the property, presentation, work,
  and residency prerequisites.
- Intake census found a 219-line exported adapter interface plus a 1,060-line
  implementation and 1,136-line wrapper/registry contract. Production has
  zero adapter registrations and the app submits zero opaque adapter-binding
  commands. Render extraction only stack-constructs scalar and color adapters
  as disguised free functions; vector, isoline, curvature-direction, and Htex
  adapter registration are exercised by tests, while the app merely displays
  a binding that cannot resolve without a production registration.
- `VisualizationConfig` and `GeometryPresentationRecipe` already carry the
  authored scalar/color/property intent. The replacement must project those
  existing values plus an optional closed per-entity recipe directly into the
  existing graphics packet/residency seam, not introduce another persisted
  visualization schema or service.
- Slice A landed a closed seven-alternative `VisualizationRecipe`, canonical
  property resolution, deterministic encoding statuses, owning packet/payload
  results, and a separate typed `ScheduleVisualizationHtexRecreate` operation.
  The encoder never schedules background work. Five new recipe contracts and
  the 22 existing adapter parity contracts pass in the Clang 23 `ci` tree.
- Slice B migrated render extraction, scene reset/revision state, and the
  Sandbox editor facade to copied per-entity recipes. Scalar/color config is
  translated into typed recipes, ready `GeometryPresentationRecipe` property-
  buffer slots project directly into the same encoder, presentation state
  suppresses duplicate config packets, and an explicit recipe takes
  precedence over both. Focused Clang 23 coverage passes 12 extraction/
  presentation cases plus four lifecycle/editor contracts; production source
  scans find no adapter registration or binding call site.

## Goal

- Replace the runtime visualization adapter interface/registry/opaque-key
  binding path with one plain `VisualizationRecipe` variant and pure typed
  encoders that consume canonical property references and emit existing
  graphics visualization packets.

## Non-goals

- No merge of scientific visualization with PBR material channels.
- No new renderer pass, property derivation algorithm, GPU resource owner, or
  universal plugin registry.
- No background work launched by a pure visualization encoder. An Htex/atlas
  rebuild is a typed feature operation on `JobService`; its completed metadata
  may then be referenced by a recipe.
- No claim that every visualization kind has identical inputs; the recipe is a
  closed typed variant with per-kind data.

## Context

- `Runtime.VisualizationAdapters` exports an interface, concrete adapters, and
  a registry, while production extraction directly constructs the scalar and
  K-Means paths and production code does not register external adapters.
- The interface/opaque key therefore adds lifetime and missing-adapter states
  without a real extension consumer. Existing graphics packet types and
  property-buffer residency remain useful destinations.
- `RUNTIME-192` supplies property identity, `RUNTIME-193` supplies desired
  presentation state, and `RUNTIME-197` supplies common property-buffer
  residency.

## Right-sizing decision

- **Elements:** `IVisualizationAdapter`, six concrete wrappers, the registry,
  owned adapter map, opaque numeric keys, per-entity binding map, public
  registration forwarding, and Sandbox facade records trigger the shallow-
  wrapper, role-fragmentation, registry-without-extension-consumer, and
  parallel-authoring heuristics. The numeric/range/packet algorithms and
  graphics packet types are real and must remain.
- **Simpler alternative:** replace object dispatch with one closed
  `VisualizationRecipe` variant and deterministic free encoders. Each typed
  alternative carries canonical `GeometryPropertyRef` values plus only its
  own scalar/color/vector/isoline/atlas metadata. Extraction builds recipes
  from existing `VisualizationConfig`/presentation state or accepts one copied
  optional recipe value; no adapter object, key, factory, registry, or service
  is introduced.
- **Htex boundary:** encoding never schedules work. Preserve the existing
  request-token behavior, if still required by a caller, as a separate typed
  free operation composed directly with `JobService`; do not create an Htex
  service for a single operation or claim that the current token-only job is a
  regeneration algorithm.
- **Blast radius:** the visualization runtime module and tests, render-
  extraction public/private state and stats, Sandbox facade/model display,
  runtime/graphics visualization docs and ADR, CMake/test registration, task
  indexes, and generated module inventory. Graphics packet validation,
  residency, renderer passes, and shader behavior are unchanged.
- **Reintroduction trigger:** add an extension registry only when a live
  independently deployed visualization implementation cannot be represented
  by the closed variant. A new built-in visualization kind, editor, config,
  test, or backend remains variant data plus a pure encoder.

## Slice plan

- **Slice A — recipe/encoders.** Add a closed plain variant for scalar, color,
  labels, vector field, isolines, and supported atlas metadata plus pure
  validation/encoding functions.
- **Slice B — extraction adoption.** Project presentation recipes directly
  during render extraction and migrate editor/config/result workflows.
- **Slice C — retirement.** After packet/residency parity, delete the adapter
  interface, registry, opaque bindings, registration methods, and obsolete
  tests/docs.

## Required changes

- [x] Define `VisualizationRecipe` as plain data with a typed alternative for
      each supported visualization and `GeometryPropertyRef` for source fields.
- [x] Implement pure encoders returning packet batches and deterministic
      diagnostics; keep allocation/residency in the existing common owners.
- [x] Project recipes directly from `GeometryPresentationRecipe` during copied
      runtime extraction without borrowed adapter/source pointers.
- [x] Migrate scalar/color/K-Means/vector/isoline/Htex/fragment-preview
      production paths and preserve their ranges, colormaps, budgets, stamps,
      and rejection diagnostics.
- [x] Move Htex/atlas regeneration out of adapter encoding and onto the
      canonical typed `JobService` operation; recipes encode only current
      copied metadata/results.
- [ ] Delete `IVisualizationAdapter`,
      `VisualizationAdapterRegistry`, adapter binding keys,
      `RegisterVisualizationAdapter`/`UnregisterVisualizationAdapter`, and
      concrete wrapper classes after parity.

## Tests

- [x] Pure encoder tests cover every recipe alternative, valid packets,
      malformed/missing/stale properties, deterministic diagnostics, and empty
      output.
- [x] Render-extraction parity tests preserve packet bytes/counts, property
      residency requests, cache invalidation, and visualization statistics.
- [ ] Existing Vulkan visualization smokes run through recipes.
- [ ] Source scans prove no production registry/adapter interface or opaque
      visualization key remains.

## Docs

- [ ] Update visualization/runtime extraction docs with recipe alternatives,
      pure encoding, and material/visualization separation.
- [ ] Regenerate the module inventory and remove old adapter registration
      instructions.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [x] All production visualization packets originate from typed recipes and
      pure encoders using canonical property references.
- [x] No production lifetime depends on an adapter object or opaque registry
      key.
- [ ] The old adapter interface, registry, wrappers, and extraction
      registration surface are deleted after parity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Visualization|RenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'Visualization' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Open-ended adapter/plugin registry without a live external implementation.
- Encoding visualization meaning into material source enums.
- Keeping old adapter registration as a permanent compatibility route.

## Maturity

- Target: `Retired`; pure CPU packet parity and operational Vulkan coverage
  must precede removal of the adapter/registry family.
