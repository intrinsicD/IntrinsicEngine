---
id: RUNTIME-243
theme: J
depends_on: [RUNTIME-242]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive refactor with an exact dirty baseline, fixed Claude review and compiler/correctness verification; no timing claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-243 — Narrow the processing-frame binding surface

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
BUILD-007 owns matched engine compile measurements; C92 remains a hypothesis.


## Goal
Continue the operator-authorized simplification with Claude. Remove broad editor
imports from eleven processing-frame leaves while preserving family results,
commands, backend availability and attachment lifetimes.

## Reuse and right-sizing decision
The existing private attachment interface now owns EditorFeatureResultBindings:
21 moved result/UV pointers plus a family-owned two-service-pointer borrow.
EditorWorkspacePreparedFrame references that record, the opaque broad bindings,
workspace snapshot and processing context. Complete values stay family-owned.
EditorProcessingContext retains its fields/defaults and sole owner, with matching
C++ linkage for reference-only borrowers. The session constructs it after epoch
and config/job callbacks are installed, and clears cached/borrowed state during
preparation/reset. Frame commands and workspace queries copy the prepared context.
Scene-frame results copy through existing context pointers, whose optionals
change only during preparation/reset. No callback or subscription body changes.

Keep family leaves and the visitor lifetime boundary; eleven current consumers
justify the pointer record. Reject a per-result accessor framework, broad family
imports in the private attachment, and gathering all families into Session.cpp.
No new production file, service, compatibility path or algorithm change. A new
cross-family requirement needs concrete callers before widening this surface.

## Acceptance criteria
- [x] Match the prior 1,362 source hashes; capture exact dirty source and compiler metadata.
- [x] Plan and implement with Claude, deleting superseded fields/declarations/imports.
- [x] Independently review fixed diffs; apply and review both suggested improvements.
- [x] Diagnose and repair two architecture-test failures without weakening the boundary.
- [x] Pass native CPU, focused separate sanitizers, representative Vulkan and strict structural checks.
- [x] Update ownership docs, compiler guards, inventory, brief and exact evidence.

## Verification
```bash
cmake --preset ci --fresh -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Configure ci-asan and ci-ubsan fresh with `-DINTRINSIC_GROUP_PURE_CTEST=ON`, build
IntrinsicCpuTests and run the same CPU exclusions with
`-R 'RuntimeEnginePrivateGlue|SandboxEditorPresentation|SandboxEditorSession|Workspace|ModelCache|SelectedAnalysis|ProcessingPanel|NormalPanel|RegistrationPanel|ParameterizationPanel|PointCloudConsolidationPanel|EditorCompilationLocality'`
and `--no-tests=error --timeout 60 --parallel 1`. Full sanitizer merge gates
remain separate. Build ci-vulkan IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests;
run `RuntimeSandboxAcceptanceGpuSmoke.SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes`
with both gpu/vulkan labels, `--no-tests=error --timeout 120` and unchanged
registered leak/deadline settings. Tests run after compilation finishes.

Run strict clean-workshop, task/layout/root, skill-mirror and compiler-tool checks;
audit the four touched interfaces/headers. Source documentation has zero errors
and nine inspected advisories (borrow/lifetime/control contracts and an existing
long service-family synopsis). Refresh inventory and session brief.

## Review, correction and results
Claude implemented 17 production-file edits before its process returned 143,
with empty stderr and no final result. Root confirmed it had stopped, retained
the partial log and completed reconciliation/docs. No termination cause or
stale-artifact diagnosis is inferred. Independent review found no blocker; both
suggestions were accepted and re-reviewed: reuse the prepared context for queries,
and ban the broad processing module in ten family leaves.

The first full CPU run caught two old architecture assertions: the Clustering
pointer's former header location and a 100-line private-interface ceiling.
The owner check now asserts presence at the family owner and absence at the old
header, retaining retired-route bans. Actual compiler dependency bans replace
the line cap, alongside existing PImpl/storage checks. A fixed test review found
no blocker; the nine-case reconciliation selector and final full CPU run passed.
Original failed-run evidence remains. These are in-scope test corrections.

Final native CPU: 4,594 passed, one expected ASan-only GLFW lifecycle skip.
Focused ASan and UBSan: 98 passed each, no skips. Actual Vulkan: one passed,
no skip. All four builds and strict checks passed; 1,362 final source/build-input
hashes match. Eighteen production files lose 571 physical / 549 nonblank lines;
no production file added/deleted. Eleven frame closures narrow from 168–180
modules to 59–95; private attachment stays at 18. No matched timing claim.
Evidence: `build/analysis/runtime243-processing-frame-locality-2026-09-14/`.

## Remaining scope
Implementation and scoped verification are complete and locally integrated.
RUNTIME-246 records the final combined sanitizer gates. BUILD-007 owns matched
compile measurements; further consumer-justified simplification and REVIEW-004
convergence remain separate. BUG-188 host discovery and BUG-180's
separate leak-enabled follow-up remain unchanged. No whole-engine completion verdict or push is inferred.
