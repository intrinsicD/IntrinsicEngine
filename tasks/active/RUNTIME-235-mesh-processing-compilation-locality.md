---
id: RUNTIME-235
theme: J
depends_on: [RUNTIME-233, RUNTIME-234]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive follow-up; implementation evidence belongs in its diff, review and tests.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.processing-compilation-locality, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
---
# RUNTIME-235 — Isolate mesh-processing contracts

## Goal
Migrate coherent mesh-processing families out of the remaining broad processing
interface, using compiler metadata for each family and a representative matched
content-edit probe for each implementation batch.

## Scope
Owners: normal/curvature, topology, UV/parameterization and registration adapters
in `src/runtime/Editor/Operations/`, with session composition and existing app
panels. Choose order from compiler rebuild evidence after RUNTIME-234; preserve
source/target registration controls, topology-changing ownership and existing
algorithm-specific transactions. Numerical kernels and renderer redesign are
out of scope.
Public APIs have no compatibility commitment. Update callers directly, preserve
all existing behavior and current-format config round-trips, and delete the
replaced broad API entries. Do not add per-method contexts, handles or libraries.
Use `intrinsicengine-reuse` and the owner routes before adding mechanisms.

## Engine integration
| Surface | Required preserved contract |
|---|---|
| Least-structured input | Existing typed float vec3/scalar properties, plus only required topology. |
| Compatible entity sources | All currently supported mesh/graph/point-cloud element domains; eligibility uses canonical preflight. |
| RuntimeModule | Shared processing commands, captured-input lifetime, cancellation and stale guards. |
| Config/agent/UI | Existing validated serialized config, selected entity/property/backend fields, diagnostics and Show actions. |
| Publication | Same-domain outputs, unrelated data preserved, history and renderer dirty notifications. |
| End-to-end tests | Existing domain/config/UI/lifetime/backend tests, actual Vulkan cases for migrated GPU paths. |

## Acceptance criteria
- [x] Record matched source/build baselines before changing code; count full production footprint.
- [x] Keep family interfaces and numerical adapters independent of session composition and unrelated methods.
- [x] Reuse the generic command handle, point/property capture and existing UI controls where their contracts match.
- [x] Extend compiler-metadata boundary checks for migrated sources; verify actual content-edit rebuild scope.
- [x] Complete fixed-diff review, CPU, sanitizer, applicable Vulkan and structural checks.
- [x] Update owner routes and architecture docs; distinguish pilot closure from Framework24 product completion.

## Verification
Use `cmake --preset ci`, build `IntrinsicTests` and `ExtrinsicSandbox`, and run
the focused family/config/UI tests plus `ProcessingCompilationLocality` before
the full CPU selector in AGENTS.md. Complete its separate sanitizer gates and
the family's real Vulkan integration tests. Use RUNTIME-233's declared edit-probe
conditions and `tools/analysis/compile_hotspots.py` for comparable diagnostics;
no arbitrary wall-clock CI threshold or second impact database.

## Discovery carried forward from RUNTIME-234

The older `ApplyEditorPointCloudOutlierRemovalCommand` in
`Runtime.GeometryProcessingOperations.DomainProperties.cpp` has no production
caller beyond its own broad forwarding overload; current app processing uses
configured OutlierAnalysis. Eleven calls remain in
`tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp`. During broad API
retirement, compare these tests against the named analyze/remove transaction,
carry over any unique user-facing coverage and delete the superseded command,
records and implementation. No compatibility bridge is required. This is a
concrete removal candidate, not evidence that all old semantics already match.

## Current slice: normal estimation and obsolete normal APIs

The operator requested continuation of the authorized simplification with Claude;
prior uncommitted RUNTIME-233/234 work is the verified baseline. No commit/push
is requested. Start with normals: a local public result edit rebuilt 57 physical
compiler producers in 334.094 seconds; exact restoration passed in 345.819 seconds.
The snapshot and declared probe live under `/tmp/intrinsic-runtime235-20260913/`.
The `ci` preset explicitly enables Sandbox for the same measured target pair.

Reuse/right-sizing decisions before implementation:
- The configured normal operation already handles point PCA, graph neighborhoods,
  weighted mesh vertices and mesh face normals. Three older provenance-specific
  commands have only test consumers and duplicate capture, jobs, history and
  result storage. Delete them and their broad overloads after migrating the nine
  old integration tests' unique coverage. Existing numerical-kernel tests remain.
- Use the existing generic processing handle and guarded completion callback;
  normal records leave broad method/session aggregates. One family owns all four
  normal algorithms; no per-variant handle, context or target is added.
- Reuse PointPropertyWatch, GeometryPropertiesCurrent, MutableGeometryProperties,
  BuildPointInputCatalog and validated config application. Retain topology and
  deleted-row semantics, vector publication and numerical/backend decisions in
  the normal adapter. Shared attachment checks guard scene lifetime before reads.
- Move the existing mesh face-ring validation and normal snapshot reconstruction
  to one ordinary compiled MeshSources owner. Mesh extraction and normal execution
  are present consumers; no numerical implementation or topology policy is copied.
- Carry forward canonical output/dirty notifications, all five mesh weightings,
  direct and queued no-change/history behavior, stale targets, and invalid-input
  coverage through the configured API. Add explicit detached/freed-scene and
  copied-frame callback tests. Public API-specific status/record layouts have no
  compatibility requirement; current UI/config behavior remains authoritative.
- Reintroduction requires a present user-facing capability or ownership constraint
  that the configured operation cannot express, rather than an old API name.

Remaining slices in this task: curvature/segmentation, topology,
UV/parameterization, registration and the recorded obsolete outlier API comparison.
This normal slice does not close the whole task or Framework24 convergence.

## Normal-slice review disposition

Claude reviewed a fixed source/test packet through the authorized CLI with tools,
hooks, MCP and persistence disabled. Review and packet are under
`/tmp/intrinsic-runtime235-20260913/claude-{input,review}.txt`.

- The broad normal result/sink/slot is fully removed; the apparent remaining
  field in the review was a hunk interpretation, confirmed by source search.
- Shared FindPropertyRevision observes all storage kinds and slot cardinality;
  GeometryPropertiesCurrent checks attachment lifetime before touching Scene.
  Typed output validation rejects an existing wrong-kind property before capture.
- The kernel has two orientation modes, None and MinimumSpanningTree; config
  accepts both. Rejecting ordinal 2 does not drop MST propagation.
- The normal snapshot builder has one surviving caller using the previous
  explicit no-provenance/deleted-geometry policy. Shared face-ring validation
  also serves mesh extraction. No other default/mutable-view callers remain.
- The shared catalog filters finite live vec3 rows and records each property's
  revision. The old method status mapper already forwarded to the common mapper.
- Session config availability and generic commands come from the same prepared
  binding; no redundant availability flag was added. Dismissal uses the existing
  one-argument UI helper. A missed entity-chooser handle was found during build
  reconciliation and updated to the prepared normal commands.
- Build reconciliation also corrected the module-file-set insertion and a direct
  cstdint include. The original fixed review packet remains preserved.

The entity chooser still reaches the shared catalog through a typed normal API.
A later catalog-surface cleanup should compare all identical typed catalog
wrappers and expose their common owner directly, deleting the redundant wrappers
and updating all callers together; do not add another forwarding layer.

## Normal-slice completion — 2026-09-13

Implemented and verified in the uncommitted checkout. The configured normal path
now owns its result/config/frame surface independently of the broad processing
module. Three obsolete normal APIs and their duplicate capture/job/history code
are removed; their unique behavior coverage uses the configured path. Existing
numerical kernels and selectable backend behavior remain covered.

The complete changed production footprint is 34,204 to 31,796 physical lines
(−2,408; −2,295 nonblank), including five new compilation-owner files. No whole
files were deleted. The matched normal-result edit measured 334.094 to 59.089
seconds and 57 to 12 physical compiler invocations (31/26 production/tests to
10/2). Exact restoration and final build reconciliation pass. This is one dirty
sample per variant, with no repeatable speedup or full clean-build claim.

The copied manifest initially retained `family: point_analysis`; the executed
edits were normal-result edits. Original artifacts remain unchanged beside a
metadata-only reseal declaring `normals` and Sandbox ON. The two executions,
source/footprint hashes, Claude packet and review, final diff, test results and
limitations are recorded in the
[report](../../ara/evidence/tables/runtime235_normals_compile_locality.md) and
[diagnostic](../../ara/evidence/diagnostics/runtime235_normals_compile_locality.json).
The full raw archive is local ignored output at
`build/analysis/normal-processing-locality-2026-09-13/`.

Verification on the final source:
- `VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON`,
  followed by `CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox`:
  passes, including post-probe reconciliation.
- Full CPU selector: 4,547 selected, zero failures, six expected skips. The first
  run found a stale source-check factory count; it was corrected to four and
  extended to check both normal jobs' world scopes before the passing full rerun.
- Final focused family/config/UI/history/lifetime/boundary checks: 323 passed.
- Fresh `ci-asan` and `ci-ubsan`, grouped `IntrinsicCpuTests`, full exclusion-only
  CPU selector with `--parallel 1`: 2,925 selected each, zero failures; ASan has
  no skips, UBSan has the one ASan-only leak-control skip. Existing BUG-188 host
  workaround was used without changing instrumentation or gate selectors.
- `ci-vulkan` target `IntrinsicPointLBVHGpuTests`, label intersection `gpu` and
  `vulkan`, regex `^(PointLBVHGpuSmoke|PointConstructionGpuSmoke)\.`: all 22 pass
  on the actual backend, no skips. Benchmark-output environment variables were
  unset to keep this verification separate from benchmark artifacts.
- Strict layering, task policy/state links, docs links, test layout, skill
  mirrors and diff checks pass; module inventory and session brief are current.
  Root hygiene still reports existing BUG-177 (`.agents/`); it was not suppressed.

Review sweep: scope is normal-family isolation and duplicate API retirement;
runtime remains the composition owner; config/UI share the validated path;
attachment epochs guard scene use, queued callbacks and copied result frames.
No algorithm/backend axis, dependency exception or compatibility bridge was added.

| Clean-workshop check | Result |
|---|---|
| Promoted imports follow layer policy | pass — strict check, no exceptions |
| CMake links follow layer policy | pass — no new target or cross-layer link |
| Public API exposes no higher layer downward | pass — runtime-owned records and generic commands |
| Renderer growth has an owning seam | n/a — renderer unchanged |
| New passes use typed IDs | n/a — no passes added |
| Recipe dependencies are resource-driven | n/a — frame recipes unchanged |
| Scaffold/parity closure has a follow-up | n/a — this task remains active |
| Temporary exceptions have owner/expiry | n/a — no exception introduced |

The global acceptance criteria remain open for the remaining families listed
above. This closes only the normal slice, not RUNTIME-235 or Framework24 convergence.

## Shared owner, catalog, obsolete outlier API, registration and parameterization — 2026-09-13

Implemented in the uncommitted checkout with Claude as sole source writer. Verification had not run at this intermediate checkpoint; the final
reconciliation below supersedes that status.

Slice and reuse decisions:

- The shared mesh helpers `MeshSupport.hpp` declares now compile once in an
  ordinary TU, `Runtime.GeometryProcessingOperations.MeshSupport.cpp`, following
  the `MeshSources.cpp` precedent. Only genuinely cross-family free functions
  moved: mesh soup/denoise/topology-edit source builders, stored-topology
  fingerprints, finite-position collection/comparison, topology publication,
  corner-UV forwarding, active-job messages and `ResultErrorOrUnknown`. Private
  mesh-field/topology structs, enum arrays, result builders and job bodies stayed
  with their owning family; `CaptureRegistrationProperty` and its record moved
  into registration. `InvalidateSelectedModelCache` and `FindActiveEditorJob`
  retype to `EditorProcessingContext`, which is all they ever read.
- `EditorProcessingContext` gains one borrowed `RHI::IDevice* Device`. It was
  absent there and is required by the registration backend gate; the duplicate
  field on `EditorGeometryProcessingContext` is removed and
  `MakeEditorProcessingContext` sets it once.
- Six byte-identical point-input catalog wrappers collapse into one compiled
  public `GetEditorPointInputCatalog` in `Extrinsic.Runtime.EditorProcessing`
  over the existing private `BuildPointInputCatalog`. Kernel-density,
  point-spacing and bilateral keep their capture-dependent catalogs and
  registration keeps its `>= 3`-sample filter: four distinct contracts, not
  duplicates. Every app, panel and test caller moved in the same change; no
  compatibility wrapper remains. The Sandbox context gained one shared
  `Processing` handle so the descriptor/construction panels stop reaching for a
  family handle to read a family-independent catalog.
- The obsolete `ApplyEditorPointCloudOutlierRemovalCommand`, its records, sink,
  result slot, model availability flag, retained result, session binding and the
  whole 944-line `DomainProperties.cpp` are removed. Coverage was compared first
  and carried onto the configured `OutlierAnalysis` Remove path: renderer dirty
  notification, exact `NumDeleted`/`v:deleted` accounting across removal, undo
  and redo, the empty-rejection `NoChange` with no undo entry, and fail-closed
  radius/missing-scene rejection before any mutation. Property survival, deleted
  rows, history and queued-job guarantees were already covered there.
- `Extrinsic.Runtime.RegistrationOperations` and
  `Extrinsic.Runtime.ParameterizationOperations` are now coherent public family
  modules on the verified normal-slice shape: `EditorProcessingCommands` plus an
  explicit typed optional completion callback, family records in the interface,
  incomplete `extern "C++"` sink/result containers borrowed by session bindings,
  and one `.Frame.cpp` composition leaf per family. The broad declarations,
  method sinks, result slots, retained snapshot fields and `Public.cpp` forwards
  for both families are deleted in the same change.
- The UV view command surface stays with parameterization: the session owns one
  guarded instance, shared bindings borrow an incomplete pointer, and the family
  frame copies it. `SubmitEditorParameterizationUvView`/`Disable...` take that
  copied surface, and the view model takes the family's copied results, so no
  general command framework or workspace aggregate is involved.
- Discovery/menu capabilities, progressive Poisson, bilateral, descriptors,
  construction, clustering, consolidation, geometry kernels and the UV renderer
  are unchanged apart from the six catalog-call migrations.

Checks and docs updated with the change: `ProcessingCompilationLocality` gains
`.MeshSupport`, `.Registration`, `.Parameterization` and `.RegistrationTests`,
and the existing family rows forbid the two new siblings; the layering
source-shape checks follow the deleted and added files; lifecycle tests cover the
newly independent registration and parameterization frames, their queued
delivery and stale attachment. Owner routes and
`docs/architecture/sandbox-editor-feature-boundaries.md` name the new owners.

### Corrected assumption: mesh kernel availability is shared data — 2026-09-13

The previous note recorded an obstacle that rested on a wrong source reading.
Codex re-checked the immutable pre-phase snapshot: the thirteen
`Mesh*KernelAvailable` / `MeshCurvatureDirectionsAvailable` /
`CurvatureSegmentationKernelAvailable` booleans gate **execution** inside the
command bodies (`Mesh.cpp` 5130/5313/5544/6096/6105/6114/6124/6294/6304/6332/6523,
and 4347/4359/4375/4387/5304 for the direction flag), not only the editor model.
Both readings of "where are these consumed" were partly right and the earlier
conclusion — that a family taking only `EditorProcessingCommands` could not keep
these contracts — was wrong.

Chosen resolution, smallest that preserves every existing contract: the thirteen
existing plain booleans move, with their exact current defaults, from
`EditorGeometryProcessingContext` to its shared `EditorProcessingContext` base.
This moves existing data and adds no capability registry, callback framework,
per-method context, handle or target. `MakeEditorProcessingContext` copies them
once (a designated initializer cannot name inherited members, so
`MakeEditorGeometryProcessingContext` keeps assigning the base wholesale), the
derived shadow declarations are removed, and the editor model still projects the
same values into panel availability. Shared command handles continue to guard
attachment lifetime. No unavailable-kernel or unavailable-direction fallback test
was weakened and no flag is hardcoded true.

## Mesh field and mesh topology families — 2026-09-13

Implemented in the uncommitted checkout with Claude as sole source writer, on the
verified normal/registration/parameterization shape. Verification had not run at this intermediate checkpoint; the final
reconciliation below supersedes that status.

Slice and reuse decisions:

- `Extrinsic.Runtime.MeshFieldOperations` owns curvature, curvature segmentation
  and geodesics: same-domain scalar/direction publication on the mesh that
  produced it, never a topology replacement. Curvature is its only queued method,
  so it is the only one with a typed optional completion callback; segmentation
  and geodesics run inline and own their whole history commit.
- `Extrinsic.Runtime.MeshTopologyOperations` owns denoise, remesh, subdivide and
  simplify. They are one family because all four rebuild the entity's halfedge
  mesh and share `BuildHalfedgeMeshForTopologyEdit`, `ApplyMeshTopologyState`,
  `SameMeshTopologyAndPositions` and the `EditorMeshTexcoordOutcome` contract,
  which moves with them. Its four retained outcomes keep one dismissal enum, as
  before, because the session's only reaction is resetting the matching optional.
- The five-way `EditorMeshCpuJobState` machine splits by family rather than being
  shared: curvature's job drops the kind discriminator entirely and topology's
  keeps a four-way one. To avoid copying the part that is genuinely common, the
  entity/provenance/metadata-signature/position prefix of the apply gate and the
  unpublished-reason text move to the shared `MeshSupport` owner as
  `ValidateMeshCpuJobSource` and `QueuedCpuJobUnpublishedReason` (RUNTIME-236
  renamed the latter and moved its declaration to `PointFields.hpp` when the
  point families started sharing it); each family adds
  only its own check (curvature property state, stored-topology fingerprint).
  No trait, factory or lifecycle template was introduced.
- Each job state now owns the guarded terminal callback the caller supplied
  instead of reaching into a session sink, so queued delivery, the duplicate
  `Pending` path that adds no callback, and the finalize-unpublished path that
  still owes exactly one result are unchanged.
- `IsPositiveFinite` and `ExtractMeshPositions` were declared by shared
  `MeshSupport.hpp` but still defined inside the broad mesh unit; both now compile
  in `MeshSupport.cpp` beside the other shared helpers. The duplicate
  `IsFiniteVec3` predicate is retired in favour of the existing shared
  `IsFiniteGeometryPosition`, and `AllFiniteVec3` moves to its only consumer.
- `Runtime.GeometryProcessingOperations.Mesh.cpp` keeps only the cross-cutting
  discovery surface every panel reads: domain resolution, surface-topology
  algorithm classification, menu items, capabilities, algorithm entries and the
  domain/algorithm debug names, plus the `GeometryProcessingDetail` shims other
  broad units call. It shrank from 6,162 to 452 lines and no longer imports any
  geometry algorithm module.
- The broad module loses both families' records, debug names, commands, config
  entry points, result slots, method sinks, retained snapshot fields, model
  `Last*Result` fields and `Public.cpp` forwards in the same change. Its model
  keeps the availability booleans, which still have panel consumers. The stranded
  `EditorGeometryProcessingResultSlot::Registration` enumerator, left over from
  the registration slice and matched by no sink or dismissal case, is removed.
- Family config entry points reuse the compiled `ApplyEditorProcessingConfig`.
  Curvature and geodesics keep their section pre-validation; curvature
  segmentation deliberately keeps having none, because the whole-document preview
  was and remains its only rejection path.

Checks and docs updated with the change: `ProcessingCompilationLocality` gains
`.MeshField`, `.MeshTopology` and `.MeshFieldTests`, and every existing family
row plus `.MeshSupport` and `.UnrelatedAdapters` now forbids the two new
siblings; only the `.Frame.cpp` composition leaves are exempt. The layering and
presentation source-shape checks follow the moved definitions, and the private
workspace-importer allowlist gained the four family frame leaves it was missing
since the registration/parameterization slice. Session lifecycle tests cover the
two new families' retention, per-slot dismissal independence, queued delivery,
detached-handle refusal and callbacks outliving the session. Owner routes and
`docs/architecture/sandbox-editor-feature-boundaries.md` name the new owners and
record the shared-availability decision.

The final reconciliation below resolves the pending imports, compilation and
compiler-metadata verification from this implementation checkpoint.


## Final reconciliation — 2026-09-13

All remaining families in this task are implemented and verified in the
uncommitted checkout. Registration, parameterization/UV, mesh fields and mesh
topology now have independent contracts and execution owners; the shared mesh
helpers and common catalog each have one compiled owner. The old immediate
outlier API is removed with its unique coverage carried onto configured removal.
This completes RUNTIME-235's implementation scope, not Framework24 convergence
or migration of every remaining broad processing adapter. The note stays active
until integration supplies a commit reference.

To respect the operator's usage budget, Claude performed the bounded source
migration while Codex owned reconciliation and verification. The remaining
families were batched before testing; every family received compiler-metadata
boundary checks, and curvature received the representative matched content-edit
probe. No separate timing is claimed for the other families.

Fixed-diff Claude reviews and local reconciliation repaired missing direct
imports and six omitted definitions, shared UV controls/dismissal ordering,
retained atlas sizing, exactly-once terminal delivery, and stale/unpublished UV
completion. Shared source validation now checks attachment lifetime before
reading Scene. New tests free scenes while curvature/topology/UV work is queued,
exercise failed kernels without losing their diagnostics, and verify UV callback
and sizing behavior. Old stale-result expectations and an unbound test fixture
were corrected without dropping property/history/dirty-state assertions.
The job-test harness now depends only on its actual JobCommands contract,
removing a broad fixture dependency from registration tests.

Final native production footprint for this batch: 40,193 to 40,055 physical
lines (−138), 37,899 to 37,818 nonblank (−81), across 47 changed production files;
16 files added and one deleted. File count increases because the split creates
compilation owners. The improvement here is compile locality and clearer reuse,
not a large reduction in total code. These counts exclude the preceding normal
slice and include every changed/new production owner and build file.

One matched curvature-result edit measured 344.537 to 74.229 seconds, with
57 to 16 physical compiler invocations (production/tests 31/26 to 11/5).
Clang 23 Debug, ci plus Sandbox, the same two targets, eight build jobs and
CCACHE_DISABLE=1 were used. Filesystem caches were not flushed; source was dirty;
there was one sample per variant. Exact edit restoration and reconciliation
passed. The only later source/build change is BUG-189's one-line CTest timeout
registration correction. No clean-build or repeatable speedup is established.

Verification:
- ci configure; IntrinsicTests and ExtrinsicSandbox build; post-probe build pass.
- Final focused tests: 537 pass. Full CPU: 4,560 selected, zero failures,
  six expected skips.
- Fresh separate ASan/UBSan presets and IntrinsicCpuTests; full grouped CPU
  selector, serial execution: 2,938 selected each, zero failures; ASan no skips,
  UBSan one ASan-only skip. Existing BUG-188 host workaround retained.
- Actual ci-vulkan PointLBVH, PointConstruction and UV-view checks: 22 passed
  initially, one inherited timeout. BUG-189 was diagnosed with Claude and the
  corrected registered case passes: 23 distinct selected cases have passing
  evidence, no skips. General GPU cohort leak settings are unchanged;
  the separate direct diagnostic's retention remains recorded under BUG-180.
- Strict layering, task policy/state links, docs links, test layout, skill
  mirrors, module inventory and diff checks pass. Strict root hygiene still
  reports existing BUG-177; it is not suppressed.

The eight-row clean-workshop scorecard above applies to the combined change:
layer/import/link/public ownership rows pass; renderer, passes, recipes and
exceptions remain unchanged. P1 uses existing generic commands and borrowed
plain data; P3 retains the common validated config path; P5 frame recipes are
unchanged. There are no compatibility bridges, new library targets or layering
exceptions. Review packets, failed attempts, repairs, source hashes and full gate
records are bound in the [report](../../ara/evidence/tables/runtime235_remaining_compile_locality.md)
and [diagnostic](../../ara/evidence/diagnostics/runtime235_remaining_compile_locality.json).
