---
id: RUNTIME-192
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-192 — Canonical geometry-property reference and catalog

## Status

- Completed and retired on 2026-07-26 at `Retired`.
- Commit reference: this retirement commit plus the four slice commits
  (`Slice A` contract, `B1` editor-visualization enum, `B2` progressive value
  kind, `B3` progressive domain, `B4` editor-catalog enum).
- Endpoint: `Extrinsic.Runtime.GeometryAvailability` is the single owner of
  `GeometryPropertyRef`, `GeometryPropertyCatalogSnapshot`,
  `GeometryPropertyValueKindFilter`, and the shared resolution queries. **No new
  module was created** — the canonical vocabulary landed beside
  `GeometryElementDomain` in the module that already resolved it, so the task
  added zero files while deleting four duplicate enums and their conversion
  switches.
- Two persisted wire formats were discovered mid-migration and preserved
  (property value kind and property domain); both mappings are owned by
  `Runtime.SceneSerialization` and covered by five regression tests, including
  guards proving the reader rejects the canonical names so a future
  "modernization" cannot silently invalidate existing scene documents.
- Verification evidence:
  - clean `IntrinsicTests` build;
  - CPU gate **4248/4248** (`-LE 'gpu|vulkan|slow|flaky-quarantine'`), one skip
    (`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`, headless host);
  - strict layering (0 violations), test-layout, docs-sync, doc-link,
    root-hygiene, task-policy, task-state-link, and maturity checks passed;
  - module inventory regenerated at 388 modules.

## Progress

Promoted to active 2026-07-25. Migration is chunked per duplicated vocabulary so
each chunk builds, passes the CPU gate, and commits independently.

- [x] **Slice A — contract.** Canonical `GeometryPropertyRef`,
      `GeometryPropertyCatalogSnapshot`, `GeometryPropertyResolution`, and the
      centralized name/domain/value-kind/count/finite-value queries landed in the
      existing `Extrinsic.Runtime.GeometryAvailability` module. Deliberately **no
      new module**: `GeometryAvailability` already owns `GeometryElementDomain`
      and is the resolver, so the canonical vocabulary belongs beside it and the
      slice adds zero files. Six contract tests cover every element domain and
      value kind, deterministic catalog order, source/property generation
      identity, and each distinct failure mode (unsupported domain, missing
      name, missing property, kind mismatch, count mismatch, non-finite).
      CPU gate 4242/4242.
- [x] **Slice B1 — retire `SandboxEditorVisualizationPropertyValueKind`** (25 refs / 5 files).
      The editor-local enum was a strict subset of `Geometry::PropertyValueKind`
      (`ScalarFloat`→`Float`, `ScalarDouble`→`Double`, `Vec3`/`Vec4`/`UInt32`
      identical), so the model field now carries the canonical kind and the
      hand-rolled `DetectVisualizationPropertyKind` was replaced by
      `DetectGeometryPropertyValueKind`. Kinds outside the visualization-capable
      set (`Bool`/`Int32`/`UInt64`/`Vec2`) fall through the existing
      scalar/color/vector/integer predicates and are skipped, preserving the
      old `nullopt` behavior. The per-editor debug name became one shared
      `DebugNameForGeometryPropertyValueKind`. CPU gate 4242/4242.
      Note: `app` may import runtime only, so the Sandbox panels pass the
      canonical kind through without importing `Geometry.Properties` — the
      `ExtrinsicSandboxAppStaysRuntimeOnly` structural test enforces this and
      caught a first attempt that imported it.
- [x] **Slice B2 — retire `ProgressivePropertyValueKind`** (279 refs / 19 files).
      Value mapping was mechanical (`ScalarFloat`→`Float`, `ScalarDouble`→`Double`,
      rest identical). `Any` became `GeometryPropertyValueKindFilter`
      (`std::nullopt`): `ExpectedValueKind` fields on the binding descriptor,
      selected-mesh bake request, and four editor models now carry a constraint,
      while `ActualValueKind`/`Kind`/`ValueKind` carry a resolved kind. Every
      `case Any:` was an unreachable branch sharing its body with `case Unknown:`,
      so dropping it is behavior-preserving; where a *resolved* kind is still
      required (bake representation APIs) the constraint resolves via
      `ResolvedExpectedValueKind()` → `value_or(Unknown)`, which lands on that
      same shared branch. The duplicate `DetectPropertyValueKind` was deleted in
      favor of the canonical `DetectGeometryPropertyValueKind`.
      **Wire format preserved:** the legacy strings moved into
      `Runtime.SceneSerialization.cpp` as a file-local mapping owned by the
      serializer, so the persisted format still reads/writes
      `ScalarFloat`/`ScalarDouble`/`Any`. Three regression tests pin it: the
      emitted strings, a full round trip, and a guard proving the reader
      *rejects* canonical `Float`/`Double` names (so a future "modernization" of
      both writer and reader cannot silently invalidate old documents).
      `Runtime.GeometryAvailability` now re-exports `Geometry.Properties`,
      because its public surface names those types and `app` may import runtime
      only. CPU gate 4245/4245.

- [x] **Slice B3 — retire `ProgressiveGeometryDomain`** (174 refs / 24 files).
      Mapped onto `GeometryElementDomain` (`GraphVertex`→`GraphNode`,
      `Point`→`PointCloudPoint`, rest identical). `MeshSurface` was split out as
      predicted: it is a *job scope*, so `Runtime.DerivedJobGraph` gained a
      module-local `DerivedJobScope` (plus `ToDerivedJobScope`) for
      `DerivedJobKey` identity, while the four property paths that listed
      `MeshSurface` shared their body with `Unknown` and simply lost the case.
      **Second wire-format constraint found and preserved:** the property
      *domain* is persisted too, and its legacy strings also diverge from the
      canonical names — the wire says `GraphVertex`/`Point`. A serializer-local
      mapping keeps them, and legacy `MeshSurface` is accepted on read and
      mapped to `Unknown` (the behavior every property path already had) rather
      than rejected. Two more regression tests pin this. CPU gate 4247/4247.

- [x] **Slice B4 — retire `SandboxEditorPropertyCatalogValueKind`** (34 refs / 5 files).
      The last duplicate, found while migrating B2. Its two converters became
      identity functions and were deleted. One deliberate behavior improvement:
      the catalog previously collapsed `Bool`/`Int32`/`UInt64` to `Unknown`
      because its enum could not represent them; it now reports the true kind
      while `IsPropertyCatalogSupportedKind` preserves the exact
      supported/bindable gating, so an `int32` property stays unbindable and is
      merely named accurately. `Test.SandboxEditorModels.cpp` was updated to
      assert that stronger contract.
- [x] **Closure evidence.** `RuntimeEngineLayering.NoDuplicateGeometryPropertyVocabularyRemains`
      proves no duplicate domain/value-kind enum or conversion switch remains,
      that the canonical vocabulary lives in one module, that `MeshSurface` is a
      job scope and not an element domain, and that the serializer still owns the
      legacy wire spellings.

### Follow-up found during Slice B2

- **`SandboxEditorPropertyCatalogValueKind` is a third duplicate** of the same
  vocabulary (`ScalarFloat`/`ScalarDouble`/`UInt32`/`Vec2`/`Vec3`/`Vec4`/`Unknown`,
  converted by `ToGeometryPropertyValueKind` in `Runtime.SandboxEditorFacades.cpp`).
  It is out of scope for B2/B3 but is exactly the "equivalent bake/editor alias"
  this task's Required-changes list targets, so it should be retired in a
  Slice B4 before the task closes.

### Design decisions taken in Slice A

- **`Any` is a filter, not a value kind.** `ProgressivePropertyValueKind::Any`
  put "no constraint" alongside real kinds, so every switch over it carried an
  unreachable case. The canonical form is
  `GeometryPropertyValueKindFilter = std::optional<Geometry::PropertyValueKind>`,
  where `std::nullopt` means unconstrained.
- **`MeshSurface` is a job scope, not a property domain.** Census showed
  `ProgressiveGeometryDomain::MeshSurface` resolves to `nullptr` /
  `UnsupportedDomain` in every property path and is only ever used as a
  `DerivedJobKey` discriminator. It must **not** enter `GeometryElementDomain`.
  Slice B3 gives `DerivedJobKey` its own local scope enum instead — noting that
  `RUNTIME-194` deletes `Runtime.DerivedJobGraph` outright, so that enum should
  stay module-private and short-lived.
- **Finite-value scanning is opt-in** (`requireFiniteValues`) because it is O(n)
  over the property and only bake-like consumers need it.

## Goal

- Establish one pointer-free semantic runtime vocabulary for referring to a
  geometry property and enumerating resolved entries in a
  generation-stamped source snapshot, then
  migrate every runtime consumer to it and delete the duplicate
  progressive/editor/bake domain and value-kind vocabularies.

## Non-goals

- No universal variant that stores arbitrary property values.
- No merge of `ECS::GeometrySources::Domain`, mesh sampling/raster domains,
  `Runtime::VertexChannel`, `Graphics::MaterialChannel`, or visualization
  semantics; those answer different questions.
- No registry, interface, service hierarchy, property copy, or live ECS
  pointer in the reference.
- No property derivation algorithm. Callers remain responsible for producing
  world normals, curvature, labels, or any other special field.

## Context

- `Runtime.GeometryAvailability` already owns the general
  `GeometryElementDomain`, while `Geometry.Properties` owns
  `Geometry::PropertyValueKind`. `Runtime.ProgressiveRenderData`,
  `Runtime.SandboxEditorFacades`, texture-bake records, visualization records,
  and vertex-attribute helpers currently mirror or translate subsets of that
  vocabulary.
- The duplicated enums force conversion switches and let otherwise identical
  property identity drift between editor, presentation, bake, and extraction.
- The right-sized endpoint is a plain authoring-safe `GeometryPropertyRef`
  (domain/name/value kind) plus a copied, resolved
  `GeometryPropertyCatalogSnapshot` carrying source/property generations;
  `GeometryAvailability` remains the resolver and no new long-lived owner is
  required.

## Slice plan

- **Slice A — contract.** Add the canonical reference/catalog records,
  deterministic validation, and generation identity with focused unit tests.
- **Slice B — adoption.** Migrate bake, selected analysis, presentation,
  visualization, readiness, and attribute-resolution callers without changing
  their feature semantics.
- **Slice C — cleanup.** After equivalence tests pass, delete the duplicate
  enums, conversion switches, aliases, and obsolete public fields in a
  separate mechanical commit.

## Required changes

- [x] Export one semantic `GeometryPropertyRef` using
      `GeometryElementDomain`, `Geometry::PropertyValueKind`, and a stable
      property name. It contains no entity, pointer, count, generation,
      storage, material, or visualization state and is safe to place in
      desired authoring recipes.
- [x] Export one pointer-free `GeometryPropertyCatalogSnapshot` with stable
      ordering, source identity, availability/source generation, and resolved
      entries pairing each plain reference with element count and property
      generation; keep live property storage and ECS handles out.
- [x] Centralize name/domain/value-kind/count/finite-value compatibility
      queries as pure functions over the existing availability/property views.
- [x] Migrate `Runtime.TextureBakeModule`, selected-entity analysis and
      readiness, progressive presentation, visualization extraction, and
      vertex-attribute candidate enumeration to the canonical reference.
- [x] Delete `ProgressiveGeometryDomain`,
      `ProgressivePropertyValueKind`,
      `SandboxEditorVisualizationPropertyValueKind`, and equivalent
      bake/editor aliases after every production caller uses the canonical
      contract.
- [x] Keep provenance domain, sampling domain, structural vertex channels,
      material channels, and visualization output meaning as distinct typed
      fields where a consumer actually needs them.

## Tests

- [x] Unit contracts cover every supported element domain and property value
      kind, deterministic catalog order, stale snapshot/generation rejection,
      missing property, wrong domain/kind/count, and non-finite diagnostics.
- [x] Cross-consumer parity tests prove one reference resolves identically for
      bake, presentation, visualization, and selected-analysis validation.
- [x] Structural tests or strict source scans prove no duplicate runtime
      property-domain/value-kind enum or conversion switch remains.

## Docs

- [x] Document the property-reference contract and the intentionally separate
      domain/channel vocabularies in `src/runtime/README.md`.
- [x] Update affected architecture docs and regenerate
      `docs/api/generated/module_inventory.md`.
- [x] Refresh task indexes, the session brief, and retirement records.

## Acceptance criteria

- [x] Every runtime feature that names a geometry property uses
      `GeometryPropertyRef` and the shared catalog/resolver.
- [x] Property meaning is prepared by callers and is not encoded as a
      specialized runtime path or duplicate enum.
- [x] The old progressive/editor/bake property identity vocabularies and all
      compatibility aliases are deleted after tests pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperty|PropertyCatalog|GeometryAvailability' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Creating a second property store, registry, service, or opaque type-erased
  value container.
- Treating provenance, raster sampling, material channels, and property
  element domains as interchangeable.
- Keeping aliases indefinitely after the migrated tests are green.

## Maturity

- Target: `Retired`; `CPUContracted` establishes the canonical value
  vocabulary, while closure requires all production adoption and deletion of
  the duplicated contracts.
