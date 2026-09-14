---
id: RUNTIME-233
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive architecture migration; the fixed diff, review and tests provide implementation evidence. Performance measurements still follow AGENTS.md section 8.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.processing-compilation-locality, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
---
# RUNTIME-233 — Prove local compilation for density and spacing

## Goal
Make density and spacing independently changeable through geometry, runtime,
config and editor, while preserving their complete user-facing behavior and
removing unnecessary integration code. Demonstrate the result with controlled
edit/build measurements before applying the design to further families.

## Decisions and scope
- The operator approves the architectural direction and explicitly confirms
  there are no external public C++ API consumers or persisted scenes/configs
  requiring compatibility. Replace APIs and engine-owned formats directly;
  update callers, current-format codecs and fixtures together. Delete obsolete
  paths instead of adding wrappers, aliases, dual readers or migration layers.
- Preserve all existing features. Overall product completion also requires the
  [Framework24 inventory and final gate](../../docs/product/framework24-convergence.md);
  this pilot does not claim to close existing product gaps.
- C++23, Vulkan/RHI, layer ownership, typed property domains, validated config,
  cancellation, history and deterministic failure handling remain required.
- Start with kernel density and point spacing. Density weights, keypoints and
  outliers must continue using the common capture owner when it moves; this
  does not enroll their entire public APIs in the pilot.
- Use bounded Claude design/diff reviews and focused feedback during iteration;
  reconcile the combined source before full integration gates. Preserve the
  earlier uncommitted work; no commit or push is part of this task's current
  authorization. No production source edits while a build reads those files.

## Discovery and right-sizing
- Flagged element: `Runtime.GeometryProcessingOperations.cppm` combines method
  contracts, context, typed result sinks and operations. Density/spacing `.cpp`
  units implicitly import that primary interface. Private headers also pull in
  its broad context; changing filenames alone cannot isolate compilation.
- Existing owners to reuse: `PointProperties.cpp` / `PointFields.hpp` for live
  input capture, scalar transactions and kNN continuation; `SpatialIndexCache`
  for acceleration; `EditorWorkspaceSession` for retained results and epoch
  guards; existing processing controls for entity/property/config execution.
- Simpler alternative: give the coherent family a narrow contract and compiled
  operations, with only the execution dependencies it consumes. Keep algorithm
  implementation imports private. Typed result aggregation remains at session
  composition and must not be imported back into method implementation units.
  Migrate the panel's command/query use with the runtime owner. Do not create a
  service, target or duplicate DTO layer for each individual method.
- Blast radius: family config/types, geometry imports where required, shared
  capture owner, runtime/session composition, processing panel and test callers.
  The other capture consumers retain numerical and publication semantics.
- A broader shared surface is warranted only when present consumers need its
  contract together and measured change impact justifies the coupling.

## Engine integration
| Surface | Required preserved behavior |
|---|---|
| Least-structured input | Typed float vec3 property on any supported element domain; live-row/deletion validation and stable source-row mapping. |
| Compatible entity sources | Mesh vertices/edges/halfedges/faces, graph nodes/edges/halfedges, and point-cloud points; no provenance narrowing. |
| RuntimeModule | Existing runtime composition, spatial cache and job owner; cancellation, attachment epoch and stale-input/output rejection. |
| Config/agent | One serializable typed config and validated preview/apply path also used by UI; current-format round-trip. |
| UI | Entity follows selection, explicit input/output properties, real backend choices, readiness, diagnostics and Show actions. |
| Publication | Same-domain scalar output, unrelated properties/topology preserved, undo/redo guards and rendering notifications. |
| End-to-end tests | Density/spacing operation, processing panel, config, session-lifetime and actual Vulkan publication tests. |

Spatial queries retain their present metric, membership, self-candidate and
pagination rules. `SpatialIndexCache` remains the index/reuse owner; this task
changes dependency boundaries, not query semantics or available backends.

## Slice plan
1. Freeze the current combined source identity. Define declared edit probes
   and baseline conditions before changing production code: algorithm body,
   family runtime body, panel body, family public record, and config-only value.
   Reuse `tools/analysis/compile_hotspots.py` and configured compiler/Ninja
   metadata. Keep compile, link and test/application readiness boundaries
   separate. Touch-only probes are dependency diagnostics, not substitutes for
   representative content edits. Do not compare mixed Ninja histories.
2. Remove the broad primary-module/context dependency for the pilot end to end.
   Reuse existing mechanisms, migrate every in-tree caller of the replaced API,
   and delete superseded entries. Capture net production footprint including
   shared helpers, forwarding code and build declarations.
3. Review the fixed diff, repair findings, and run matched edit probes plus
   focused correctness tests; run full relevant gates on the integrated source.
4. Add deterministic dependency/rebuild regression coverage using existing
   build facts, and document the proven boundary in the architecture contract,
   catalog and reuse owner routes. Coordinate with CI-014 instead of adding a
   second build-impact database or selector. Timing budgets require comparable
   samples; do not invent a wall-clock limit for arbitrary PR hosts.
5. Record remaining family migrations and observed exceptions with concrete
   owners before expanding. A new method integration exercise must demonstrate
   the canonical path without changing unrelated method implementations.

## Acceptance criteria
- [x] Record the no-compatibility decision and identify canonical owners and the
  broad-interface dependency that the pilot must remove.
- [x] Retain comparable baseline and candidate edit probes with exact source,
  toolchain, preset, target, cache and command identities; emit validated
  manifests/results before making any timing claim.
- [x] Pilot implementation units and private support no longer import the broad
  processing interface or all-method result/config aggregates, directly or
  transitively. Shared implementation exists once.
- [x] A pilot family public-record edit does not recompile unrelated algorithm
  adapters; any remaining composition rebuild is explicitly explained from
  compiler/build metadata. A panel-body edit does not recompile algorithm code;
  a config value change uses the existing validated path without compilation.
- [x] Narrow typed config, runtime and UI paths are fully wired, obsolete API
  entries are deleted, and all integration-matrix behavior is covered.
- [x] Report actual production footprint and edit/build/test costs. Investigate
  regressions before expanding; distinguish measured wins, tradeoffs and
  unresolved costs without asserting a predetermined speedup or LOC target.
- [x] Add executable locality regression checks, their canonical architecture
  contract/catalog entry, and the reusable agent implementation route.
- [x] Complete Claude review and fix source-confirmed findings; relevant CPU,
  separate sanitizer, Vulkan and structural gates pass on combined source.
- [x] Demonstrate the method integration path and record the bounded next
  family tasks; preserve the separate Framework24 completion gate.

## Verification
```bash
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure --no-tests=error -R '^(KernelDensity|PointSpacing|DensityWeight|Keypoint|Outlier|SandboxProcessingPanels|SandboxEditorSessionLifecycle|SandboxConfigSections)' --timeout 60
ctest --test-dir build/ci --output-on-failure --no-tests=error -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci-vulkan
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests --parallel 8
ctest --test-dir build/ci-vulkan --output-on-failure --no-tests=error -R '^PointLBVHGpuSmoke\.(OutlierNeighborhoods|LocalDistanceRatio|KernelDensity|PointSpacing|Keypoint|DensityWeight)' --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --check
python3 tools/agents/generate_session_brief.py --check
```
Integration also runs the exact `ci-asan` and `ci-ubsan` commands in AGENTS.md
section 7. The local declared manifest and content-edit runner are
`/tmp/intrinsic-runtime233-20260912/manifests/incremental.yaml` and `probe.py`.
Invoke `python3 /tmp/intrinsic-runtime233-20260912/probe.py before` on the frozen
pre-pilot source and `.../probe.py after` on the reconciled candidate. Each probe
restores the original content and rebuilds before continuing. Use
`ctest --test-dir build/ci --output-on-failure -R '^ProcessingCompilationLocality\.'`
after building the registered producers for deterministic dependency enforcement.

## Implementation and review progress
- Baseline content-edit probes completed and restored. Source hashes, disabled
  ccache, Clang 23 Debug preset identity, eight-job build commands and isolated
  Ninja edges are retained per run. Single-sample dirty-worktree results are
  diagnostic and explicitly not claim-eligible.
- Added one generic checked processing handle/context. Density/spacing own their
  typed records and commands; session storage and frame preparation remain in
  composition. Removed their entries from the broad context/result aggregate
  and updated all in-tree callers directly.
- Shared point capture/publication is one ordinary compiled implementation with
  C++ linkage declarations, reused by its existing consumers. Added an epoch
  check before any captured scene access and guarded typed completion delivery.
- Claude design review accepted the direction. Adopted its separation of generic
  execution, family records and session composition. Declined a templated result
  handle/trait framework and per-family contexts: present consumers need one
  handle and explicit typed callbacks. Also retained an independent generic base
  for the broad context; inheritance introduces no import back-edge here.
- New compiler-metadata boundary tests cover implicit/transitive dependencies,
  unresolved or ambiguous producers, stale scans and cycles. Added scene-destroyed
  queued-job tests and separate family retention/copy/dismiss/epoch tests.
- The final unsanitized build succeeds. All 102 focused tests and the full
  4,538-test CPU selector pass, with six capability skips. After source-shape
  assertions were updated for the new sink path and composition owner, the full
  selector was rerun successfully. Separate sanitizer and Vulkan integration
  also completed before the implementation review.
- Claude's design review is complete. Its fixed implementation packet is
  prepared at `/tmp/intrinsic-runtime233-20260912/claude-implementation-input.txt`;
  the operator approved transmission, and the read-only review completed.
  Corrections made after packet preparation restored two source-test strings,
  updated presentation assertions, and removed family sinks from the shared
  test fixture. Claude findings are assessed against that reconciled source.

## Bounded follow-ups
- Claude implementation review identified family-record coupling in the shared
  private bindings. Reuse those existing borrowed bindings with forward-declared
  pointers and session-owned sinks, rather than adding a new family session
  record/header/accessor. Give the two forward-declared containers C++ linkage;
  their complete definitions remain in the family module. Remove the resulting
  unnecessary family imports and extend compiler checks to sibling command and
  workspace units. A direct family import is needed again only by a consumer
  that actually reads or constructs its typed records.
- [RUNTIME-234](RUNTIME-234-point-analysis-compilation-locality.md)
  migrates remaining point-analysis contracts and their existing consumers.
- [RUNTIME-235](RUNTIME-235-mesh-processing-compilation-locality.md)
  migrates measured mesh-processing families one at a time.
- CI-014 retains generic impact-selection ownership. Renderer/SpatialIndexCache
  transitive dependencies are visible remaining costs, outside this pilot.

## Reconciled local evidence
- Local manifests and all twelve canonical result payloads validate. Frozen
  before/intermediate/final probes, exact physical compiler edges, executable
  link endpoints, source hashes, host/toolchain identity, final pilot diff and
  production footprint are retained under
  `build/analysis/processing-locality-2026-09-12/`. These are diagnostic runs,
  not claim-eligible performance evidence.
- The final family probe rebuilds only its two operation test files and the
  session-lifecycle test. Its production compilation is limited to the family
  and explicit runtime/app composition; unrelated numerical adapters stay out.
  The general test fixture now uses only the generic processing context,
  with density/spacing completion sinks local to their actual consumers.
- The canonical integration exercise is the migrated spacing path itself:
  define typed records/config in the family interface; bind shared commands;
  capture/validate through PointProperties; compute in the existing kernel;
  publish through the existing scalar transaction; retain/copy/dismiss at the
  session leaf; route UI through the same validated config and Show controls.
  The content-edit probes and compiler boundary tests check this path without
  adding a fake engine feature or a per-method framework.
- Root hygiene reports the pre-existing local `.agents/` metadata mismatch
  already owned by [BUG-177](../backlog/bugs/BUG-177-root-hygiene-local-agent-metadata.md).
  This task neither changes the root policy nor removes that local state.

## Architecture review
| Check | Disposition |
|---|---|
| Promoted imports follow layer policy | Pass: strict module/include layering check, no exceptions. |
| CMake links follow layer policy | Pass: existing runtime target owns new units; no new library edge. |
| Public surfaces avoid downward higher-layer exposure | Pass: generic and family interfaces stay in runtime; no app/private-session imports in the family primary. |
| Renderer growth has an owner | Not applicable: no renderer state or subsystem added by this pilot. |
| New passes use typed IDs | Not applicable: no passes added. |
| Recipe dependencies follow resources | Not applicable: no frame recipe changes. |
| Maturity closure names follow-up work | Pass: RUNTIME-234/235 own further family migration; Framework24 closure stays separate. |
| Temporary exceptions have owner/expiry | Pass: no new compatibility wrapper, layer exception or temporary backend. |

Config/UI share validated apply; typed domains, backend identities and numerical
kernels retain their contracts. Generic commands and queued completion callbacks
check the attachment epoch before using borrowed services. Session publication
and result copying run on the existing frame thread; workers use captured data.
The pilot deliberately adds a generic command owner and a family composition
leaf, not a per-method class hierarchy, registry or target framework.

## Production footprint
The fixed pre-pilot snapshot versus the final production diff affects 47 files:
44,763 to 44,973 physical lines across those files, a net addition of 210 lines
and six files. This count includes new interfaces, implementation units, the
private command-access header, caller wiring and CMake declarations; it excludes
tests, docs and pre-existing uncommitted changes. The new shared command owner
and explicit family composition leaf add code while the broad family entries
and context/handle overload pairs are removed. This pilot demonstrates locality,
not net engine slimming or a measured clean-build improvement. Further family
migration must continue counting its complete footprint rather than treating
file splits as code reduction.

- ASan compilation passed. Its first CTest invocation stopped during test
  discovery because the local sandbox blocks LeakSanitizer thread inspection.
  [BUG-188](../backlog/bugs/BUG-188-sandbox-sanitizer-test-discovery.md) owns this
  verification-environment issue. The identical CTest command was retried successfully
  outside the sandbox; no sanitizer setting or host ptrace policy was changed.

- The unchanged host ASan retry passed all 2,916 registered grouped/individual
  CTest entries. The new expired-attachment tests and both compiler-boundary
  checks passed under ASan; the subsequent UBSan and Vulkan runs also passed.

- The full UBSan selector passed all 2,916 registered entries with six expected
  capability skips. Its lifetime and compiler-boundary checks passed. Vulkan
  smoke-target compilation and the nine selected cases subsequently passed.

## Implementation review and repair
- All nine selected actual Vulkan processing smoke tests pass with no skips,
  including density/spacing cross-domain publication and cancellation after GPU
  submission. The CPU, separate ASan/UBSan and Vulkan gates are complete.
- [Final diagnostic report](../../ara/evidence/tables/runtime233_compile_locality.md)
  and [machine-readable record](../../ara/evidence/diagnostics/runtime233_compile_locality.json)
  bind the exact manifest/results, compiler counts, build/link endpoints, source
  hashes, footprint and verification log hashes. C92 remains a performance
  hypothesis; dirty one-sample timings are not claim-eligible.
- Claude reviewed the approved 252,801-byte packet, SHA-256
  `7271bd09df1647bfa46861c32c3ff07d035d418da2342508a4950512287a3794`.
  Its primary source-confirmed defect was family coupling through complete
  private workspace bindings. Replacing those values with incomplete borrowed
  pointers removes all nine sibling-unit imports without another file/accessor.
  The extended compiler gate and 73 focused tests pass. The repeated family
  record edit builds in 61.906 s with 14 compiler invocations (11 production,
  three tests), versus 77.103 s / 23 before review and 399.339 s / 62 before
  the pilot. Source restoration and its rebuild completed successfully.
- Review disposition: the broken source-test literal was already repaired after
  packet preparation. The frame visitor supplies `EditorFeatureBindings`;
  `AttachmentEpochIsActive` is a free helper; the existing geometry helper
  declarations/definitions resolve; designated result initializers and the
  exported friend declaration compile. These speculative build findings need
  no additional production changes. The copied result-message text is corrected,
  and the lifetime test now retains the current attachment across destruction.
- The tooling test now actually removes the object producer before checking
  failure. Do not require the scanner file to be newer than the object: scanning
  normally precedes compilation. Dependency checks require a completed producer
  build; CMake owns imported-interface freshness. Missing Python remains a
  configure error so the required contract cannot silently skip.
- Broad imports in the family frame leaf resolve the current private workspace
  header; shrinking that shared composition surface remains part of the bounded
  family follow-ups. Retain the compiled chooser and its small `std::function`
  callback rather than moving its UI body into a per-caller template. The generic
  context inheritance remains independent; no new context framework is needed.
- After repair, all 4,538 selected CPU tests pass with six capability skips;
  all 55 affected ASan and all 55 affected UBSan checks pass without skips; all
  nine actual Vulkan cases pass without skips. The meaningful builds, source
  documentation audit, layering, task policy and docs synchronization pass.
  This note remains active for landing; no commit, push, retirement or
  full-engine completion is claimed.
