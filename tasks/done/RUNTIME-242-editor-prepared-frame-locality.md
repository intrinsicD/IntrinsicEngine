---
id: RUNTIME-242
theme: J
depends_on: [RUNTIME-241]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive refactor with an exact dirty baseline, fixed Claude review and compiler/correctness checks; no timing claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-242 — Keep workspace snapshots out of processing-frame composition

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
BUILD-007 owns matched engine compile measurements; C92 remains a hypothesis.


## Goal
Continue the operator-authorized simplification with Claude, preserving the
verified uncommitted source. Remove unnecessary snapshot dependencies from the
private attachment interface and sixteen context-only editor operation implementation units.
Keep public record ownership, layout, defaults and all editor behavior.

## Reuse and right-sizing plan
The existing workspace session owns prepared-frame lifetime and attachment
epochs. Its `LastFrame()` accessor has no caller: remove its declaration,
forwarder and Impl getter. Its private `PrepareFrame` has one caller supplying
all four arguments: remove private defaults, preserving the public workspace
preparation defaults. Retain `IsAttached`, which the public attachment uses.

Keep SnapshotRequest, Snapshot, SnapshotContext and SelectedModelCache in
EditorWorkspaceSnapshots. Pointer/reference-only consumers borrow declarations
using the existing C++ linkage pattern; all four sole definitions carry matching
exported C++ linkage. Preserve storage and public signatures; compile the two
cache traversal methods in the existing matching Public.cpp. Private attachment
uses the canonical AssetPayloadKind enum owner, not the scene-operation alias.
Remove snapshot imports from the eleven family Frame units and five scene,
visualization and recipe operation units that use context instead of snapshot
values. Actual snapshot consumers retain
the owning import. No new file, class, visitor, wrapper or compatibility path.

The session and visitor stay: their epoch and borrowed-reference boundaries
carry correctness. A real future snapshot member/value use requires importing
its owner at that consumer; an actual private default-argument caller would
justify reconsidering that convenience. Rebuilt compiler metadata establishes
private attachment 100→18 dependencies. Broad family dependencies may remain, so no compile-speed claim follows.

## Acceptance criteria
- [x] Capture exact prior source and discover owners, callers and existing tests.
- [x] Review the bounded alternative with Claude; implement matching ownership and remove dead API/imports.
- [x] Preserve record bodies and attachment behavior; add compiler-metadata guards.
- [x] Review a fixed diff with Claude and fix confirmed findings.
- [x] Pass native CPU, focused separate sanitizer, representative Vulkan and strict structural checks.
- [x] Update canonical ownership docs, inventory and exact results/remaining scope.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Fresh-configure isolated ci-asan and ci-ubsan with
`-DINTRINSIC_GROUP_PURE_CTEST=ON`, build IntrinsicCpuTests, then run each with the
same CPU exclusions and
`-R 'SandboxEditorSession|Workspace|ModelCache|SelectedAnalysis|ProcessingPanel|NormalPanel|RegistrationPanel|ParameterizationPanel|EditorCompilationLocality'`
plus `--no-tests=error --timeout 60 --parallel 1`. Public record attachment and
private import changes retain the same runtime bodies; focused sanitizers plus
the full native suite cover the combined source. Full sanitizer PR/merge gates
remain required independently.

Build ci-vulkan IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests and run the
`SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes` case with both
gpu/vulkan labels and existing registered deadline/leak settings. This is a
representative workspace/display check, not general GPU parity evidence. Run
test gates only after builds finish. Preserve BUG-188's host-discovery workaround
and BUG-180's separate leak-enabled follow-up.

Add guards on private attachment and all sixteen context-only operation producers using
compile_hotspots.py. Run strict clean-workshop, task/layout/root, source docs,
skill-mirror and compiler-tool checks. Refresh module inventory and session
brief. The original verification used dirty source. Baseline starts in
`/tmp/intrinsic-runtime242-20260913/`; final evidence belongs in
`build/analysis/runtime242-editor-prepared-frame-locality-2026-09-13/`.

## Plan review
Claude confirmed dead LastFrame, sole explicit-argument PrepareFrame caller,
three aggregate definitions and the existing borrowed-record pattern. Keep the
live IsAttached API. It found five more unused Snapshot imports, accepted into
the same cohort. Its suggestion to retain the scene-operation payload alias
would leave 54 dependencies; the canonical enum owner predicts 18. Root keeps
the owner import and removes SceneEditingOperations. This is a source/metadata
comparison, not an assertion that direct imports alone measure compile speed.
Preset dependency scanning must rebuild affected owners and consumers after
record attachment changes; use ccache-disabled builds and verify actual metadata.
Do not clobber unrelated build/evidence trees without a diagnosed stale artifact.

## Implemented source state
The request, snapshot, context and selected-cache definitions keep their sole
owner in `Runtime.EditorWorkspaceSnapshots.cppm`, with matching exported C++
linkage for existing pointer/reference borrowers. Request/snapshot/context bodies
and cache storage/signatures retain their fields, ordering and defaults. Cache
Clear/Stats bodies now compile in the existing Public.cpp; no file was added.
The private attachment imports the canonical asset enum owner and borrows only
the request declaration. The shared internal header borrows the snapshot,
context and cache types. Public preparation defaults stay; unused LastFrame
accessors and private preparation defaults are removed. Session storage,
epoch guards, visitor lifetime and operation bodies remain unchanged.

Sixteen context-only operation units lost the snapshot import.
`EditorCompilationLocality.WorkspaceAttachment` gained two forbidden modules;
`.PreparedConsumers` covers all sixteen units using actual compiler metadata.
The slice changes 21 production files, removes 7 physical / 9 nonblank lines net,
and adds or deletes no production file. Rebuilt private attachment closure is
100→18; each of the sixteen operation closures loses only the snapshot module.
Broader feature dependencies remain. This is structural evidence, not a measured
compile-time improvement.

## Review and verification results
The initial static Claude review missed a cache pointer whose declaration had
come from the removed import. The first native compile caught it. Root added the
matching cache borrow/definition and moved its non-trivial methods out of the
interface. The next compile exposed an obsolete module-attached cache forward;
root removed it. Both failed logs are retained. A second fixed-diff Claude
review inspected all borrowed declarations and corresponding definitions and
found no blocking issue. The final source comparison verifies the unchanged
records, storage, moved method bodies, public defaults and operation bodies.

Native, ASan, UBSan and Vulkan builds all passed. The full native CPU selector
passed 4,592 tests, with one expected ASan-only GLFW lifecycle test skipped.
Focused ASan and UBSan selectors each passed 52 tests. The selected Vulkan
workspace/display smoke passed without skipping. All gates bind to the same
1,362 source/build-input hashes. Strict structural checks and generated docs
are recorded alongside these results in
`build/analysis/runtime242-editor-prepared-frame-locality-2026-09-13/`.
Source-documentation review retains four advisory comments that specify
selection, ownership or lifetime contracts; there are no documentation errors.

## Remaining scope
Implementation and scoped verification are complete and locally integrated.
RUNTIME-246 records the final combined sanitizer gates. BUILD-007 owns matched
compile measurements; later slices own shared-binding cleanup and REVIEW-004
owns product convergence.
BUG-188's host-discovery workaround and BUG-180's separate leak-enabled follow-up
remain unchanged. No whole-engine completion or compile-speed claim is made.
