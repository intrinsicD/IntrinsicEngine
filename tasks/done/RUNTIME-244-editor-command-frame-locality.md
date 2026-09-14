---
id: RUNTIME-244
theme: J
depends_on: [RUNTIME-243]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive dependency refactor with a fixed dirty baseline, Claude review and compiler/correctness verification; no timing claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-244 — Narrow editor command-frame dependencies

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
BUILD-007 owns matched engine compile measurements; C92 remains a hypothesis.


## Goal
Continue the operator-authorized simplification with Claude. Remove broad editor
bindings from scene, visualization and render-recipe command preparation while
preserving command lifetime, result copies, draft revisions and model statistics.

## Reuse and right-sizing decision
Reuse the existing three context-conversion functions behind the existing private
attachment interface. Their complete context definitions remain family-owned;
matching C++ declarations permit opaque return types. Scene history callbacks
read the already-copied context fields. Keep construction timing unchanged:
visualization receives its statistics pointer after snapshot construction.
Reject extra cached contexts, new files and another command abstraction. Action
implementations still use shared helpers and remain outside this bounded slice.

## Acceptance criteria
- [x] Match the prior 1,362 verified source hashes and capture exact dirty baseline.
- [x] Review concrete consumers and plan with Claude.
- [x] Implement and reconcile; review a fixed diff with Claude and fix findings.
- [x] Verify native CPU, separate full CPU sanitizers, representative Vulkan and structural checks.
- [x] Update ownership docs, compiler guards, module inventory and evidence.

## Verification
```bash
cmake --preset ci --fresh -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
For each of ci-asan and ci-ubsan, configure fresh with
`-DINTRINSIC_GROUP_PURE_CTEST=ON`, build IntrinsicCpuTests and run the same
exclusion-only CPU selector with `--parallel 1`. Build ci-vulkan target
IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests and run
`RuntimeSandboxAcceptanceGpuSmoke.SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes`
with both gpu/vulkan labels, `--no-tests=error --timeout 120` and unchanged
registered leak settings. Compile before running tests. Run strict workshop,
layout/root, skill-mirror and compile-tool checks; audit changed interfaces.
Evidence is recorded under `build/analysis/runtime244-editor-command-frame-locality-2026-09-14/`.

## Review and results
Claude planned and implemented the seven-file refactor, then independently
reviewed a fixed diff with no blockers. Root verified unchanged context fields,
order/defaults and command bodies except the intended scene-field substitutions.
The source audit caught a missing synopsis and a historical comment; both were
fixed. Final source documentation has zero errors and four inspected ownership
and lifetime advisories. Strict structural checks pass.

Seven production files lose 125 physical / 120 nonblank lines; no production
file is added or deleted. Actual compiler closures narrow from 168 to 55 for
scene commands, 53 for visualization and 58 for recipe editing. The private
attachment stays at 18; eleven processing frames and both action units have
unchanged closures. No timing gain is claimed.

Full native CPU: 4,598 passed, one expected ASan-only GLFW skip. Full ASan:
2,977 passed, no skips. Full UBSan: 2,976 passed, one expected GLFW skip. Sanitizer
registration groups the audited pure cohorts; both use the full CPU selector.

The Vulkan gate exposed BUG-192: CTest killed a sequential appearance test at
30 seconds, before its own 45-second watchdog. Exact-environment probes reached
phase 14; retained fixture diagnostics showed 47 frames, settle=2, and wall-clock
budget exhaustion. Claude reviewed the test-only correction: 90-second internal
budget, 120-second CTest deadline, existing frame cap/pixel checks retained,
plus explicit readiness assertion and exit diagnostics. The original registered
Vulkan test then passed without a skip in 30.4 seconds. Original failures and
probes remain in the archive; host pacing varies, so no fixed-rate claim is made.

After that GPU-only correction, all three CPU trees were reconciled and every
CPU registration compared equal (commands, environment, labels and deadlines).
Focused editor/lifetime/compiler checks then passed 102/102 in each tree using
`-R 'RuntimeEnginePrivateGlue|SandboxEditorPresentation|SandboxEditorSession|Workspace|ModelCache|SelectedAnalysis|ProcessingPanel|NormalPanel|RegistrationPanel|ParameterizationPanel|PointCloudConsolidationPanel|EditorCompilationLocality'`
and the same exclusions with `--parallel 1`. The full CPU results cover unchanged
production inputs; only the GPU fixture and its per-test registration changed
later. The final 1,362 source/build-input hashes and registry comparisons bind
that distinction explicitly.

## Remaining scope
Implementation and verification are complete and locally integrated.
Full CPU sanitizer verification is now recorded for the combined production
source. Matched eligible-source compile measurements, consumer/helper analysis
of the two remaining broad action units, and REVIEW-004 convergence remain open.
BUG-188 discovery and BUG-180's separate leak-enabled follow-up are unchanged.
No push or whole-engine completion verdict is inferred.
