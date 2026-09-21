---
id: RUNTIME-270
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive implementation; source diff, tests, review and checkpoints retain evidence without unattended custody.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, method.engine-integration, runtime.processing-compilation-locality, repo.source-documentation]
---
# RUNTIME-270 — Property binding equivalence across methods

## Goal

Make all suitable properties interchangeable by numeric shape, independently of
name, prefix or producing method, across runtime, config/agent and UI bindings.
Expose supplied feature properties to segmentation instead of always recomputing
curvature. The operator explicitly broadened the previous UI-037 continuation
on 2026-09-20; this task owns the broader change, while UI-037 retains readiness.

## Acceptance criteria

- [x] Generic GMM segmentation accepts explicit mesh vertex/face numeric features;
      selected features are used, preserved, and never replaced with curvature.
- [x] One Vec2 and two scalar channels with equal values produce equal results;
      numeric storage conversion preserves representable values and rejects
      precision loss at conversion/non-finite samples. Subsequent kernel arithmetic
      and vertex averaging use floating-point arithmetic, not exact arithmetic.
      Unsupported dimensions fail explicitly.
- [x] Config file round-trip, agent apply, runtime preflight and UI selection use
      the same feature bindings and compatibility predicate. Computed curvature
      remains an explicit input choice; intrinsic feature curves retain their
      actual curvature requirements.
- [x] Remove arbitrary input name/prefix gates from curvature, segmentation,
      parameterization and geodesics; retain topology/correspondence, output
      aliasing and structural storage requirements. Regression tests use names
      unrelated to position, curvature or domain prefixes.
- [ ] Audit every method/config/UI binder against a family matrix, including
      point-set, graph-neighborhood, mesh-correspondence and owning topology edits.
      Sampled clean paths are not an exhaustive engine-wide compliance claim.
- [ ] Replace source-name-derived normal/color interpretation in visualization,
      surface appearance and texture baking with explicit recipe/config choices.
- [ ] Extend checked numeric adapters across remaining input and output bindings.
      A compatible output shape may require checked conversion into the declared
      target storage; never reinterpret existing storage, silently narrow values,
      or weaken output aliasing/ownership rules. Current fixed-kind output slots
      are not yet universal dimension-based publication.
- [ ] Migrate geodesics string slots to canonical property refs. Make
      parameterization corner-UV binding/retirement explicit: today it retires
      `h:texcoord` only when publishing `v:texcoord`, preserving unrelated corner
      UVs for custom output names. Do not replace this with unconditional deletion.
- [ ] Decide supported feature widths beyond the existing GMM's 1–3 channels;
      Vec4 is rejected until an implementation supports it, never truncated.
- [ ] Close stale-source, invalidation, no-per-frame-scan and prepared-frame
      readiness coverage with UI-037, including newly bound feature fields.
- [x] Full relevant tests, layering/docs checks and independent fixed-diff review
      pass; record remaining limitations without claiming universal completion.

- [x] Close the existing mesh-field/fixture compilation follow-up with matched
      clean and target-incremental measurements, explicit shared-owner costs,
      fixed-diff review and separate integration commits (C103/C104). This is a
      bounded development-cost result, not completion of the product matrix.

## Engine integration

| Surface | Required behavior / owner |
| --- | --- |
| Least-structured input | GMM consumes numeric face features; face-dual regularization genuinely needs mesh faces/adjacency. |
| Compatible entity sources | Vertex properties aggregate to their incident faces; face properties retain face correspondence. No property-name eligibility. |
| RuntimeModule | Existing mesh-field operations bind current properties and publish through existing history; no new service. |
| Config/agent | Serializable full property refs plus explicit computed-input selection; RUNTIME-270. |
| UI | Canonical catalog filtered by the same runtime feature predicate; RUNTIME-270. |
| Publication | Face/edge output properties preserve topology and unrelated input properties; existing history owner. |
| End-to-end tests | Type/name substitution, config round-trip, command outputs/history, invalid inputs and UI command routing; RUNTIME-270. |
| Readiness | Metadata now, deferred finite-input readiness where needed; UI-037 owns complete nonblocking closure. |

## Discovery and decisions

Reuse the existing CPU GMM/face-dual solver, curvature adapter, canonical
`GeometryPropertyRef`, property catalog and resolver. No new method/backend or
compatibility aliases. The numerical kernel never receives property names.
The source-label audit found unnecessary prefix gates in curvature,
parameterization and geodesics, and name-derived normal interpretation in
visualization/baking. Point-input catalogs and sampled executors already use
full property refs; mesh-correspondence and topology-changing domain restrictions
are legitimate and must stay. Geodesics' legacy string slots remain a canonical
ref migration follow-up; string names alone do not justify restricting names.

The operator was asked whether numeric storage kinds should interoperate.
Pending a different preference, use checked numeric conversion as the stated
implementation assumption; exact storage identity remains part of property
resolution and output mutation, not a substitute for feature shape.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|MeshCurvature|Geodesic|Parameterization|GeometryProperty|ProcessingCompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
```

## Current implementation and remaining scope

Generic GMM uses `SegmentFaceFeatures` with one to three ordered numeric channels,
including Vec2 or paired scalars. Vertex features average onto incident faces;
face features preserve correspondence. Config schema 2 stores full refs; an empty
feature list explicitly selects computed curvature. There is no schema-1 reader.
Curvature-specific methods retain their actual curvature requirements. Conversion
rejects integer precision loss, but averaging and GMM arithmetic are not exact.

The existing runtime property-types owner provides component counts and
per-domain structural-property protection. Curvature, segmentation,
parameterization and geodesics no longer gate inputs by name prefix. Private
compiled adapters retain correspondence, aliasing and publication ownership.

The unchecked acceptance criteria above are the authoritative remaining scope.
Additional coverage still owed: Bool/Int32/UInt32 scalar twins, unused/deleted-slot
nonfinite runtime inputs, interactive picker budgets, and optional Vec3 direction /
edge-color exceptional-value history cases. Clarify topology-only
`MeshSurfaceTopologyStatus::MissingPositions` while preserving position validation
at geometry extraction. GPU, sanitizer and interactive usability evidence remain
separate from CPU verification; no universal property-binding completion is claimed.

Start the binder audit at visualization recipes/actions and TextureBake normal
interpretation, EditorFeatureContextAdapters catalog filters, Parameterization
corner-UV retirement, and GeodesicsConfig string slots. Inventory accepted shape,
storage, correspondence, catalog predicate, codec and executor together; do not
mechanically weaken exact-kind checks before checked publication owns conversion.

## Closed development-cost batch — 2026-09-21

The operator explicitly prioritized duplicate-code and compilation-time closure
for this session. Close the previously attached batch here; future compilation
work must identify its own acceptance criterion and owner rather than extending
this property-binding task by default.

Existing `EditorFeatureTestContext.cpp` owns context/presentation, graph and
icosahedron fixture construction. Consumers reuse it; `MockRHI.cpp` owns device
construction/destruction. No new abstraction or translation unit. The ten-file
batch removes 262 net lines, preserving specialized fixture differences. Source
commit: `e804afc24`. C103 closes the earlier four-commit measurement obligation;
C104 closes the accumulated fixture batch. The exact ten-file tree matches local
snapshot `887415fb7e6df4c04caa154f06f64f04a422b731`.

See the [recent-locality report](../../ara/evidence/tables/runtime270_recent_locality_measurement.md)
and [fixture-batch report](../../ara/evidence/tables/runtime270_fixture_batch_measurement.md)
for raw results, source identities and all regressions. C104 runtime-contract target
medians: clean 428.676 → 426.548 s (negligible); Models edit 23.748 → 17.723 s;
Visualization edit 21.253 → 16.787 s; fixture header 67.902 → 58.503 s; mock header
53.364 → 35.166 s (11 → 8 compilers). Fixture implementation regresses
8.570 → 10.112 s. Clang 23, ci Debug Null/headless, four jobs, cache disabled,
ABBA n=2 per arm; scanning/linking and owner compilation included. Other executable
and full-engine costs remain unmeasured, not mandatory follow-up experiments.

SessionLifecycle dependency cleanup is separately committed as `8affd8dcc`.
Its code matches the previously reviewed after snapshot; no clean-build or
end-to-end incremental speedup is claimed. The direct compiler diagnostics excluded
scanning/linking and cannot extend C104. A future performance claim needs its own
matched target baseline. No remaining implementation or integration obligation for
these completed source changes.

BUG-204's consumer-probe fix is committed as `defaafeac` and retired at
[BUG-204](../done/BUG-204-compile-benchmark-header-probes.md). The unchanged predicate
is extracted for negative-path tests; retained measurement runners keep their
original byte identities. No accepted experiment was rerun.

Closure verification: canonical ci configure and IntrinsicTests build; 459 focused
CPU tests; 4,861 passed, zero failed and one expected GLFW/LSan
capability skip out of 4,862 selected (156.62 s). All 29 compile-tooling tests, 103 manifests and eight
retained canonical results pass. All 41 indexed evidence files match their hashes.
Strict layering, test layout, task policy, docs sync, claims and root hygiene pass;
relative links and session brief pass. Header documentation audit: zero errors;
its one existing comment explains the test-only live-dependency boundary.
Claude Fable 5.1 performed independent plan/fixed-diff review; the missing negative
probe tests were fixed. No sanitizer/GPU run or new performance claim this session.

Historical implementation notes are preserved in `e804afc24`; this current
summary replaces repeated chronological checklists without dropping open scope.
