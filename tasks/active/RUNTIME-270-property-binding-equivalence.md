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

## 2026-09-20 — Compile segmentation success evaluation out of line

Operator-directed duplicate-code/compilation continuation from `25fe9b152`.
Moved `EditorCurvatureSegmentationResult::Succeeded()` from the public mesh-field
interface into the existing matching implementation unit. The three-method
branching diagnostic predicate is ordinary compiled runtime implementation;
small sibling accessors stay in the interface. Reuse decision: use the existing
module implementation, with no new helper, file, target or dependency edge.
The body matches the baseline after whitespace normalization. Declaration,
`[[nodiscard]]`, `const noexcept`, records and imports are unchanged.

Production accounting across both touched files: 27 lines added, 26 removed,
net +1 (out-of-class definition spacing). This is implementation relocation,
not duplicate-code reduction or a measured compile-time speedup. Future edits
to this body can stay in the implementation file. The diagnostics types still
belong in the interface; no transitive-import reduction is claimed.

Claude CLI `--model fable` reviewed the plan and fixed diff with no blockers;
the exact 5.1 version suffix was not independently attested. Its final review
used loose inline/linkage wording: members defined inside a named module are
not implicitly inline, and exported members remain callable by importers.
The actual declaration/definition and link validation determine correctness.
No Codex subagents were needed for this bounded move.

The module inventory was regenerated and has no diff. Architecture documentation
now locates the predicate in the implementation. Source-documentation audit:
zero errors, three existing declaration-comment review prompts retained because
they explain admission versus execution, callback timing and synchronous history
publication. Layer/ownership review: existing runtime owner, no new exports,
imports, wiring, state, lifetime or failure paths. No new behavior test is needed
for an identical body; existing public operation tests exercise GMM, patches and
boundary diagnostics and verify importer linkage after the move.

Exploratory `compile_hotspots.py --build-dir build/ci --top 8` suggests examining
`Test.SandboxEditorVisualization.cpp`, `Test.SandboxEditorClusteringMethods.cpp`
and `Test.SandboxEditorMeshMethods.cpp` next. This mixed historical Ninja log has
13 unresolved obsolete outputs absent from the current compilation database;
it is discovery only, not a matched baseline or a performance result. Preserve
current test semantics and registration when evaluating their shared dependencies.
Logs and the fixed review diff are ephemeral in
`/tmp/intrinsic-segmentation-result-locality/`.

### All remaining open points at this checkpoint

- [ ] Exhaustive method/config/UI binding family matrix, including owning edits.
- [ ] Checked numeric input/output adapters with target storage, aliasing and
      structural ownership; output slots still require fixed kinds.
- [ ] Explicit configured normal/color interpretation for visualization,
      appearance and texture baking instead of source-name inference.
- [ ] Canonical geodesics refs and explicit parameterization corner-UV retirement.
- [ ] Feature widths beyond 1–3; keep Vec4 rejected until supported.
- [ ] UI-037 stale-source, invalidation, prepared-frame readiness and bounded scans.
- [ ] Bool/Int32/UInt32 scalar-twin, inactive-slot nonfinite and picker-budget tests.
- [ ] Topology-only `MissingPositions` naming and optional structural-name cases.
- [ ] Relevant sanitizer/GPU and interactive usability verification before claims
      in those classes; this continuation verifies CPU behavior only.
- [ ] Matched compilation measurements for this and the earlier template move;
      use a controlled source/toolchain/cache baseline, not mixed Ninja timings.
- [ ] Investigate the sandbox test compilation candidates above before selecting
      another locality change; no additional extraction is justified yet.
- [ ] Resolve the pre-existing ignored `[[nodiscard]]` result at
      `tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp:286` in a test cleanup.
- [ ] Optional Vec3 direction and edge-color exceptional-value history coverage,
      including a captured NaN compared with itself.

The broader binding task remains open. A fresh session after final verification
is a useful boundary before the distinct test-compilation investigation; resume
from this task note rather than loading previous conversation transcripts.

Final verification: Clang 23 `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests` pass. The focused selector
`CurvatureSegmentation|ProcessingCompilationLocality` passes all 70 tests.
The default full CPU selector passes 4,861 tests with zero failures and one
expected `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip
(4,862 selected; 159.51 s). Strict layering, test layout, task policy and
explicit-file docs-sync pass; doc links, root hygiene, session-brief freshness
and diff whitespace checks pass. No sanitizer or GPU run. The four-point sweep
confirms one implementation-locality intent, unchanged layer/API contracts,
existing behavior coverage and synchronized documentation. Ordinary refactoring
only: no ARA research or performance claim introduced.

## 2026-09-20 — Reuse compiled sandbox test fixture construction

Operator-directed duplicate-code/compilation continuation from `e2e1ae458`,
following the sandbox-test discovery leads above. The existing
`tests/support/EditorFeatureTestContext.cpp` / `EditorGeometry` owner now compiles
`MakeSelectable` and `AddPointCloudSource` for the Visualization, MeshMethods and
ClusteringMethods contract partitions. Clustering also reuses its existing
`SetPositions` and `SetTexcoords`. Eight former local bodies were checked equal
after whitespace normalization before replacement; the two relocated bodies
only expand existing namespace aliases. Signatures, nodiscard, property resize,
component construction and call sites retain their contracts.

The Clustering `AddTriangleMeshSource` deliberately stays local: it populates
through `PopulateFromMesh`, whereas the shared fixture builds raw property arrays.
A matching name is not proof of equivalent population semantics. Existing support
objects already link all affected executables; there are no CMake changes, new
files, production changes, public module changes or new engine dependencies.
Five implementation-only imports make component ownership explicit. The header
adds declarations, not bodies. Test-support README records the shared ownership.

Accounting over all five changed C++ files: 49 lines added, 85 removed, net -36;
production line delta is zero. No elapsed compilation speedup is claimed. Test
bodies and registration macros from the first TEST in each touched partition are
byte-identical to the baseline. Existing behavior tests validate fixture linkage
and behavior; no implementation-mirroring test was added. Source-documentation
scan of the support header/README has zero errors and one retained review prompt
for the explicit test-only live-context boundary comment.

### All remaining open points at this checkpoint

- [ ] Exhaustive method/config/UI binding family matrix, including owning edits.
- [ ] Checked numeric input/output adapters with target storage, aliasing and
      structural ownership; output slots still require fixed kinds.
- [ ] Explicit configured normal/color interpretation for visualization,
      appearance and texture baking instead of source-name inference.
- [ ] Canonical geodesics refs and explicit parameterization corner-UV retirement.
- [ ] Feature widths beyond 1–3; keep Vec4 rejected until supported.
- [ ] UI-037 stale-source, invalidation, prepared-frame readiness and bounded scans.
- [ ] Bool/Int32/UInt32 scalar-twin, inactive-slot nonfinite and picker-budget tests.
- [ ] Topology-only `MissingPositions` naming and optional structural-name cases.
- [ ] Relevant sanitizer/GPU and interactive usability verification before claims
      in those classes; this continuation verifies CPU behavior only.
- [ ] Matched compilation measurements for the fixture and earlier locality moves;
      use controlled source/toolchain/cache baselines, not mixed Ninja timings.
- [ ] Review the matching `MakeSelectable` / `AddPointCloudSource` pair in
      `Test.SandboxEditorModels.cpp` for adoption of the compiled owner. Other
      selection-test helpers need their own component-contract comparison.
- [ ] Investigate remaining sandbox test imports and repeated fixture bodies;
      do not replace the Clustering triangle without population-equivalence proof.
- [ ] Resolve the pre-existing ignored `[[nodiscard]]` result in
      `AddDenoiseAllBoundaryMeshSource` in `Test.SandboxEditorMeshMethods.cpp`.
- [ ] Optional Vec3 direction and edge-color exceptional-value history coverage,
      including a captured NaN compared with itself.

The task remains open. No Codex subagents were needed for this small shared-owner
change. Logs and the fixed review packet are ephemeral under
`/tmp/intrinsic-fixture-reuse/`. Resume from this latest checkpoint and its open
points instead of importing prior conversation transcripts.

Claude CLI `--model fable` reviewed the plan, fixed diff and a source/build-evidence
follow-up. Exact model suffix 5.1 was not independently attested. The fixed-diff
review found no correctness defect; conditional include, alias and support-object
linkage concerns were resolved against existing declarations and the successful
Clang 23 build. The README wrapping finding was fixed. Testing on older supported
compiler majors was not performed; the reviewer raised it as a possible CI check,
not an observed defect. The existing ignored-nodiscard warning remains listed
above. No approval or additional abstraction was needed.

Verification so far: `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests` pass with Clang 23;
`ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.'
-LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passes all 209 tests.
Strict layering, test-layout, task-policy and explicit-file docs-sync checks pass,
as do doc links, root hygiene, session-brief freshness and whitespace checks.
Touched-scope planning was captured; the canonical full CPU gate is the validation
route for this checkpoint. No sanitizer/GPU or performance evidence is added.

Final default CPU gate:
`ctest --test-dir build/ci --output-on-failure
-LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passes 4,861 tests,
zero failures, with one expected
`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip
(4,862 selected, 159.35 s). Final four-point sweep: one test-fixture reuse intent,
unchanged engine layering and behavior, original test bodies preserved, compiled
consumer linkage verified, and support/task documentation synchronized. No source
edits followed the successful build and tests. Research-manager epilogue: ordinary
refactoring only; no research event or ARA claim. This is a clean session boundary
before the next fixture/import investigation; the latest open-point list above
is the continuation context.

## 2026-09-20 — Recent compilation batch measured

The recent helper, equality, diagnostic and fixture batch now has a matched local ABBA measurement from `beef1cfb8` to `b3c18fd17`. See the [bounded measurement report](../../ara/evidence/tables/runtime270_recent_locality_measurement.md) and C103 for all timings, raw evidence and limits. This completes batch-level timing of those four commits; it does not isolate each commit or retire the remaining product work. BUG-204 records the header-probe accounting repair.


## 2026-09-20 — Measured-hotspot fixture continuation

The operator again requested duplication and compile-time work with Claude,
explicitly continuing this track alongside the standing Framework24 priority.
Selection uses the recent matched report above and a fresh Clang 23 time trace
of `Test.SandboxEditorMeshMethods.cpp`. The trace identifies template-heavy
EnTT/property construction in this slow translation unit. It does not establish
an elapsed-time improvement or a clean-build claim. The direct/queued-command
template itself has small instantiation events in this trace; it is not the next
priority simply because its body looks large.

Reuse decision: compile the identical `MakeContext` bodies from five sandbox
partitions and the identical presentation recipe/state builders from three
partitions in the existing `EditorFeatureTestContext.cpp` owner. The two matching
presentation attachment bodies use that owner too. Models now adopts the
existing compiled selectable/point-source builders. All copied bodies were
compared after whitespace/alias normalization before editing; every partition's
TEST section was byte-identical in the first iteration. A second iteration
replaces the same two-emplace setup sequence in two Visualization tests with
`AttachGeometryPresentation`; assertions and remaining test logic are unchanged.
Defaults, borrowed pointers, component insertion
order, and returned owned recipe/state values stay unchanged. Existing support
objects already link the consumers; no new source, target, engine API or layer
edge is introduced. Distinct graph/triangle population contracts remain local.

Seven C++ files: 127 additions, 325 removals, net -198 lines. Production-source
line delta is zero. The shared header adds declarations and the owning
GeometryPresentation import, not template bodies. Support README describes the
owner. Local diagnostic/review/build logs are in
`/tmp/intrinsic-compile-next/`; these are ephemeral development records, not
claim-grade benchmark evidence. Existing dirty benchmark/ARA files predate this
slice and are preserved.

### Remaining open points (2026-09-20 checkpoint)

- [ ] Exhaustive method/config/UI binding family matrix, including owning edits.
- [ ] Checked numeric input/output adapters preserving target storage, aliasing
      and structural ownership; output slots still require fixed kinds.
- [ ] Explicit configured normal/color interpretation in visualization,
      appearance and texture baking, replacing source-name inference.
- [ ] Canonical geodesics refs and explicit parameterization corner-UV retirement.
- [ ] Feature widths beyond 1–3; retain explicit Vec4 rejection until supported.
- [ ] UI-037 stale-source, invalidation, prepared-frame readiness and bounded scans.
- [ ] Bool/Int32/UInt32 scalar twins, inactive-slot nonfinite runtime cases and
      direct picker-budget tests.
- [ ] Topology-only MissingPositions naming and optional structural-name cases.
- [ ] Relevant sanitizer/GPU and interactive usability verification before
      claims in those evidence classes.
- [ ] Matched compilation measurement for this new fixture slice, including the
      shared-owner cost, consumer edits, header invalidation and clean builds.
      The previous four-commit batch is already measured; do not repeat it.
- [ ] Remaining sandbox imports and repeated fixture bodies: compare matching
      graph/icosahedron candidates before adoption; retain Clustering's different
      triangle/graph population semantics. Rank future work with fresh traces.
- [ ] Resolve the pre-existing ignored nodiscard result in
      AddDenoiseAllBoundaryMeshSource; it remains a visible build warning.
- [ ] Optional Vec3 direction and edge-color exceptional-value history coverage,
      including captured NaN compared with itself.

Models selectable/point-source adoption is complete and is no longer an open
point. The broader task remains open. Prefer a fresh session at the verified
checkpoint before another compilation experiment or family-wide binding audit.

Review and verification: Claude CLI `--model fable` reviewed the plan, first
fixed diff and final Visualization substitution, reporting no correctness
blockers. The CLI alias does not independently attest a 5.1 model suffix. No
Codex subagent was needed. Review comments about forced header inclusion were
checked against the tree: callers include the header normally; no forced-include
assumption is needed for the successful linkage. Optional cosmetic suggestions
were not mixed into this extraction. The README ownership suggestion is applied.

Clang 23 `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests -j 4` pass, including the final
iteration rebuild. First-iteration focused selector
`ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.|EditorCompilationLocality.TestContext' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
passes 210/210. Strict layering, test-layout, task-policy, explicit-file docs-sync,
relative links, root hygiene, session-brief freshness and whitespace checks pass.
The support-header/README source-documentation audit has zero errors and one
retained review prompt for the necessary test-only live-context boundary comment.
A final source comparison confirms all assertions and test logic unchanged,
apart from the two reviewed fixture setup substitutions. No sanitizer or GPU
execution was performed. Full CPU result follows below.

Final CPU gate completed on 2026-09-21: the default exclusion-only selector
passes 4,861 tests, zero failures, with one expected
`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip
(4,862 selected; 158.39 seconds). No source edits followed the successful final
build and full suite. Four-point sweep: one fixture-compilation intent; unchanged
engine layering; existing behavior tests plus boundary checks pass; owner/task
documentation synchronized. Research-manager epilogue: ordinary refactoring,
no new research claim or ARA mutation. Changes remain uncommitted alongside the
pre-existing measurement work. This is the recommended fresh-session boundary;
resume from the consolidated open points above.


## 2026-09-21 — Graph and icosahedron fixture continuation

The operator explicitly continued measured compilation and duplicate-code work
with Claude. The checkout already contained the previous verified fixture slice
and unrelated measurement/ARA changes; those are preserved. This session's
baseline is that dirty source, not HEAD. Existing matched batch timings identify
these sandbox partitions as slow consumers; fresh serial Clang 23 time traces
also show EnTT render-component instantiation work. The small extraction is not
presented as a clean-build optimization. This session changes six C++ files:
41 added and 62 removed lines (net -21), including the compiled owner and
header; production-source delta is zero.

Reuse decision: Models/Visualization have token-identical fixed three-vertex,
two-edge graph fixtures; MeshMethods/Clustering have token-identical icosahedron
fixtures. Both pairs now use compiled functions in the existing
`tests/support/EditorFeatureTestContext.cpp` owner with declarations in its header.
Population order, `emplace` versus `emplace_or_replace`, returned ownership and
the MeshMethods default function-pointer argument are unchanged. Graph/mesh/
population imports are implementation-only at the shared owner. Models and
Visualization remove six unneeded direct imports and two unused aliases each.
Clustering's variable-position graph (no RenderPoints) and its distinct triangle
remain local; its unused mesh-builder import is removed after Claude review. No new file, target, abstraction or engine dependency is introduced.

The boundary-mesh fixture now asserts `AddTriangle(...).has_value()` instead of
ignoring its nodiscard result. All four partitions' TEST sections are byte-identical
to the session baseline. The support README records shared fixture ownership.
Claude CLI `--model fable --effort medium` reviewed the plan and fixed diff,
reporting no correctness blockers; the exact 5.1 suffix is not independently
attested. No Codex subagent was needed for this bounded extraction.

Profiling is diagnostic only: serial direct compiler invocations use the current
`ci` commands, cache bypass and temporary outputs, with one before/after sample.
The consumer observations are close to baseline and do not establish a speedup.
The initial shared-owner after attempt used an outdated module map and failed at
its new import. The canonical build regenerated the map and the owner retry
passed. The shared owner became more expensive in this single diagnostic sample;
there is no evidence of an overall saving. The failed attempt is invalid
profiling data, not a source or CI regression. Logs, snapshots,
traces and Claude packets are under `/tmp/intrinsic-compile-graph/` and remain
ephemeral; no new benchmark claim or ARA result is introduced.

### Remaining open points at this checkpoint

- [ ] Exhaustive method/config/UI binding family matrix, including owning edits.
- [ ] Checked numeric input/output adapters preserving target storage, aliasing
      and structural ownership; output slots still require fixed kinds.
- [ ] Explicit configured normal/color interpretation in visualization,
      appearance and texture baking, replacing source-name inference.
- [ ] Canonical geodesics refs and explicit parameterization corner-UV retirement.
- [ ] Feature widths beyond 1–3; retain explicit Vec4 rejection until supported.
- [ ] UI-037 stale-source, invalidation, prepared-frame readiness and bounded scans.
- [ ] Bool/Int32/UInt32 scalar twins, inactive-slot nonfinite runtime cases and
      direct picker-budget tests.
- [ ] Topology-only MissingPositions naming and optional structural-name cases.
- [ ] Relevant sanitizer/GPU and interactive usability verification before
      claims in those evidence classes.
- [ ] Matched compilation measurement of the uncommitted fixture slices together,
      including shared-owner cost, consumer edits, header invalidation and clean
      builds. The earlier four-commit batch is already measured; do not repeat it.
- [ ] Rank the remaining sandbox imports and template-instantiation owners using
      fresh traces and invalidation costs before another extraction. Matching
      graph/icosahedron consolidation is complete; preserve distinct Clustering
      graph/triangle semantics. Small helpers alone have not established a
      meaningful build-time saving.
- [ ] Optional Vec3 direction and edge-color exceptional-value history coverage,
      including captured NaN compared with itself.

The prior ignored-nodiscard open point is resolved. Broader product work remains
open. Prefer a fresh session after this slice's final verification, using this
list rather than loading prior transcripts. Verification completion follows.

Final verification for this checkpoint: Clang 23 `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests -j 4` pass, including the
post-review import-removal rebuild. The focused selector combining
`^SandboxEditorUi\.|EditorCompilationLocality.TestContext|CurvatureSegmentation|MeshCurvature|Geodesic|Parameterization|GeometryProperty|ProcessingCompilationLocality`
with the standard capability exclusions passes 400/400 tests. The default full
CPU gate from Verification passes 4,861 executed tests, zero failures and one
expected `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip
(4,862 selected; 159.38 seconds). No sanitizer/GPU execution is claimed.

Strict layering, test-layout, task-policy and explicit-file docs-sync pass;
relative links, root hygiene, session-brief freshness and whitespace checks pass.
Module-inventory regeneration makes no change. The source-documentation audit
of the support header and README has zero errors and one retained review prompt
for the existing test-only live-context boundary comment. Four-point sweep:
one test-fixture reuse intent, unchanged engine layering, preserved test bodies
and passing behavior/boundary coverage, synchronized owner/task documentation.
No C++ edits followed the successful final build/tests. Research-manager epilogue:
ordinary refactoring with development diagnostics only, no new research claim or
ARA mutation. Changes remain uncommitted alongside the preserved earlier work.

## 2026-09-21 — Remove unused dependencies from measured sandbox consumers

The operator explicitly continued duplication/compile-time work with Claude,
prioritizing actual slow compilation. The session baseline was the existing
uncommitted fixture and measurement work, which is preserved. This slice only
changes the Models and Visualization sandbox test partitions: 46 removed lines,
no added helper or production source. All helper and TEST bodies are byte-identical
to the session baseline.

The existing traces and fresh serial, cache-bypassing Clang 23 compilations
identify these consumers as expensive. `MockRHI.hpp` instantiated transfer-token
and byte-buffer container machinery in both although neither uses a mock.
Reuse inspection confirmed that `EditorFeatureTestContext.hpp` remains their
fixture owner; neither needs a replacement helper. Remove the unused mock and
progressive-Poisson headers, unused method/platform/component imports and
namespace aliases, plus Visualization's duplicate AssetWorkflowModule import.
Keep the distinct mock/reference users in MeshMethods and Clustering unchanged.

Claude CLI `--model fable --effort medium` reviewed the plan and fixed diff.
The exact 5.1 model suffix is not independently attested. A compiler check
caught `Runtime::JobToken` requiring `Runtime.JobService`, which Claude's plan
had missed; the direct import was restored before the successful final compile.
This failed experimental compile is resolved, not an outstanding gate failure.
Final corrected review and canonical verification are recorded below.

Before/after development traces confirm that the identified mock-related
container instantiations disappear. One-shot elapsed observations are exploratory,
not a matched benchmark or a general build-speed claim. No research claim is
introduced. Source snapshots, review packets, and canonical verification logs
are in `/tmp/intrinsic-compile-imports/`; diagnostic traces are in
`/tmp/intrinsic-compile-graph/imports-{before,after,corrected}/`. These paths are
ephemeral and are not durable performance evidence.

### Remaining open points (current consolidated list)

- [ ] Exhaustive method/config/UI binding family matrix, including owning edits.
- [ ] Checked numeric input/output adapters preserving target storage, aliasing
      and structural ownership; output slots still require fixed kinds.
- [ ] Explicit configured normal/color interpretation in visualization,
      appearance and texture baking, replacing source-name inference.
- [ ] Canonical geodesics refs and explicit parameterization corner-UV retirement.
- [ ] Feature widths beyond 1–3; retain explicit Vec4 rejection until supported.
- [ ] UI-037 stale-source, invalidation, prepared-frame readiness and bounded scans.
- [ ] Bool/Int32/UInt32 scalar twins, inactive-slot nonfinite runtime cases and
      direct picker-budget tests.
- [ ] Topology-only MissingPositions naming and optional structural-name cases.
- [ ] Relevant sanitizer/GPU and interactive usability verification before
      claims in those evidence classes.
- [x] Matched accumulated fixture/dependency batch for the runtime-contract target,
      including both owner edits, all five consumers, both headers and fresh builds;
      C104 and `ara/evidence/tables/runtime270_fixture_batch_measurement.md` retain
      all four ABBA samples. Do not repeat this or the earlier C103 batch.
- [ ] Full-engine/other-executable build costs remain unmeasured. Sanitizer/GPU
      execution remains unclaimed. BUG-204 is fixed and reverified; its retirement
      still waits for committing the existing runner fix.
- [ ] Rank remaining expensive sandbox consumers and template owners using
      fresh traces and invalidation costs before another extraction. Models and
      Visualization no longer include unused mock/reference headers; SceneCommands
      also drops both, and MeshMethods drops its unused reference header. Preserve
      genuine mock/reference use and distinct Clustering graph/triangle semantics.
      MockDevice lifecycle now has a compiled owner and matched batch evidence.
      Fresh clean producer medians rank MeshMethods (22.588 s), SessionLifecycle
      (21.828 s), ClusteringMethods (19.729 s), AssetImportFormatCoverage (18.417 s)
      and SceneCommands (18.276 s) highest. Obtain discriminating compiler traces
      and check canonical helpers before choosing another extraction. Keep current
      source frozen until a new baseline is recorded.
- [ ] Optional Vec3 direction and edge-color exceptional-value history coverage,
      including captured NaN compared with itself.

After the fixture-batch verification below, a fresh session is recommended before
another consumer family. Resume from this consolidated list, the C104 report and
the working diff; both matched measurement batches are now complete.

Final verification: Clang 23 canonical `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests -j 4` pass. The focused
selector from the prior checkpoint passes 400/400 tests. The default full CPU
selector in Verification passes 4,861 executed tests, zero failures and one
expected `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip
(4,862 selected; 157.61 seconds). No sanitizer or GPU execution is claimed.
Claude's corrected fixed-diff review reports no blockers and confirms the
restored JobService declaration ownership.

Strict layering, test-layout, task-policy and explicit-file docs-sync pass;
relative links, root hygiene, session-brief freshness and whitespace checks pass.
Module-inventory regeneration produces no diff. Four-point sweep: one dependency
cleanup intent, unchanged engine layering, unchanged helper/test bodies and passing
behavior coverage, updated task record with no public surface/doc changes needed.
No C++ edits followed the successful final build/tests. Research-manager epilogue:
ordinary refactoring with exploratory development diagnostics only; no new research
result or ARA mutation. All changes remain uncommitted, including the preserved
pre-session work.


## 2026-09-21 — Compile mock lifetime once and trim two more consumers

The operator requested continued compile-time/duplication work, batching related
slices with Claude. The existing uncommitted fixture and measurement changes were
the baseline and remain preserved. The standing convergence focus is acknowledged;
this explicitly directed continuation stays within RUNTIME-270 test compilation.

Reuse search found `MockRHI.cpp` and `MockRhiTestSupportObjs` already own the
recording methods and all linked mock consumers. `MockDevice` still synthesized
its container construction/destruction in consumers. Its default constructor and
virtual destructor now default out of line in that existing owner. Explicit
`noexcept` preserves the measured baseline traits; the mutex continues to delete
copy/move. No new file, abstraction, target or production dependency is introduced.
Leave the other three mock classes unchanged: a user-declared destructor would
suppress implicit moves, particularly for vector-held `MockCommandContext`.

The related dependency slice removes unused Poisson reference includes/aliases
and duplicate AssetWorkflow imports in MeshMethods and SceneCommands, plus the
unused mock include/namespace alias in SceneCommands. Genuine MeshMethods mock
use and Clustering reference/mock use remain. Every sandbox helper and TEST body
is byte-identical to the session baseline. The compiler caught the unused
`Extrinsic::Tests` alias after removing SceneCommands' mock include; removing that
alias resolves the experimental compile failure without changing any test.

Fresh cache-bypassing Clang 23 traces confirmed the two initial consumers were
slow to compile. After outlining, MockDevice-specific container destructor
instantiation events disappear from MeshMethods. This is development diagnostic
evidence for placement, not a matched performance result or general speedup claim.
Some later diagnostics overlapped build/profiling work; do not compare their wall
times. Shared-owner cost, invalidation and matched clean-build measurement remain
open in the consolidated list above, along with BUG-204. Snapshots, trait probe,
review packets and validation logs: `/tmp/intrinsic-mock-lifetime/`; compiler
traces: `/tmp/intrinsic-compile-graph/lifetime-*`. These are ephemeral diagnostics.

Claude CLI `--model fable --effort medium` reviewed the plan and fixed diff;
the exact 5.1 backend suffix is not independently attested. No Codex subagent
was needed for this bounded batch. The support README names the compiled
lifecycle owner. Source-documentation audit has zero errors/review findings.
The task's existing contract declarations remain applicable; no canonical
contract is narrowed. All remaining task points are in the updated consolidated
checkbox list above; this slice does not retire RUNTIME-270.


Final verification for the mock lifetime batch: canonical Clang 23 `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests -j 4` pass. The focused selector
below passes 516/516 tests. Claude's corrected fixed-diff review reports no
blockers. Strict layering, test-layout, task-policy and explicit-file docs-sync
pass, as do relative links, root hygiene and whitespace checks. Module inventory
regeneration has no diff. The full CPU selector from Verification passes 4,861
executed tests, zero failures and one expected
`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip
(4,862 selected, 161.08 seconds). No sanitizer or GPU execution is claimed.

```bash
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.|CurvatureSegmentation|MeshCurvature|Geodesic|Parameterization|GeometryProperty|ProcessingCompilationLocality|RendererFrameLifecycle|SamplerManager|TextureManager' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```


Final four-point sweep: one test-compilation intent with two related slices;
production layering unchanged; preserved helper/test bodies and lifetime traits,
with all affected CPU executables built and tested; owner documentation and task
open points updated. No source edits followed the successful build and tests.
The lifetime change adds seven lines across the existing header/implementation;
the consumer cleanup removes nine lines including one blank line. This reduces
repeated compiler-generated work, not duplicate handwritten algorithm bodies.
No research result is promoted: research-manager epilogue requires no ARA mutation
for this ordinary refactoring and exploratory diagnostics. Changes remain
uncommitted alongside preserved prior work. A fresh session is recommended now;
resume from this note and the diff for matched batch measurement or another
measured hotspot, without reloading old conversation transcripts.


## 2026-09-21 — Matched accumulated fixture batch measurement

The operator explicitly prioritizes compilation and duplication work this session.
Measure the existing test-support batch before selecting another extraction; prior
working changes remain preserved. Reuse `benchmark_compile_iteration.py` and its
already-fixed BUG-204 header-consumer probes. New manifest:
`benchmarks/ci/manifests/engine_compile_iteration_fixture_batch.yaml`.
Baseline `b3c18fd17add1a555540b57ba388d62726d39e87`; after is local snapshot
`887415fb7e6df4c04caa154f06f64f04a422b731`, containing only the ten test/support
files in the accumulated diff (178 additions, 440 deletions including README).
The snapshot is isolated; the working branch and index are not committed or changed.

Canonical owners remain `EditorFeatureTestContext.cpp` for shared fixtures and
`MockRHI.cpp` for mock lifetime. Distinct Clustering graph semantics stay local.
The measured target is `IntrinsicRuntimeContractTests` and its dependency closure;
other executable consumers and full-engine clean builds are outside this cohort.
ABBA, two samples per arm, Clang 23, ci Debug Null/headless, four jobs, no compiler
cache, same source/build paths and identical preinstalled dependencies. Measure
clean/no-op, all five consumer edits, both compiled owners and both shared headers.
Full source lists account for every rebuilt consumer; declared probes are minimum
required consumers, not claims of exact fanout. No competing builds/tests run.

Claude CLI `--model fable --effort medium` reviews the fixed diff and protocol;
the precise 5.1 backend suffix is not independently attested. Read-only review
packets and progress: `/tmp/intrinsic-fixture-measure-20260921`; raw sampling:
`/tmp/intrinsic-fixture-results-20260921`. Sampling and fresh verification are complete. C104 and
`ara/evidence/tables/runtime270_fixture_batch_measurement.md` bind all results:
Models 25.37% lower incremental median, Visualization 21.01%, fixture header
13.84%, mock header 34.10% (11 → 8 compiled sources). Shared fixture owner cost
rises 18.00%; clean target median falls only 0.50%. All small and negative outcomes
are retained. No general/full-engine clean-build improvement is established.

Fresh canonical ci configure and IntrinsicTests build pass. Full CPU CTest:
4,861 passed, zero failures, one expected capability skip (4,862 selected,
162.88 seconds). Baseline runtime executable: 1,193 passed and two Null-platform
skips. Both snapshot lifetime-trait compilations pass. Four canonical results,
103 manifests and 26 tooling tests pass. Claude's final raw-evidence audit reports
no blockers; the initial noexcept concern was withdrawn and compiler-checked.
Starting host load differed (0.80 versus roughly 2.6–2.7); this remains a local
descriptive comparison with n=2 per arm, not statistical or publication evidence.

The consolidated list above contains the remaining task work. No unresolved
review or execution blocker remains for this measured cohort. Other executable
costs, sanitizer/GPU coverage and the product-family audit remain open; nothing
in this measurement retires the overall task. The entire existing tests diff was
hash-checked unchanged after sampling and verification. No new C++ edits were
needed this session. Changes remain uncommitted, preserving the prior working
state; the isolated snapshot commit is only a measurement identity.

Four-point sweep: one accumulated test-compilation intent, canonical test-support
owners and production layers preserved, source/trait/full-CPU verification green,
report/claim/task evidence synchronized. Use the saved handoff and this note for
the next measured hotspot; do not load prior conversation transcripts.


## 2026-09-21 — SessionLifecycle dependency cleanup

The operator explicitly prioritized duplicate-code and measured compilation work
with Claude for this session. Starting HEAD is `b3c18fd17`; all pre-existing dirty
fixture, tooling, measurement and ARA changes are preserved. This slice changes
only `Test.SandboxEditorSessionLifecycle.cpp`: 58 deleted include/import/alias
lines, no helper or test-body changes, and no production-source changes.

Selection used C104's retained clean producer timings (SessionLifecycle median
21.828 seconds), then a fresh Clang 23 trace. The trace identified unused
`MockRHI.hpp` instantiating transfer-token vectors and byte-buffer hash maps.
Reuse inspection found no mock or progressive-Poisson reference use in this
consumer: remove those headers, their aliases, unused imports and the duplicate
AssetWorkflowModule import. No replacement helper or abstraction is needed.
`RuntimeTestModule.hpp` remains the canonical runtime test-module owner.
The `Intrinsic::Tests` uses are distinct from the removed `Extrinsic::Tests` alias.

An experimental compile caught required Asset.ImportRouter and
ECS.Component.StableId imports; both were restored before successful verification.
Claude CLI `--model fable --effort medium` reports Fable 5.1 and reviewed the plan
and fixed inline diff read-only. Its initial `/tmp` packet read was denied; that
attempt was not treated as review. The subsequent inline review found no blockers
and withdrew its Geometry.KMeans concern. KMeans records intentionally come from
PointCloudServiceOperations' exported ClusteringTypes; EditorFileImportResult is
owned by the retained SceneEditingOperations import. No exhaustive
import-what-you-use claim is made.

### Development compilation diagnostics

Four serial direct compiler invocations in ABBA order, same ci Debug Clang 23
command, source/output paths and existing BMI inputs, compiler cache bypassed,
trace overhead included in every arm. Before: 20.393 and 20.536 seconds; after:
14.455 and 14.768 seconds. These are exploratory object-compilation observations,
not canonical benchmark results or a clean/full-target incremental speedup claim.
BMI construction, dependency scanning and linking are excluded; n=2 per arm,
normal desktop host without affinity/governor control. Both after dependency files
exclude MockRHI.hpp and ProgressivePoissonReference.hpp and retain RuntimeTestModule;
the identified mock-container instantiations disappear from the traces.

Snapshots, exact command, sample JSON, depfiles, traces, failed experiment and
review/build/test logs are under `/tmp/intrinsic-session-locality-20260921` and
`/tmp/intrinsic-compile-graph/session-{before,after,corrected}`. These are ephemeral
diagnostics, not durable benchmark evidence. The final source is byte-equal to
the after snapshot; every helper and test body is byte-equal to baseline.

### Remaining work

- [x] Trace and remove this consumer's unused mock/reference dependencies.
- [ ] Canonical matched clean/target-incremental and header-touch measurement of
      this new slice before making a build-speed claim; C103/C104 are complete
      and must not be rerun as substitutes for the new baseline.
- [ ] Remaining measured candidates: MeshMethods, ClusteringMethods,
      AssetImportFormatCoverage and SceneCommands. Trace actual mechanisms before
      extracting more helpers; AssetImportFormatCoverage does not contain these
      unused headers and was intentionally left unchanged.
- [ ] All product-family binding, numeric adapters, visualization semantics,
      geodesics/UV, feature-width, UI-037, scalar/picker, topology naming and
      optional history coverage points in the consolidated list above remain open.
- [ ] Full-engine/other-executable timing, sanitizer/GPU/interactive verification
      remain open. BUG-204 retirement still awaits its existing fix's commit.
- [ ] Accumulated working changes remain uncommitted; preserve and review each
      coherent batch separately when preparing commits.

Fresh verification: `cmake --preset ci` and
`cmake --build --preset ci --target IntrinsicTests -j 4` pass. Focused
`^(SandboxEditorSession|SandboxEditorSessionLifecycle|EditorPointReadiness)\.`
CTest selector with standard exclusions passes 64/64. Default full CPU selector
passes 4,861 executed tests, zero failures and one expected
GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl capability skip (4,862
selected; 159.52 seconds). No sanitizer/GPU execution is claimed.

Task policy, relative links, explicit-file docs-sync, test layout, root hygiene,
session-brief freshness and whitespace checks pass. Final code hash and byte-equal
helper/test bodies rechecked after verification. Four-point sweep: one measured
consumer's dependency cleanup, no layer/public API changes, unchanged behavior
coverage passes, task and handoff synchronized. No unresolved correctness/review
finding remains. Research-manager epilogue: ordinary refactoring and exploratory
diagnostics, no new research claim or ARA mutation. Fresh-session handoff:
`/tmp/intrinsicengine-handoff-20260921-session-locality.md`. This verified boundary
is a useful restart point before another hotspot; no need to reload transcripts.
