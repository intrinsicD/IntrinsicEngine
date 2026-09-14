---
id: RUNTIME-239
theme: J
depends_on: [RUNTIME-238]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive consolidation and compilation-boundary refactor; fixed dirty baseline, review and correctness/metadata gates, without a new performance claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, graphics.recipe-slot-lookup, runtime.spatial-query-locality]
---
# RUNTIME-239 — Reuse recipe lookup and isolate the spatial query interface

## Goal
Complete the two remaining candidates from RUNTIME-238 under the operator's
continuing plan/implement/review/test/fix authorization with Claude. Preserve
user-visible methods, configuration, property publication and renderer behavior.
No public C++ compatibility bridge, new source file, service or registry is needed.
Exact dirty baseline: `/tmp/intrinsic-runtime239-20260913/before/` and `before.json`.

## Canonical owners and reviewed plan
- `Graphics.RenderingContract` owns `RenderRecipeDescriptor`; its existing
  `.cpp` owns the compiled const/mutable `FindRecipeSlot` overloads. Remove the
  repeated const lookups in RenderRecipeConfig, FrameRecipe and recipe editing,
  plus config's repeated mutable traversal. Keep first matching stable name,
  `nullptr` on missing, empty-name behavior and normal vector-borrow invalidation.
  Runtime calls the graphics owner explicitly. No inline traversal or template
  framework is introduced. An existing config-preview test will prove that
  duplicate names edit only the first slot without changing outer validation.
- `SpatialIndexCache` remains the concrete service/module and owns the same
  CPU snapshots, GPU work, revisions, batches and resource retirement. Its
  public GPU methods are the existing nearest/kNN/radius queues. The only
  external low-level cache caller is a smoke test; remove public
  RecordGpuBuild/RecordGpuQueries/GpuView and keep recording private in Impl.
  Keep handle-based double stale checks, allocation/failure cleanup, lazy-build
  order and counters. Graphics.PointLbvhWorkspace remains the direct compute
  workspace for actual lower-level consumers; no GPU kernel or view type moves.
- The cache also imports WorldRegistry solely for a constructor reference.
  That import reaches JobService and RHI, so removing PointLBVH alone is
  insufficient. Reuse the established exported extern-C++ class definition and
  matching out-of-line definition attachment for WorldRegistry, with a matching
  non-exported forward declaration in the cache. Keep WorldRegistry's fields,
  methods, CPU constructor use and lifecycle bodies unchanged. Actual users
  import the definition where needed. No additional Pimpl or forward header.

Claude reviewed both the initial plan and this second import path. Its valid
corrections are included: a failed queued batch cannot be reused, so the
reacquire phase submits a fresh one; expected CPU/GPU build counts become two.
Use actual preset-ci Clang23 CMake/P1689 metadata through compile_hotspots.py;
the initial suggestion to use the inactive ci-clang20 tree is rejected.
The initial cache closure contains 44 modules; negative guards captured both
PointLBVH-to-RHI and WorldRegistry-to-JobService paths. Final counts must come
from the rebuilt source, not a simulated graph or a timing estimate.

## Preserved integration contracts
| Concern | Required behavior |
| --- | --- |
| Inputs and metric | Canonical float3 properties on all compatible domains; same Euclidean/local or explicit entity-transform space. |
| Query membership | Original source rows, deletion mapping, deterministic ties, exclusion, k limits, inclusive radius and full overflow counts. |
| Mutation and lifetime | Stale source/world/entity/transform/deletion handles reject; reacquire rebuilds; immutable CPU leases and pending GPU retention remain. |
| Backend reporting | CPU queries stay CPU; queued GPU queries reject unavailable/stale work and never silently fall back. |
| Config/UI/publication | Existing validated paths and named-property publication remain unchanged; no method or backend is added. |

Extend the existing framed kNN/radius fixture, replacing its separate manual
cache smoke helper. Preserve initial batch reuse and stale-before-submission
failure. Reacquire after the existing mutation: kNN IDs remain ordered the same,
but row 4 squared distance changes to 4; radius counts become {2,3}. Confirm the
second GPU build, then destroy/prune and reject CPU/queued queries on the old
handle. Keep existing fixture and CTest deadlines; avoid mutable property reads
in assertions that could advance revisions.

## Acceptance criteria
- [x] Capture owners, callers, exact baseline and failing dependency guards; review plan with Claude.
- [x] Implement canonical recipe lookup and smaller cache interface without new source files.
- [x] Preserve world lifecycle and GPU queue state/lifetime behavior with affected tests.
- [x] Enforce cache metadata boundary, update actual callers and synchronize docs/inventory.
- [x] Review fixed diff with Claude and fix findings before final verification.
- [x] Pass preset CPU, sanitizer and affected actual Vulkan checks; record full source footprint and remaining scope.

## Implementation notes
Implemented and verified in the working tree:
- `Graphics.RenderingContract.cppm/.cpp` export and compile the const/mutable
  `FindRecipeSlot` overloads; the mutable one delegates through
  `std::as_const` + `const_cast`. The four private copies in
  `Graphics.RenderRecipeConfig.cpp`, `Graphics.FrameRecipe.cpp` and
  `Runtime.RenderRecipeEditingOperations.cpp` are gone, runtime calls
  `Graphics::FindRecipeSlot` explicitly, and every `<algorithm>` include is
  still used by remaining code. `Test.RenderRecipeConfig.cpp` gains
  `DuplicateSlotNamesEditOnlyTheFirstMatch`.
- `Runtime.SpatialIndexCache.cppm` drops `RecordGpuBuild`/`RecordGpuQueries`/
  `GpuView`, `Graphics.PointLBVH`, `RHI.CommandContext` and the `WorldRegistry`
  import; the CPU constructor keeps a non-exported `extern "C++"` forward
  declaration. The two recorders moved verbatim into `Impl` as handle-based
  `RecordBuild`/`RecordQueries` and the participant calls `m_Impl->RecordQueries`.
- `Runtime.WorldRegistry.cppm/.cpp` carry matching `extern "C++"` attachment on
  the class definition and its member-definition block; no in-tree caller needed
  a new import: WorldRegistry was not re-exported, while remaining users of
  the previously re-exported PointLBVH already import its owner directly.
- `Test.PointLBVHGpuSmoke.cpp` deletes `CheckEntityCache` and extends
  `KnnBatchApp` with round 3 (fresh batch after reacquire, row-4 squared distance
  4, radius counts {2,3}, CPU/GPU builds 2, then destroy/prune rejection).
- New CTest `SpatialCompilationLocality.QueryInterface`.

## Verification
Completed on the final reviewed source with canonical ci Clang23, separate
ci-asan/ci-ubsan grouped serial CPU gates and affected ci-vulkan checks.
Vulkan host: NVIDIA GeForce RTX 3050, driver 590.48.01.
BUG-188's host discovery workaround remains. Registered Vulkan tests retain
their existing leak settings; BUG-180's separate leak-enabled evidence remains
open. Changes are uncommitted; these observations are not timing speedup claims.

## Final review and verification — 2026-09-13
Evidence: `build/analysis/runtime239-spatial-query-boundary-2026-09-13/` contains
the exact dirty baseline, fixed Claude plan/review snapshots, correction diffs,
source identity, compiler metadata and complete gate logs.

Claude's independent review found a frame assertion that could repeat until the
fixture deadline after failed reacquire. Replaced it with an expectation and
explicit loop exit; the radius and kNN size checks now exit before indexing too.
Removed a dead cache installation in the direct GPU workspace test and corrected
the import explanation. Claude's follow-up found no remaining actionable issue.
The Vulkan run checks the retained deterministic IDs after changed tree bounds.
The source-documentation synopsis and task-heading errors found by structural
checks were corrected; original failures and passing repair logs are retained.

WorldRegistry's class and implementation tokens match the baseline except for
required linkage attachment. Both private GPU recorder bodies match after the
required owner-reference substitutions; the const recipe lookup matches its
previous implementation. No resource ownership, queue state machine, fallback,
shader, algorithm or test deadline changed. The initial CPU build preceded a
comment correction; the final native rebuild and full CPU/sanitizer/Vulkan runs
all use the final source identity, including the reviewed GPU-test corrections.

| Gate | Result |
| --- | --- |
| ci configure, IntrinsicTests and ExtrinsicSandbox | pass |
| Focused contracts and compiler-metadata guard | 252 passed; no skips |
| Full native CPU on host | 4,577 passed; 1 ASan-only case skipped |
| Full ASan CPU, grouped, serial | 2,956 passed; no skips |
| Full UBSan CPU, grouped, serial | 2,955 passed; 1 ASan-only case skipped |
| Affected Vulkan queues, consumers and rendering | 40 passed; no skips |
| Kernel-policy / compiler-analysis Python regressions | 24 / 22 passed |
| Strict layering, task policy, links, layout, root hygiene and skill mirrors | pass |
| Source documentation | zero objective errors; six advisory findings retained |

The compiler-metadata closure of SpatialIndexCache drops from 44 to 11 modules;
GeometryProcessingOperations 64 to 63, NormalOperations 95 to 94,
PointAnalysisOperations 94 to 93, EditorProcessing 60 to 58. Only the cache
interface is claimed GPU-independent; actual processing owners still import
backend types when their contracts require them.

This batch changes nine production files, removes 13 physical/nonblank lines,
and adds/deletes no production file. Combined with RUNTIME-238, 13 production
files change with 389 fewer physical lines and 332 fewer nonblank lines. These
are structural counts against captured dirty baselines, not benchmark results.

Clean-workshop rows 1–4, 6 and 8 pass; 5 and 7 are not applicable (no new pass,
scaffold or parity closure). No layering allowlist exception is introduced.
The two remaining candidates named by RUNTIME-238 are complete. This closes
this selected cleanup scope; it does not assert whole-engine product convergence
or retire the existing matched compile-timing and product acceptance work.
