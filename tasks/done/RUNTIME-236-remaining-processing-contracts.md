---
id: RUNTIME-236
theme: J
depends_on: [RUNTIME-235]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive continuation; implementation evidence is the diff, review and tests. No new performance claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.processing-compilation-locality, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
---
# RUNTIME-236 — Finish remaining processing execution contracts

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
BUILD-007 owns matched engine compile measurements; C92 remains a hypothesis.


## Goal
Move the remaining bilateral, descriptor, construction, progressive-Poisson,
clustering and consolidation execution contracts off broad geometry-processing
state. Remove the broad command/context/result machinery when its last caller
has migrated; retain one cross-cutting discovery owner.

## Scope and decisions
The operator requests continued simplification with Claude to conserve usage.
Existing verified uncommitted RUNTIME-233/234/235 work is the baseline; preserve
it. No compatibility wrappers, per-method handles/contexts/libraries or new
framework. Keep numerical algorithms, services, backend choices, cancellation,
current config round trips, property-domain eligibility and visible UI behavior.
The original slice excluded commit/push; local integration is now recorded above. The baseline snapshot is
`/tmp/intrinsic-runtime236-20260913/before/` with hashes in `before.json`.

Flagged ceremony: the broad API still duplicates context/handle resolution,
config forwarding and result retention for the remaining six areas. Reuse
EditorProcessingCommands, GuardEditorProcessingResult, ApplyEditorProcessingConfig,
compiled property capture/radius helpers and the existing family frame pattern.
Keep specialized catalogs only where trial capture or minimum sample counts
change their contracts. Service-owned async work is load-bearing and remains
in its current service; do not duplicate it in editor adapters.
Reintroducing an extra execution context would require a concrete ownership
contract that the generic context cannot represent, not merely a method name.

## Engine integration
| Surface | Preserved contract |
|---|---|
| Least-structured input | Typed float vec3/scalar properties on the element domains each method already accepts; topology only where the algorithm needs it. |
| Compatible entity sources | All currently supported mesh/graph/point-cloud element domains; eligibility keeps the canonical preflight. |
| RuntimeModule | Existing service/job ownership, attachment epochs, cancellation, stale guards and terminal-result delivery. |
| Config/agent/UI | Same validated serialized controls, explicit entities/properties/backends, diagnostics and Show actions. |
| Publication | Same-domain output or explicit owning construction/replacement; unrelated data, history and dirty notifications retained. |
| Spatial queries | Existing radius/kNN membership, metric, batching and SpatialIndexCache reuse; no algorithm changes. |
| End-to-end tests | Existing domain/config/UI/lifetime/backend tests, plus the actual Vulkan cases for the migrated GPU paths. |

## Acceptance criteria
- [x] Record owner/reuse choices with Claude and exact baseline/full production footprint.
- [x] Migrate all six remaining areas and remove superseded broad execution plumbing.
- [x] Extend compiler-metadata locality checks and preserve behavior/lifetime tests.
- [x] Review the fixed combined diff with Claude, fix findings and pass affected verification.
- [x] Update architecture, discovery routes and generated inventory; state remaining scope honestly.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
bash tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/sync_skills.py --check
```
Run focused family, config/UI, lifetime and ProcessingCompilationLocality checks
before the full CPU gate. Reconcile separate ci-asan/ci-ubsan CPU gates and actual
ci-vulkan integration for changed GPU continuations. Retain existing BUG-188
host workaround and BUG-177/180 limitations. Prior matched family-edit probes
already exist; this batch checks compiler metadata and full size without another
wall-time benchmark or speedup claim unless a concrete new risk requires one.

## Approved owner plan — 2026-09-13
Descriptors join PointAnalysisOperations (same radius-analysis mechanism).
PointSetOperations groups bilateral and progressive Poisson; PointConstructionOperations
owns new surface/graph entity creation. PointCloudServiceOperations retains
clustering/consolidation service dispatch and correlation. Shared mesh helpers
replace duplicate forwarders; canonical Poisson config replaces the editor copy.
Generic processing context stays free of clustering/consolidation service-module imports: the service
family frame borrows two pointers, and operations validate attachment before
service use. Broad execution context/handle/results and Public.cpp are removed
once callers migrate; discovery and selection use the generic processing handle.
No new timing benchmark is planned; actual compiler metadata gates locality.

## Implementation and reuse decisions — 2026-09-13

Owners as approved. `Runtime.GeometryProcessingOperations` keeps its name and
shrinks to discovery: domains/algorithms/capabilities/menus,
`EditorGeometryProcessingModel`, `GetAvailableEditorKMeansDomains`,
`ResolveEditorSelectedMeshVertexProperties` and the four primitive-selection /
selection-interaction entry points, all retyped to `EditorProcessingCommands`.
Implementation files kept their current names; renaming them to the new family
prefixes stays a separate mechanical slice.

Reuse instead of new code:

- `Internal.hpp` and its four double-forwarding shims are gone. The four units
  now call `EditorFeatureDetail::{ResolveStableEntity, ToEditorCommandStatus,
  GeometryMetadataSignatureForEntity}` and `MeshSupport::{FindActiveEditorJob,
  BuildActiveDerivedJobMessage, InvalidateSelectedModelCache,
  CollectFiniteGeometryPositions, EditorJobResult}` — all global-module-attached
  `extern "C++"` owners already compiled in ordinary TUs, so module attachment
  is preserved. The anonymous third copies in the Poisson unit were compared
  field-for-field against those owners before removal; `CollectFiniteVertexPositions`
  and `CollectFiniteGeometryPositions` were byte-identical.
- Those five family-neutral helpers plus `QueuedCpuJobUnpublishedReason` are
  declared in the existing `PointFields.hpp`, which `MeshSupport.hpp` includes;
  their definitions stay in `MeshSupport.cpp`. The Poisson unit therefore drops
  `MeshSupport.hpp` and returns to `Geometry.HalfedgeMesh.Fwd`, and
  `ProcessingCompilationLocality.PointSet` now forbids `Geometry.HalfedgeMesh`
  and `Geometry.MeshSoup` so the mesh-snapshot surface cannot creep back in.
  `PointKnnRows`/`AdvancePointKnnRows` moved to their sibling `RadiusRows.hpp`
  so that inclusion does not push the spatial-index cache into six mesh units.
  `CollectFiniteGeometryPositions` lost its default argument (one declaration,
  one default); its two callers pass the position property explicitly.
- `ApplyEditorProcessingConfig` now serves every processing family’s config apply, including
  clustering and point-cloud consolidation. Those two have no editor-side section
  pre-validator, so they pass `{.State = EngineConfigState::Valid}` and let the
  whole-document preview be the single rejection path — the same pattern
  `ApplyEditorCurvatureSegmentationConfig` already used. Default source IDs, the
  rejected result shape and its diagnostics are unchanged.
- `AreEditorProcessingConfigCommandsAvailable` replaces
  `AreEditorGeometryConfigCommandsAvailable` on the shared handle; panels read
  one `ProcessingConfigCommandsAvailable` flag.
- `PrepareEditorProcessingCommands(attachment)` is the single new composition
  entry, declared by discovery and implemented in a composition-only
  `Runtime.GeometryProcessingOperations.Frame.cpp`.

Service family: the generic `EditorProcessingContext` imports neither
clustering nor consolidation service module.
`EditorPointCloudServicePreparedFrame` carries the two borrowed service pointers
beside the generic handle (the `EditorParameterizationUvViewCommandSurface`
precedent). Every entry point checks `EditorProcessingCommands::IsBound()`
before dereferencing a service, so a frame copied past detach or session
destruction reports unavailable instead of touching freed state.

Progressive Poisson now uses `ProgressivePoissonPlaygroundConfig` directly; the
editor duplicate, its two enums and six conversion functions are gone. The
serialized `double` round trip is unchanged, and `ProgressivePoissonHashLoadFactor`/
`ProgressivePoissonRadiusAlpha` narrow to `float` exactly where the retired
`MakeEditorProgressivePoissonConfig` did — at the CPU reference and GPU plan
boundaries — with validity checked on the narrowed values so rejection is
unchanged. Panel widgets keep `float`/`int` locals and widen when building the
config.

Intentionally retained as distinct contracts: `GetEditorBilateralFilterInputCatalog`
(trial-captures a distinct filtered output and requires count-matched normals, so
it is not `GetEditorPointInputCatalog`); the descriptor 33-slot output record;
construction's new-entity publication and identity/hierarchy undo validation;
and the service families' `Queued` status plus correlation ids.

Queued terminal delivery is now uniform. Progressive Poisson and ICP
registration install `JobDesc::FinalizeUnpublishedOnMainThread` like bilateral,
descriptors, construction and the analysis families, so a job that is cancelled,
stale or dropped delivers exactly one terminal failure while attached instead of
leaving the panel row on its submit-time `Pending` message. Detachment suppresses
callbacks into the retired UI. Both keep the
submit-time channel/backend identity on that result and append the worker's own
diagnostic when the apply gate never rejected. The finalizers read only their
own job state — no scene, no spatial cache, no history or output publication —
and the shared `Delivered` latch prevents duplicate delivery when a rejected
publisher is followed by the finalizer. Both apply gates test the attachment
epoch before any borrowed-scene read, and the ICP GPU readiness gate closes before touching the borrowed spatial
index cache, releasing its share of the pending query batch. A request that
observes an already active job for the same entity and output registers no
callback; an immediate outcome on a session without a job lane is returned
rather than delivered. The former `EXPECT_FALSE(completedSinkCalled)`
assertions in `Test.SandboxEditorClusteringMethods.cpp` now assert one attached
terminal failure while keeping every property/transform/topology/history
assertion, and new cases cover publish-time rejection, duplicate `Pending`
observation, cancellation and a genuinely freed scene.

## Review reconciliation — 2026-09-13

Fixed after the fixed-diff review and the root findings:

- `tests/CMakeLists.txt` `ProcessingCompilationLocality.MeshFieldTests` had six
  further test names as positional `add_test` arguments, which failed configure
  (`build-initial.log`). It now registers one name and forbids the three point
  families alongside its existing siblings.
- `src/runtime/README.md` no longer names the removed
  `SandboxEditorContext::GeometryCommands`, and the boundaries doc's name table
  now points the mesh topology/field and UV types at their real declaration
  owners instead of the retired broad interface.
- Removed the unreachable publisher branches for a null scene and a null
  submit-time snapshot in the Poisson path after the same-thread apply gate
  and the owned snapshot established their validity. The
  `ProgressivePoissonProperties` guard stays: the queued path never checks the
  snapshot's domain storage at submit, so it is reachable.
- Rejected as not a defect: an unattached rejected request carries a default
  `World` in its `KMeansRunCompleted`/`PointCloudConsolidationResult`. The
  request never reaches a service and routes no event, so there is no consumer to
  preserve a last-attached world for; no compatibility-shaped expired context is
  reintroduced.

Configure/build, all 663 focused family/locality checks and the full CPU,
ASan and UBSan gates passed after reconciliation. Vulkan integration exposed
one registration test adapter defect and two unchanged consolidation watchdog
failures; all three were diagnosed and their registered reruns passed.

## Root reconciliation and architecture review

The initial Claude review missed the reproduced CMake blocker and the unsafe
scene reads; root inspection found both. The final frozen-source Claude review
found no blocking code issue. Subsequent focused execution exposed a missing
descriptor-dismissal switch case and an empty dependency result in the new
cancellation test; both were fixed and all 663 focused checks then passed.
The cancellation dependency now makes pre-worker metadata coverage deterministic.
The full CPU run's one unrelated timeout is tracked in
[BUG-190](BUG-190-curvature-refinement-timeout-during-compilation.md);
timed test runs are sequenced after compilation rather than changing deadlines.

The four-point review keeps this batch scoped to processing execution ownership
and its lifetime/config/publication contracts. No layer policy, numerical
kernel, renderer pass, service implementation or compatibility path was added.
The shared handle stays generic; clustering/consolidation service pointers
remain at family composition.
The new family interfaces are compilation boundaries, not new dispatch layers.
The reuse routes and public inventory follow the final declaration owners.

| Clean-workshop row | Result |
|---|---|
| 1 — Layer imports | pass: strict scan, zero exceptions |
| 2 — CMake links | pass: no new cross-layer target link |
| 3 — Public types flow downward | pass: processing surfaces stay in runtime |
| 4 — Renderer ownership | n/a: no renderer growth |
| 5 — Typed pass IDs | n/a: no new passes |
| 6 — Resource-driven recipe edges | n/a: no recipe changes |
| 7 — Maturity follow-ups | n/a: no research capability retirement |
| 8 — Exception owners | pass: no compatibility shims or allowlist entries |

## Verification results — 2026-09-13

Evidence archive: `build/analysis/runtime236-remaining-processing-2026-09-13/`
(ignored local artifacts; exact dirty baseline and complete command logs).

| Gate | Result |
|---|---|
| Clang 23 `ci`, `IntrinsicTests` + `ExtrinsicSandbox` | pass after the recorded CMake repair |
| Focused family/config/UI/lifetime/locality | 663/663 pass, including 19 compiler-metadata locality checks |
| Full CPU, idle host | 4569 passed; 1 ASan-only control skipped; 4570 selected |
| Full ASan CPU | 2948/2948 pass, including the GLFW leak-shutdown control |
| Full UBSan CPU | 2947 passed; 1 ASan-only control skipped; 2948 selected |
| Registration after caller repair | CPU 47/47, ASan 21/21, UBSan 21/21 pass |
| Vulkan ICP after caller repair | pass, 46.75 seconds, original watchdog and seven rounds |
| Selected Vulkan integration, reconciled | all 32 selected cases passed across the original selection and affected reruns; initial 29/32, then ICP, LOP and EAR passed |
| Strict layering/task/docs/test-layout/mirrors | pass; final evidence sync recorded in the archive |
| Source documentation | zero objective errors; 203 advisory findings, mostly existing README history |
| Strict root hygiene | existing BUG-177 `.agents/` mismatch remains |

The immediate-registration adapter ignored returned results and waited for a
callback, so its first round never advanced. It now feeds each non-`Pending`
return to its existing completion handler. The existing CPU history test also
asserts zero callbacks for immediate success and rejected submission. Claude's
conditional double-callback concern was checked against the full production
branch: rejected submission installs no finalizer or callback. The already
covered queued path delivers once. No production change or timeout increase
was needed for that test repair.

Timed tests ran without concurrent compilation after BUG-190. The unchanged
curvature case passed at its original 30-second limit both alone (17.76 seconds)
and in the full CPU gate (16.63 seconds). Sanitizers used the existing BUG-188
host execution workaround. GPU evidence retains the registered environment;
it does not establish whole-process leak freedom (BUG-180).

The exact production delta against the turn-start dirty snapshot is 46 touched
files, 138 fewer physical lines and 49 fewer nonblank lines. Eight files were
added and two deleted, a net increase of six compilation-boundary files.
This batch finishes the remaining processing execution-family migration;
it is not a whole-engine completion claim or a new compile-time benchmark.
The accumulated implementation is recorded in the local commit below.


The consolidation failures were test budget shortfalls: original-budget probes
reached the last phase after cancellation and earlier assertions completed.
Claude reviewed the traces and the targeted test-only correction. LOP now uses
240/300-second internal/CTest limits, EAR 480/540; all other deadlines, labels,
commands and sanitizer environments were verified unchanged across the 32-case
registry. Both original registered cases passed. Their phase durations remain
as useful failure diagnostics; temporary debug tags are removed. BUG-191 records
the initial failures, diagnosis and final runs. This is correctness verification,
not a timing benchmark or a claim that the service became faster.

All acceptance criteria for this processing slice are implemented and verified,
and locally integrated. Earlier CPU/sanitizer gates were followed by focused checks
for the final test-only edits; the entire suite was not unnecessarily repeated.
The historical BUG-177 root-hygiene mismatch was subsequently resolved in RUNTIME-237.
