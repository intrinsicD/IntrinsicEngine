---
id: RUNTIME-234
theme: J
depends_on: [RUNTIME-233]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive follow-up; implementation evidence belongs in its diff, review and tests.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.processing-compilation-locality, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
---
# RUNTIME-234 — Isolate the remaining point-analysis contracts

## Goal
Migrate density weights, keypoints and outlier analysis through the proven
processing-family boundary without duplicating capture or publication mechanisms.

## Scope
Owner: `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.*`
adapters and `Sandbox.MeshProcessingPanels.cpp`. Group contracts only when
consumers need them together; keep radius versus kNN semantics and outlier
removal/provenance transactions explicit. SpatialIndexCache retains ownership,
metrics, support membership and requested/actual backend reporting.
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
- [x] Complete fixed-diff review, CPU, sanitizer, applicable Vulkan and structural checks (existing BUG-177 gate failure recorded below).
- [x] Update owner routes and architecture docs; distinguish pilot closure from Framework24 product completion.

## Verification
Use `cmake --preset ci`, build `IntrinsicTests` and `ExtrinsicSandbox`, and run
the focused family/config/UI tests plus `ProcessingCompilationLocality` before
the full CPU selector in AGENTS.md. Complete its separate sanitizer gates and
the family's real Vulkan integration tests. Use RUNTIME-233's declared edit-probe
conditions and `tools/analysis/compile_hotspots.py` for comparable diagnostics;
no arbitrary wall-clock CI threshold or second impact database.

## Reuse and right-sizing plan
- Continue from the fully verified, uncommitted RUNTIME-233 pilot; preserve the
  earlier checkout changes. Its implementation dependency is satisfied in this
  combined source. No commit or push is requested.
- One PointAnalysisOperations family owns weights, keypoints and outlier
  diagnostics; density/spacing stays independently compiled. Reuse the generic
  processing handle and existing session/frame pattern with incomplete borrowed
  records. No per-method context, handle, library or new dispatcher.
- Outlier removal must retain its selection-clearing transaction. Move the
  existing selection dependency into the shared execution context; remove its
  duplicate broad-context field and update callers. History replay must reject
  an expired attachment before dereferencing the borrowed scene.
- RadiusRows currently belongs to the broad module even though weights,
  keypoints, descriptors, construction and bilateral filtering share it. Use
  the same ordinary compiled-owner/C++-linkage pattern as PointProperties;
  preserve query membership, capacity, pagination and failure semantics.
- Freeze source and matched build probes under
  `/tmp/intrinsic-runtime234-20260913/` before production edits. Extend compiler
  checks to enforce both directions between the two point families and protect
  unrelated workspace/mesh adapters. Review the fixed diff with Claude.
- Reconsider grouping only when measured consumers need a different boundary;
  shared numerical capture does not require shared public method records.

## Review and implementation record
- Added one PointAnalysisOperations family for outliers/keypoints/weights,
  retaining explicit numerical and provenance transactions. The shared generic
  context carries selection, needed by removal; session bindings borrow
  incomplete result containers and only the composition leaf copies them.
- Reused PointProperties for catalog discovery in all five existing consumers,
  independent of method configs/results; canonical property resolution already
  rejects empty names. RadiusRows now has an ordinary compiled C++ linkage owner.
- Five typed config adapters call one compiled validated apply mechanism. The
  broad context/command overloads, forwarding and family result slots are gone.
- Claude reviewed fixed 267,831-byte packet SHA256
  `0eda24a8e713d7e584d68b23bd693764bb30ac2ce175f73ac737c3adeb3c2ba9`
  with tools/hooks/MCP disabled; review is under
  `/tmp/intrinsic-runtime234-20260913/claude-review.txt`.
- Addressed missed GPU test handle conversions; made config helper imports and
  `<string>` explicit; fixed construction's remaining catalog call. Descriptor
  code already obtains shared declarations through its existing private header.
- Documented completion semantics on both families: synchronous outcomes return
  directly, newly submitted jobs deliver once while attached, and duplicate
  active-output requests observe the existing job without registering callbacks.
  Tests distinguish duplicate callbacks and direct synchronous return.
- Added catalog exclusion/deleted-NaN tests across all three APIs, direct and
  prepared-frame selection-clear tests, freed-scene undo and queued callback
  tests. Added a dedicated compiler cohort for migrated test producers so they
  cannot accidentally import the other point family.
- Selection is a present shared execution dependency; a new per-method context
  would duplicate the generic handle. UI config availability still comes from
  the same prepared-session binding as the family commands, so no additional
  family-specific availability flag is introduced.
- The first focused run passed 151/153 selected entries; two source-assertion
  binaries belonged to a different producer than the selected build targets.
  Full IntrinsicTests reconciliation is required before reporting final results.

## Reconciliation progress

The complete `IntrinsicTests` and `ExtrinsicSandbox` build passes. All 157
focused family/config/UI/session/compiler checks pass; the full CPU selector
passes 4,548 entries (six expected capability skips), in 116.51 seconds. The
prepared-frame selection test uses SceneInteractionModule and explicit same-domain
mask/score refs, matching production composition and config validation. Its
initial null-service assertion explained the fixture crash; no speculative
stale-BMI source workaround was introduced.

The matched public density-weight result edit measured 351.986 s / 60 physical
compiler invocations before and 70.610 s / 16 afterward; exact source restoration
and rebuild passed on both variants. Single dirty-worktree samples, not a
repeatable speedup claim. The entire production footprint is +30 physical / +19
nonblank lines across 32 changed production files, with three family files added.
Final sanitizer and Vulkan verification is recorded below.

The final owner audit found a superseded immediate outlier-removal API with no
app caller. Its behavioral/test comparison and deletion are recorded under
RUNTIME-235's broad API retirement, without interrupting the frozen verification
of this family boundary.

Clean-workshop scope review: rows 1–3 pass (allowed runtime dependencies, no new
target links, no higher-layer API exposure); rows 4–6 are not applicable (no
renderer members, passes or recipes changed); row 7 is not applicable (no
research/product maturity closure); row 8 passes (no layering exceptions or
compatibility bridges added). Existing root-hygiene finding BUG-177 (`.agents/`)
remains; no agent metadata was deleted and the strict gate was not weakened.

## Final verification and handoff

Implementation and review are complete in the combined, uncommitted checkout.
The [local diagnostic report](../../ara/evidence/tables/runtime234_compile_locality.md)
and [machine-readable record](../../ara/evidence/diagnostics/runtime234_compile_locality.json)
bind source hashes, footprint, matched edit probes, review and final test logs.
Full local artifacts are under
`build/analysis/point-analysis-locality-2026-09-13/`.

- Full CPU: 4,548 selected CTest entries, zero failures, six expected capability
  skips. Fresh full ASan: 2,926 selected, zero failures/skips. Fresh full UBSan:
  2,926 selected, zero failures, one expected ASan-only leak-test skip. Sanitizer
  registration groups the audited pure cohort; execution remains serial.
- Affected actual Vulkan integration: all 22 PointLBVHGpuSmoke and
  PointConstructionGpuSmoke entries pass without skips. This covers migrated
  GPU fixtures and the shared capture/radius consumers.
- Final post-probe reconciliation: all 157 focused family/config/UI/session and
  compiler checks pass. Production hashes still match the measured source.
- Strict layering, task policy, doc links, test layout and skill mirrors pass;
  generated module inventory matches. Compile-hotspot tooling has 22 passing
  tests; both canonical benchmark results validate. BUG-177 remains a separate
  root-hygiene gate failure, and BUG-188 retains sanitizer sandbox ownership.

RUNTIME-235 owns the remaining mesh-processing migration and comparison/deletion
of the superseded immediate outlier API. No commit, push, whole-engine slimming,
repeatable speedup or Framework24 completion is inferred from this slice.
