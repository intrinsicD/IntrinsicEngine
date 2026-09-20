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

## 2026-09-20 implementation checkpoint

The first slice reuses the existing face-dual CPU solver through
`SegmentFaceFeatures`: one to three numeric channels, including a Vec2 or two
scalar fields, with vertex-to-face averaging or explicit face correspondence.
Config schema 2 persists ordered full feature refs. The panel uses the same
feature predicate and channel budget; an empty feature list explicitly chooses
computed curvature. There is no schema-1 compatibility reader under AGENTS §5.
Curvature-specific curve/patch methods reject supplied generic features.

The canonical runtime property-types owner now provides component counts and
domain-specific structural-property protection. Bilateral, descriptors and
point-property operations reuse it instead of separate reserved-name lists.
Curvature, segmentation, parameterization and geodesics no longer require name
prefixes. The topology walker counts vertex slots independently of a default
position property, so parameterization accepts a bound renamed position field
when `v:position` is absent. Genuine correspondence and output ownership stay
required. Numeric adapters remain private compiled runtime implementation;
no per-frame value scan or extra exported implementation body was introduced.

Review: Claude (`fable` CLI alias) performed planning and two fixed-diff reviews,
then reviewed the final topology correction read-only. Codex Sol medium handled
the binding audit/name gates; Sol high handled the bounded generic kernel in a
separate worktree. Integration found and fixed a missing include, the hidden
canonical-position dependency, and an old test that incorrectly reserved a
halfedge name on vertex storage. Claude's structural-output and UI-budget
findings were fixed. Its suggested schema migration was rejected under AGENTS
§5; repeated input channels remain valid regularized-GMM inputs.

Architecture review: public additions expose geometry spans and runtime property
refs within their existing layers; no service/interface or new target is added.
Existing command/history owns publication. The new controls serialize through
config and use the shared predicate. Clean-workshop rows 1–3 pass (imports,
links, public types); rows 4–8 are n/a (no renderer/recipe, maturity closure or
temporary layer exception). Scope/tests/docs sweep covers this task's explicit
cross-method intent. No compilation-time or numerical performance gain is claimed. Against
`2bc24687d`, the 20 changed production files total 19,211 → 19,626 lines
(+415): new feature binding adds code, while shared structural checks remove
three duplicated lists. This is capability/correctness work, not a net LOC cut.

Additional open verification/cleanup points:

- Numeric scalar twin coverage for Bool/Int32/UInt32, a runtime unused/deleted-slot
  non-finite fixture, and direct interactive picker budget coverage. Existing
  kernel tests cover inactive/deleted samples and config tests cover over-budget
  mixed widths; these do not substitute for the remaining UI/runtime cases.
- `MeshSurfaceTopologyStatus::MissingPositions` now describes a missing vertex
  source on this topology-only path; clarify the old status name when auditing
  its public consumers. Geometry extraction separately checks actual positions.
- Float working arithmetic can round intermediate means, including sums of
  exactly representable UInt64 inputs; only input conversion has the lossless
  integer check. Do not describe averaging or GMM arithmetic as exact.
- Sanitizer and GPU gates have not been run for this slice. No GPU behavior or
  interactive new-feature-picker usability result is claimed.

The broader acceptance items above remain open. Start a fresh session from this
note before implementing the exhaustive family matrix and remaining numeric
adapters; do not infer engine-wide completion from this checkpoint.

Verification so far: Clang 23 `ci` configure and `IntrinsicTests` build pass;
384/384 focused CPU tests pass after the two test-driven corrections. Strict
layering, test layout, task policy and docs-sync checks pass; links and root
hygiene pass. Module inventory regenerated (429 modules, byte-identical).
Touched interface/header source-documentation audit has zero errors; its 15
review prompts describe existing lifetime/numerical contracts, including a
false-positive history prompt on the deterministic EM iteration counter.
Full CPU result is recorded below after completion. Local logs/review output:
`/tmp/intrinsic-property-binding/` (ephemeral, not repository evidence custody).

The first full CPU run completed 4,861 tests with five failures in old
reserved-output expectations (outlier, KDE, keypoint, density-weight, spacing).
Those tests reserved halfedge/edge/vertex names on unrelated domains. Updated
fixtures now assert both genuine structural rejection and unrelated-name
acceptance; all 295 affected UI/runtime tests pass after rebuilding. The picker
also now labels/scopes options by domain so equal-named vertex and face fields
remain separately selectable. The full CPU selector is rerun on this final code.

Claude's final follow-up reviewed cross-domain picker identity and the five
corrected test families, reporting no blockers. Shared domain truth-table
coverage is retained; additional method-specific cases for point-cloud
`v:connectivity`/`v:halfedge` and keypoint `v:halfedge` can extend that matrix.

Next audit entry points (confirm current code before editing):

- `src/runtime/Visualization/Runtime.VisualizationRecipes.cpp` and
  `src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp`:
  source-name selection of normal encoding versus direct color.
- `src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp`: matching
  name-based normal interpretation in property bake preparation.
- `src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp`: catalog
  visibility/internal-property exceptions; distinguish structural ownership from
  accidental filtering of valid sample fields.
- `src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp`:
  explicit corner-UV relationship instead of canonical-name retirement.
- `src/runtime/Modules/Geodesics/Runtime.GeodesicsConfig.cpp`: replace legacy
  string-only slots with full refs while retaining vertex correspondence.

Start the next slice by inventorying each input/output binder's accepted shape,
actual storage kind, correspondence, catalog predicate, codec and executor. Do
not mechanically replace every exact-kind test: checked conversion/publication
must first own the storage and failure contract.

The second full run exposed one interactive panel test relying on the existing
single-domain picker row IDs. Domain ID scoping is now conditional on the new
cross-domain predicate; ordinary position pickers retain their existing IDs.
The correction is verified with the processing-panel suite and the final full
CPU rerun below. The discovered failures were introduced expectation/identity
changes in this slice, not environmental failures or quarantined tests.

Final verification: the rebuilt `IntrinsicTests` target passes. Final default
CPU selector exits 0: 4,861 registered tests, 4,860 passed, zero failed, one
capability skip (`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`), 160.72 s.
All 26 `SandboxProcessingPanels` tests pass after the picker identity correction.
Full log: `/tmp/intrinsic-property-binding/full-cpu-verified.log`.
No test labels, gates or quarantine rules were weakened. RUNTIME-270 remains
active for the unchecked engine-wide items; UI-037 remains active for readiness.

## 2026-09-20 — Mesh-field property compilation locality

Operator-directed duplication/compile-locality continuation from `beef1cfb8`,
with Claude Fable review and one checkout writer. This is a bounded preparation
slice; none of the remaining engine-wide binding acceptance items closes here.

Reuse decision: curvature/segmentation and geodesics already share
`CaptureCurvatureProperty` / `ApplyCurvatureProperty`, but both execution units
compiled their template bodies. Keep their private declarations in
`Runtime.MeshFieldOperations.Properties.hpp`; put unchanged bodies in the
existing curvature execution owner and explicitly instantiate only the shared
`double` and `bool` variants there. Other field types remain local instantiations.
No new file, target, public interface, import or layer edge is introduced. Keep
the light config facade free of property implementation dependencies.

Claude's planning pass questioned the likely timing benefit; the fixed-diff
review found no blockers. Its header contract clarification was applied. The
configured CLI uses `--model fable`; the version suffix is not independently
attested. No Codex subagents were needed. `nm -C` on the geodesics object shows
four undefined capture/apply references, resolved by the curvature object at
link time, rather than duplicate emitted implementations.

The two production files total 2,866 → 2,892 physical lines (+26), due to the
private declaration/instantiation bookkeeping. This removes repeated template
compilation, not source lines. No elapsed compile-time improvement is claimed;
matched timing remains open. Exact type/count validation, absent-property
removal, diagnostics, history and topology ownership are unchanged.

Verification logs and read-only review are under
`/tmp/intrinsic-mesh-field-properties/` (ephemeral). Clang 23 `ci` configure,
focused build and all 120 focused tests pass. Source-documentation audit has
zero errors; the existing large coherent family unit and the necessary
absence/restore contract comment are the two review prompts. Full final results
are recorded below after completion.

### Open points after this slice

- [ ] Complete the exhaustive method/config/UI input/output family matrix.
- [ ] Extend checked numeric input/output adapters with target storage and
      alias/ownership guarantees; current output slots still require fixed kinds.
- [ ] Replace name-derived normal/color interpretation with explicit config in
      visualization, surface appearance and texture baking.
- [ ] Migrate geodesics strings to canonical refs and define explicit
      parameterization corner-UV retirement relationships.
- [ ] Decide and implement supported feature widths beyond 1–3; keep Vec4 rejected
      until supported without truncation.
- [ ] Complete UI-037 readiness, stale-source/invalidation and no-per-frame-scan
      coverage for bound features.
- [ ] Add the previously identified Bool/Int32/UInt32 scalar-twin, runtime
      inactive-slot nonfinite and interactive picker-budget cases.
- [ ] Clarify topology-only `MissingPositions` naming and extend the optional
      method-specific structural-name truth-table cases identified above.
- [ ] Run relevant sanitizer/GPU gates and interactive picker usability checks
      before making claims in those evidence classes.
- [ ] Measure compile-time benefit under a matched build-task baseline before
      treating this relocation as a speedup; reassess if measurements show no value.

Next bounded binding work: inventory and select one checked numeric adapter
family, using the entry points above. Do not carry this template relocation as
proof of numeric interoperability or engine-wide completion.

Final slice verification: rebuilt `IntrinsicTests` passes after the header comment
fix. The default CPU selector exits 0: 4,861 selected, 4,860 passed, zero failures,
one expected `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability
skip, 160.57 seconds. No sanitizer or GPU execution. Strict layering, task policy,
test layout and explicit-file docs-sync checks pass; links, session-brief
freshness, root hygiene and `git diff --check` pass. Existing geodesics tests cover
wrong output storage, no-change history, absent-output undo and redo; curvature
and segmentation tests exercise the other local field types. The moved bodies
are identical after whitespace normalization. Four-point review: one property
compilation intent, unchanged layer/public boundaries, passing existing behavior
coverage, synchronized ownership docs/task. Start a fresh session at this verified
checkpoint before the broader binding audit; all open points are listed above.

## 2026-09-20 — Remove duplicate mesh-field value comparisons

Operator-directed duplicate-code/compile-time continuation from `3f078e224`.
Reuse inspection found two private Vec3/Vec4 vector equality loops duplicating
`std::vector::operator==` plus GLM's component equality. Replaced all five callers
and deleted both routines. The installed GLM uses ordinary component equality:
NaN remains unequal and signed zeros equal. Do not substitute the bitwise
comparison owner in `Runtime.GeometryValueComparison.hpp`, whose contract differs.
Bindings remain excluded from these state comparisons, as before.

Production scope: only `Runtime.MeshFieldOperations.Curvature.cpp`, 2,873 → 2,832
lines (-41); no new helper, header, import, target, interface or dependency edge.
The new `ColorHistoryUsesNumericComponentEquality` regression exercises real
segmentation publication, undo, signed-zero-equivalent redo, stale undo for NaN
in every color component, unchanged history revision on rejection, and recovery.
Existing curvature tests retain Vec3 direction publication/no-change/history
coverage. No algorithm or public binding behavior changes.

Claude CLI (`--model fable`; version suffix not independently attested) checked
the planning proposal. Removing repeated config imports was rejected as a
compile-time optimization because the module interface already imports those
owners. This slice reduces duplicate source; no compile-time speedup is claimed.
Fixed-diff review and verification results follow below. Logs are ephemeral under
`/tmp/intrinsic-field-equality/`. No Codex subagents were needed.

### Remaining open points

- [ ] Exhaustive method/config/UI binding family matrix (including owning edits).
- [ ] Checked numeric input/output adapters with target-storage, aliasing and
      structural-ownership guarantees; fixed-kind output slots remain.
- [ ] Explicit configured normal/color interpretation for visualization,
      appearance and texture baking instead of source-name inference.
- [ ] Canonical geodesics refs and explicit parameterization corner-UV retirement.
- [ ] Feature widths beyond 1–3; Vec4 stays rejected until supported.
- [ ] UI-037 stale-source, invalidation, prepared-frame readiness and bounded scans.
- [ ] Bool/Int32/UInt32 scalar-twin, inactive-slot nonfinite and interactive
      picker-budget coverage from the prior binding slice.
- [ ] Topology-only `MissingPositions` naming and optional structural-name cases.
- [ ] Relevant sanitizer/GPU and interactive usability verification before claims
      in those classes; this continuation verifies CPU behavior only.
- [ ] Matched compile-time measurement of the previous template relocation before
      treating it as a speedup; interface-inherited imports are not a new locality
      win. Select measured hotspots before further compilation-oriented changes.
- [ ] Optional Vec3 direction and edge-color exceptional-value history cases,
      including a captured NaN compared with itself; the new test covers face
      Vec4 colors with NaN edits against a finite snapshot.

Next bounded implementation: select one numeric adapter family from the remaining
binding matrix. Start a fresh session at this verified cleanup checkpoint before
that wider audit to avoid carrying discovery/review context into unrelated work.

Review: Claude's source review confirmed property-handle lifetime and found no
blockers; its temporary-diff read was denied, so a second review received the
complete fixed diff directly and also found no blockers. The installed GLM
comparison implementation was independently inspected. `<limits>` was already
included directly by the test. Optional direction/edge-color and captured-NaN
coverage is listed above rather than represented as completed.

Verification so far: Clang 23 `ci` configure and `IntrinsicTests` build pass;
all 200 focused tests pass. Strict layering, test layout, task policy and
explicit-file docs-sync pass; doc links, root hygiene and session-brief freshness
pass. Scope is one comparison cleanup; existing layer/public boundaries and
binding contracts are unchanged, so no architecture or inventory change is due.

Final CPU verification: 4,862 selected, 4,861 passed, zero failures, one expected
`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip; 161.77 s.
No sanitizer or GPU run. This is ordinary refactoring evidence, not a performance
or research claim; no ARA claim is introduced. All remaining points are above.
