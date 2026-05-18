# HARDEN-068 — Define ECS stable identity and scene metadata contract

## Status

- Status: in-progress (slice 2 / HARDEN-068-Impl-A in flight on a new
  branch; slice 1 landed on `main` via PR #865 / commit `559134a`).
- Owner/agent: Claude on `claude/setup-agentic-workflow-PaRh5` (slice 2);
  previously Claude on `claude/setup-agentic-workflow-9l6ef` (slice 1).
- Branch: `claude/setup-agentic-workflow-PaRh5`.
- Started: 2026-05-18 (slice 1), 2026-05-18 (slice 2 on this branch).
- Current slice: slice 2 — HARDEN-068-Impl-A: ship the
  `Extrinsic.ECS.Component.StableId` payload module + CPU-only
  `unit;ecs` payload tests + targeted layering boundary case.
- Slice 2 deliverables landed in this branch:
  - `src/ecs/Components/ECS.Component.StableId.cppm` exporting
    `Extrinsic::ECS::Components::StableId`, `kInvalidStableId`,
    `IsValid`, defaulted equality / `operator<=>`, and the exported
    `StableIdHash` functor (splitmix64-style mix of the two halves
    plus an asymmetric combiner so swapped halves do not collide).
    The payload imports only `<compare>`, `<cstddef>`, and `<cstdint>`;
    no `entt`, `geometry`, `assets`, `runtime`, `graphics`, or
    `platform` headers.
  - `src/ecs/Components/CMakeLists.txt` registers the new
    `CXX_MODULES` file in alphabetical order.
  - `tests/unit/ecs/Test.ECS.StableIdentity.cpp` covering default
    construction → sentinel equality, explicit-bit-pattern assertions
    on `kInvalidStableId`, `IsValid` truth table, equality
    componentwise, `operator<=>` lexicographic ordering, swapped-halves
    hash distinguishability, sentinel-vs-near-neighbor hash
    distinctness, `std::unordered_map<StableId, T, StableIdHash>`
    cross-module-boundary usage, and the trivially-copyable /
    standard-layout / 16-byte size invariants. Labels `unit;ecs` via
    the existing `IntrinsicECSTests` executable.
  - `tests/CMakeLists.txt` wires the new test file into the
    `_ecs_test_files` list.
  - `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` adds
    `StableIdPayloadStaysCpuOnly` — a targeted case that asserts the
    StableId payload header does not pull in `entt` or any
    higher-layer module (the directory-recursive sweep already covers
    `Extrinsic.{Graphics,RHI,Runtime,Platform,App,Asset}.*`, but the
    no-`entt` rule is unique to this payload because `entt` is legal
    elsewhere in `src/ecs`).
  - Docs sync: `src/ecs/README.md` (public module surface, directory
    layout, "Stable identity and scene metadata" status block);
    `src/ecs/Components/README.md` (Contents list,
    "Stable identity vs `MetaData`" block); `docs/architecture/ecs.md`
    ("Stable identity and scene metadata" status block); the `ecs`
    row of `docs/migration/nonlegacy-parity-matrix.md`;
    `docs/api/generated/module_inventory.md` (regenerated; new
    `Extrinsic.ECS.Component.StableId` row).
- Local verification this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict`.
  - `python3 tools/repo/check_layering.py --root src --strict`.
  - `python3 tools/repo/check_test_layout.py --root . --strict`.
  - `python3 tools/docs/check_doc_links.py --root .`.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` (435 modules;
    `Extrinsic.ECS.Component.StableId` registered).
  - **Default CPU gate deferred to CI.** The pinned `clang-20` /
    `clang-scan-deps-20` toolchain required by `CMakePresets.json`
    is unavailable on this remote-execution host (matches the
    HARDEN-065 slice-2 precedent and GRAPHICS-033D). Per
    `/AGENTS.md` §5 ("Do not treat GCC or stale non-preset build
    trees as valid verification for module changes") the
    `cmake --preset ci` build + `IntrinsicECSTests` /
    `IntrinsicEcsContractTests` run on the pinned toolchain via the
    PR's `pr-fast` workflow row before retirement.
- Next verification step: monitor the PR's `pr-fast` (pinned-clang-20)
  job for green; if `IntrinsicECSTests` (label `ecs`,
  `Test.ECS.StableIdentity.*`) and `IntrinsicEcsContractTests` (label
  `contract`, `EcsLayeringBoundaries.StableIdPayloadStaysCpuOnly`)
  both pass, retire this task to
  `tasks/done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`
  with the completion date (YYYY-MM-DD) and PR/commit reference.
  HARDEN-068-Impl-B and HARDEN-068-Impl-C remain identified-only and
  open from the backlog when a concrete consumer demands them.

## Slice plan

- **Slice 1 (this slice, planning only).** Record the five HARDEN-068
  decisions inline in this task file's "Recorded decisions" section,
  update `src/ecs/README.md`, `src/ecs/Components/README.md`,
  `docs/architecture/ecs.md`, and the `ecs` row of
  `docs/migration/nonlegacy-parity-matrix.md` with the stable-identity
  policy, and identify implementation children (Impl-A/B/C). No C++
  source, no new modules, no test files, no module-inventory delta.
  Verification: docs/structural validators only (the pinned `clang-20`
  CPU gate is not exercised because nothing compilable changes).

- **Slice 2 (in flight on `claude/setup-agentic-workflow-PaRh5` —
  HARDEN-068-Impl-A): `StableId` payload module.**
  Adds `src/ecs/Components/ECS.Component.StableId.cppm` exporting the
  `Extrinsic::ECS::Components::StableId` value type (two `std::uint64_t`
  halves), `kInvalidStableId`, `IsValid`, defaulted equality /
  `operator<=>`, and the exported `StableIdHash` functor, with the
  "this is metadata, not a registry-wide field" documentation inline.
  Adds `tests/unit/ecs/Test.ECS.StableIdentity.cpp` with
  default-construction, sentinel equality, explicit-bit-pattern
  assertions, `IsValid` truth table, equality, `operator<=>`
  lexicographic ordering, hash distinguishability (swapped halves
  + sentinel vs near neighbors), cross-module
  `std::unordered_map<StableId, T, StableIdHash>` usage, and
  trivially-copyable / standard-layout / 16-byte invariants. Extends
  `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` with the
  targeted `StableIdPayloadStaysCpuOnly` case (the directory-recursive
  sweep already covers `Extrinsic.{Graphics,RHI,Runtime,Platform,App,
  Asset}.*` imports; the targeted case additionally forbids `entt`
  inside the StableId payload because `entt` is legal elsewhere in
  `src/ecs`). Regenerates `docs/api/generated/module_inventory.md`
  (435 modules). Default CPU gate (`cmake --preset ci` +
  `IntrinsicECSTests` + `IntrinsicEcsContractTests` on the pinned
  `clang-20` toolchain) runs in CI before retirement, matching the
  HARDEN-065 slice-2 / GRAPHICS-033D precedent.

- **Slice 3 (deferred — HARDEN-068-Impl-B): generator + scene-local
  lookup contract.** Add a deterministic-by-seed and a random
  `StableId` generator (CPU-only, no global RNG state in ECS — caller
  passes seed/engine); optionally promote a thin `StableIdRegistry`
  sidecar contract under `runtime/` (NOT `ecs/`) if a serializer or
  selection consumer actually needs scene-local `StableId → entt::entity`
  lookup. Open only when a concrete consumer task exists; do not open
  speculatively.

- **Slice 4 (deferred — HARDEN-068-Impl-C, optional): adjacent
  authoring metadata.** Open only when a scene serializer or prefab
  ingest task explicitly demands them: separate components for
  `SerializationHints`, `SceneSource`, or prefab provenance. Each is
  its own focused component (do not extend `MetaData`).

## Recorded decisions

### Decision 1 — Stable entity identity shape

**Pick: 128-bit UUID-shaped `StableId`** (`struct StableId { std::uint64_t High; std::uint64_t Low; }`).

| Option considered | Verdict | Rationale |
|---|---|---|
| 128-bit UUID-shaped pair | **Pick** | Globally unique without coordination (supports cross-scene/prefab references, hot reload, undo/redo); industry-standard scene-serializer pattern (Unity GUID, Unreal FGuid, glTF UUID extensions); fixed 16-byte payload trivially copyable and hashable. |
| Scene-local 32-bit / 64-bit integer | Rejected | Collisions become real over a project lifetime once prefabs cross scenes; forces a global authoring-time uniqueness coordinator that doesn't exist; doesn't survive merging two scenes from independent edits. |
| Generational `Core::StrongHandle<EntityTag>` pair | Rejected | Generational handles are insertion-order-dependent and not durable across save/load or hot-reload; `entt::entity` already covers the in-process generational case. |
| Hash of `MetaData::EntityName` | Rejected | Names collide; renames invalidate references; not stable across rename refactors. |

Sentinel value: `kInvalidStableId = StableId{0u, 0u}` (both halves zero).
`IsValid(StableId)` returns `false` only for `kInvalidStableId`.
Equality, `operator<=>`, and an explicit `StableIdHash` are provided so
`std::unordered_map<StableId, …>` works across module boundaries (mirrors
the `Extrinsic.Core.StrongHandle` exported-hasher pattern at
`src/core/Core.StrongHandle.cppm:61`).

### Decision 2 — Mandatory vs optional

**Pick: optional authoring metadata component, not a registry-wide field.**

`StableId` is a sparse `entt`-component opted into by authoring/runtime
code that actually needs serializer/undo/external-reference durability.
The ECS bootstrap path (`Extrinsic.ECS.Scene.Bootstrap::CreateDefault`)
does **not** assign a `StableId` automatically.

| Option | Verdict | Rationale |
|---|---|---|
| Optional sparse component | **Pick** | Transient/runtime-only entities (procedural geometry instances, debug overlays, render-extracted snapshots) skip the 16-byte cost and avoid pulling in a generator dependency; `entt::view<StableId>` enumerates only authoring entities. |
| Mandatory on every entity (auto-generated by bootstrap) | Rejected | Forces every transient entity to carry a 128-bit zero or a generated UUID; couples ECS bootstrap to an RNG/seed source; expands migration churn for HARDEN-060/061/062-shaped bootstrap callers that don't need identity durability. |
| Extension of `entt::entity` itself (e.g., custom registry storage) | Rejected | Tying stable identity to the `entt` integer would force every consumer through ECS; the value type belongs to authoring/serialization, not entity storage. |

### Decision 3 — Scene-local reference semantics

**Pick: ECS owns only the value type + equality/hash; runtime/editor owns
reference resolution and serialization.**

ECS exposes `StableId` (value type), `kInvalidStableId`, `IsValid`,
equality, `operator<=>`, and `StableIdHash`. ECS does **not** ship a
`StableId → entt::entity` lookup sidecar in slice 1 — that helper
(`Runtime::StableIdRegistry` or equivalent) belongs in `runtime/` and is
opened only when a concrete serializer/selection consumer asks for it
(deferred to HARDEN-068-Impl-B).

Rationale: keeps the ECS layer-clean (no scene-graph mutation observer);
lets runtime/editor decide between scene-local map, prefab-aware
multi-scene resolver, or stream-time lazy resolution; matches the
existing pattern where `runtime/` owns the cross-cutting maps
(`Runtime.RenderExtractionCache`, `Runtime.ProceduralGeometryCache`).

Hierarchy, selection caches, and serialized cross-entity references
store `StableId` (when durability is needed) or `entt::entity` (when
the reference is in-process only). Mixing the two is allowed; the rule
is that anything persisted to disk or sent across hot-reload must store
`StableId`, not `entt::entity`.

### Decision 4 — MetaData augmentation vs separate components

**Pick: separate `Extrinsic.ECS.Component.StableId`; do not extend `MetaData`.**

`MetaData::EntityName` stays the bootstrap naming contract (cheap,
common, present on every default entity). `StableId` is a separate
sparse component opted into per Decision 2. Future serialization
metadata (`SerializationHints`, `SceneSource`, prefab provenance)
likewise live in their own focused components, each opened by a future
slice with a concrete consumer.

| Option | Verdict | Rationale |
|---|---|---|
| Separate `StableId` component | **Pick** | Sparse storage; clean opt-in; `entt::view<StableId>` enumerates only authoring entities; future serialization-adjacent metadata fits the same one-component-per-concept pattern. |
| Extend `MetaData` with optional fields | Rejected | Forces every default-bootstrapped entity to carry zeros for unused fields; couples the cheap `EntityName` carrier to durable-identity semantics; would migrate every call site that constructs `MetaData`. |
| Single `EntityIdentity` component bundling name + StableId + provenance | Rejected | Conflates the "always present, cheap" name from the "rarely present, durable" identity; obscures sparse-vs-dense storage tradeoffs; complicates default bootstrap. |

### Decision 5 — `AssetInstance::Source::AssetId` typing

**Pick: defer — keep raw `std::uint32_t`; cross-link the existing
`HARDEN-062` decision; no widening of the `ecs → assets` dependency
contract in this slice or in any HARDEN-068 implementation child.**

The current `ECS.Component.AssetInstance::Source::AssetId` is a raw
`std::uint32_t` engine-wide stable ID by deliberate `HARDEN-062`
decision (see `src/ecs/README.md` "Asset references on components"
section). Promoting it to the typed `Extrinsic::Assets::AssetId`
would create an `ecs → assets` dependency edge that the layering
contract (`tools/repo/check_layering.py::ALLOWED_DEPS`) and
`AGENTS.md §2` do not currently permit.

HARDEN-068 explicitly does **not** widen that contract. If typed asset
handles are ever desired, that becomes a separate architecture task
(prefix `ARCH-` per the `tasks/README.md` lifecycle conventions) that
opens after a concrete consumer demands it; the architecture task owns
the layering allowlist change and the contract-test update, and only
then can an `ecs → assets` edge be allowed.

This is recorded as a Forbidden change in this slice (see below) so a
later slice cannot quietly widen the contract under the HARDEN-068 ID.

## Implementation children (identified, not opened)

- [x] **HARDEN-068-Impl-A — `StableId` payload module + tests.** Landed
  on `claude/setup-agentic-workflow-PaRh5` as this task's slice 2.
  Added `src/ecs/Components/ECS.Component.StableId.cppm`,
  `tests/unit/ecs/Test.ECS.StableIdentity.cpp`, and the targeted
  `StableIdPayloadStaysCpuOnly` contract case in
  `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`. CPU-only.
  See slice 2 in the slice plan.
- [ ] **HARDEN-068-Impl-B — Generator + optional runtime lookup.**
  Adds deterministic and random `StableId` generation helpers
  (CPU-only in ECS, no global RNG state) and, if a consumer demands
  it, `Runtime::StableIdRegistry` under `runtime/` (NOT `ecs/`).
  Open only when a concrete consumer task exists.
- [ ] **HARDEN-068-Impl-C — Adjacent authoring metadata (optional).**
  Open only when a scene serializer or prefab ingest task explicitly
  demands separate `SerializationHints` / `SceneSource` /
  prefab-provenance components. Each lands as its own focused
  component; `MetaData` is not extended.

## Goal

- Define promoted ECS components and policies for stable entity
  identity, scene-local references, and serialization-facing metadata
  without coupling ECS to runtime scene serialization or live asset
  services.

## Non-goals

- No scene serializer implementation.
- No prefab/editor UI system.
- No live `AssetService`, runtime scene-manager, graphics/RHI, or
  platform dependencies in ECS.
- No immediate migration of `AssetInstance::Source::AssetId` to typed
  asset handles (Decision 5; explicitly deferred behind a future
  `ARCH-*` task).
- No `StableId` generator inside ECS in slice 1 (deferred to
  HARDEN-068-Impl-B); ECS owns only the value type.
- No `StableId → entt::entity` lookup sidecar inside ECS in slice 1
  (deferred to HARDEN-068-Impl-B; lookup belongs in `runtime/`).

## Context

- Owner/layer: `ecs`, with runtime/assets participating in ownership
  decisions where serialization and asset identity cross layer
  boundaries.
- Source review:
  [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md)
  ("Stable identity and serialization metadata" row).
- Current `MetaData` stores only `EntityName`
  (`src/ecs/Components/ECS.Component.MetaData.cppm`), and
  `AssetInstance::Source::AssetId` is a raw `std::uint32_t` by
  deliberate decision from `HARDEN-062`
  (`src/ecs/README.md`, "Asset references on components" section).
- Scene save/load, undo/redo, prefab references, hot reload, and
  external references need stable identity distinct from volatile
  `entt::entity` values.
- Runtime should own serialization mechanics, but ECS can own
  CPU-only identity and metadata value types.
- The existing `Extrinsic.Core.StrongHandle` exported-hasher pattern
  (`src/core/Core.StrongHandle.cppm`) is the template for the
  module-boundary-safe hasher that the `StableId` payload will mirror
  in HARDEN-068-Impl-A.

## Required changes

- [x] Decide the stable entity identity shape (Decision 1: 128-bit
  UUID-shaped `StableId`).
- [x] Define whether stable IDs are mandatory on all scene entities
  or optional authoring metadata (Decision 2: optional sparse
  component).
- [x] Define scene-local reference semantics for hierarchy, selection
  caches, serialized references, and future prefab/source provenance
  (Decision 3: ECS owns the value type; runtime/editor owns reference
  resolution and any `StableId → entt::entity` sidecar).
- [x] Decide whether additional metadata belongs in `MetaData` or
  separate components such as `StableId`, `SceneSource`, or
  `SerializationHints` (Decision 4: separate components; `MetaData`
  remains the bootstrap naming contract).
- [x] Revisit the `AssetInstance::Source::AssetId` typing decision
  from `HARDEN-062`; if typed handles are desired, create/update a
  separate architecture task to widen `ecs` dependencies before
  implementation (Decision 5: defer; do not widen the `ecs → assets`
  contract under HARDEN-068).
- [x] Keep serializer IO and runtime scene-manager behavior outside
  ECS (recorded in Decision 3 and the Forbidden changes list).
- [x] Add `Extrinsic.ECS.Component.StableId` module (slice 2 /
  HARDEN-068-Impl-A; `src/ecs/Components/ECS.Component.StableId.cppm`).
- [x] Add `tests/unit/ecs/Test.ECS.StableIdentity.cpp` (slice 2 /
  HARDEN-068-Impl-A).

## Tests

- [x] Slice 1: docs/structural validators only (see Verification
  below). No new C++ tests in this slice — nothing compilable changes.
- [x] Slice 2 (HARDEN-068-Impl-A): added
  `tests/unit/ecs/Test.ECS.StableIdentity.cpp` covering default
  construction, sentinel equality, `operator<=>` lexicographic
  ordering, hashability across the module boundary
  (`std::unordered_map<StableId, T, StableIdHash>`), trivial-copy /
  standard-layout / 16-byte invariants, and explicit-bit-pattern
  assertions on `kInvalidStableId`; label `unit;ecs`.
- [x] Slice 2 (HARDEN-068-Impl-A): extended
  `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` with the
  targeted `StableIdPayloadStaysCpuOnly` case so the new
  `Extrinsic.ECS.Component.StableId` module is included in the
  prohibited-import sweep. The directory-recursive sweep
  (`EcsSourcesDoNotImportHigherLayerModules`) auto-includes the new
  file for the shared `ecs -> {core, geometry}` import constraint;
  the targeted case additionally forbids `entt` inside the payload
  because `entt` is legal elsewhere in `src/ecs` (e.g. `DirtyTags`
  stamping helpers).

## Docs

- [x] Update `src/ecs/README.md` with the stable identity / metadata
  policy and a forward pointer to the Impl-A slice (slice 1).
- [x] Update `src/ecs/Components/README.md` with the StableId
  ownership decision and the "MetaData stays the naming contract"
  rule (slice 1).
- [x] Update [`docs/architecture/ecs.md`](../../docs/architecture/ecs.md)
  with a "Stable identity and scene metadata" section that records
  the five HARDEN-068 decisions at the architecture level (slice 1).
- [x] Update the `ecs` row of
  [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md)
  to reference the recorded HARDEN-068 decisions and to note that
  the `StableId` payload module remains a slice-2 (Impl-A)
  deliverable (slice 1).
- [x] Regenerate
  [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md)
  (slice 2: 435 modules; new `Extrinsic.ECS.Component.StableId` row).

## Acceptance criteria

- [x] ECS has a documented stable identity contract suitable for
  runtime serialization and external references (the five decisions
  above plus the docs updates).
- [x] Identity metadata remains CPU-only and layer-clean (Decisions
  1–5 plus the Forbidden changes list).
- [x] The raw-vs-typed asset ID decision is either unchanged and
  documented or moved into an explicit architecture follow-up before
  code depends on it (Decision 5: unchanged; explicitly deferred to a
  future `ARCH-*` task that opens only when a consumer demands it).
- [x] `Extrinsic.ECS.Component.StableId` exists with payload tests
  (slice 2 / HARDEN-068-Impl-A:
  `src/ecs/Components/ECS.Component.StableId.cppm` +
  `tests/unit/ecs/Test.ECS.StableIdentity.cpp` + the targeted
  `StableIdPayloadStaysCpuOnly` contract case).

## Verification

### Verification (slice 1, docs/planning only)

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

No C++ or CMake changes in slice 1; the pinned `clang-20` CPU gate
is not exercised because nothing compilable changes. The validators
above are sufficient verification for a planning-only slice (mirrors
the `GRAPHICS-029..034` and `ASSETIO-001` precedent).

### Verification (slice 2, HARDEN-068-Impl-A)

Default CPU gate (pinned `clang-20`, runs in the PR's `pr-fast`
workflow row; deferred locally because the remote-execution host only
has Clang 18 — same precedent as HARDEN-065 slice 2 and GRAPHICS-033D):

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
```

Local structural/docs validators (run this session):

```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes

- Implementing runtime scene serialization or editor prefab workflows
  in ECS.
- Adding live asset services, runtime scene managers, graphics/RHI
  handles, or GPU residency state to ECS metadata.
- Widening the `ecs` dependency contract to `assets` without a
  separate architecture decision and the matching allowlist/test
  updates (Decision 5).
- Treating `entt::entity` values as stable serialized identifiers.
- Adding a `StableId` generator or `StableId → entt::entity` lookup
  sidecar inside `src/ecs/` in any HARDEN-068 child (Decisions 2 and
  3 — generator helpers may live in ECS as pure functions taking a
  caller-provided seed/engine; runtime-side lookup sidecars live in
  `src/runtime/`).
- Extending `MetaData` with new fields under HARDEN-068 (Decision 4;
  future authoring-metadata components are separate components).

## Next verification step

- Slice 2 / HARDEN-068-Impl-A awaits the pinned-`clang-20` CI run (the
  `pr-fast` row that invokes `cmake --preset ci` +
  `IntrinsicECSTests` (label `ecs`, includes the new
  `Test.ECS.StableIdentity.*` cases) + `IntrinsicEcsContractTests`
  (label `contract`, includes the new
  `EcsLayeringBoundaries.StableIdPayloadStaysCpuOnly` case)). Once
  green, retire to
  `tasks/done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`
  with the completion date and PR/commit reference. No further
  HARDEN-068 slices are auto-promoted: HARDEN-068-Impl-B (generator
  + optional `Runtime::StableIdRegistry` sidecar) and
  HARDEN-068-Impl-C (adjacent authoring metadata) open from the
  backlog only when a concrete consumer demands them.

## Maturity

- Target: `CPUContracted` once HARDEN-068-Impl-A passes the pinned
  `clang-20` CPU gate in CI (CPU-only `unit;ecs` payload coverage on
  the new `Extrinsic.ECS.Component.StableId` module). `Operational`
  is not in scope for HARDEN-068; that requires a serializer /
  prefab / editor consumer task that explicitly drives the payload
  end-to-end.
- Slice 1 closed `Scaffolded → Scaffolded` for the identity-contract
  decision record. Slice 2 / HARDEN-068-Impl-A in this branch
  promotes the payload module from `Scaffolded` (designed, not
  coded) to `CPUContracted` once the pinned `clang-20` CPU gate in
  CI confirms the new module compiles, the `unit;ecs` payload tests
  pass, and the `StableIdPayloadStaysCpuOnly` contract case passes.
