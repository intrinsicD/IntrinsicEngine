---
id: RUNTIME-245
theme: J
depends_on: [RUNTIME-244]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive dependency cleanup with a captured dirty baseline, fixed Claude review and compiler/correctness checks; no performance claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-245 — Narrow editor action helper dependencies

## Goal
Continue the operator-authorized engine simplification with Claude. Keep scene
and visualization actions dependent on their actual helper contracts, preserving
all current commands, history, publication and attachment behavior.

## Reuse and right-sizing decision
Both action units include all workspace bindings to call shared helper functions.
Reuse the compiled helper implementations and separate their declaration needs
from workspace storage. Retain the thin geometry helper and private attachment
boundaries. No new runtime abstraction or compatibility path is needed.
Scene primitive-view history and visualization render-hint history are not
substitutable: only the latter owns surface visualization state. Keep their
operation-specific transactions until a contract-preserving consolidation is
justified. Claude recommended two private declaration headers: command/file
helpers and property/model helpers. The existing broad header includes both;
actions include only helper declarations. The two files add no implementations
or runtime abstractions. Scene no longer imports visualization; visualization
retains the scene import required by the command helper declarations. Reconsider
a broader abstraction only if a new consumer demonstrates a shared transaction
contract, not from matching emplace/undo lines.

## Acceptance criteria
- [x] Match the prior 1,362 verified source hashes and capture the dirty baseline.
- [x] Plan the exact helper boundary with Claude and implement the bounded slice.
- [x] Review a fixed diff with Claude and resolve valid findings.
- [x] Verify compiler closures, full native CPU and focused sanitizer behavior.
- [x] Synchronize ownership docs, locality guards and task results.

## Verification
Native Clang 23 configure/build:
```bash
cmake --preset ci --fresh -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
After the final import corrections, reconfigure the same preset, rebuild both
native targets, then configure ci-asan and ci-ubsan fresh with
`-DINTRINSIC_GROUP_PURE_CTEST=ON` and build `IntrinsicCpuTests --parallel 8`.
All compilation finished before final tests. The final native gate passed
4,599 tests, with one expected ASan-only GLFW lifecycle skip (4,600 total).

Separate ASan and UBSan each passed 97 focused tests, zero skips. The selector
covers every ordinary test case in `Test.SandboxEditorSceneCommands.cpp` and
`Test.SandboxEditorVisualization.cpp`, plus `SandboxEditorSession`,
`EditorCommandHistory`, `EditorCompilationLocality` and `RuntimeEnginePrivateGlue`.
It uses the same CPU exclusions, `--no-tests=error --timeout 60 --parallel 1`;
exact commands and source-derived selector are retained in the evidence archive.
Full sanitizer gates for the preceding combined source remain in RUNTIME-244;
this slice reruns affected sanitizer coverage, not the entire sanitizer suite.

Actual compiler closures: scene actions 220 -> 55, visualization actions
220 -> 75. The other fifteen monitored producers retain identical closures,
including the private attachment at 18. No producer gains a module dependency.
The Actions guard also excludes graphics texture-bake and RHI command-context
implementations; these actions use the runtime bake contract.

Six production files change, including two new declaration headers: net minus
138 physical / 147 nonblank lines. No implementation, module, public API or
runtime indirection is added. Both action bodies (apart from unused aliases),
shared records, helper implementation owner, session, attachment and thin
geometry header match the baseline. The final 1,364 source/build hashes match
all final gates. Module inventory remains at 418; strict workshop, task,
layering, links, test layout, root hygiene, kernel/compile-tool and skill-mirror
checks pass. Source documentation reports zero errors; three retained ownership
comments were inspected.

## Review and corrections
Claude planned, implemented and independently reviewed the fixed diff, then
reviewed the correction diff; no blockers remain. Root restored the required
SceneEditingOperations and Asset.ImportRouter imports, removed an unnecessary
graphics texture-bake import, and resolved the documentation findings. The
Registry import stays because visualization uses its AssetId.

An interim native run had three dependency-check failures after root edited the
source during testing: the scanners correctly reported source newer than their
build metadata. No behavior test failed. Final verification uses frozen source,
freshly rebuilt metadata and a repeated full native gate. Initial build/test
failure logs and their diagnosis remain in the archive; no gate was weakened.

Evidence: `build/analysis/runtime245-editor-action-helper-locality-2026-09-14/`.

## Remaining scope
Implementation, review and verification are complete, pending accumulated
integration. Matched eligible-source compile timing, genuinely shared mechanisms
in remaining workspace/model composition, and REVIEW-004 convergence remain
open. Scene primitive-view Surface tracking is intentionally unchanged; changing
its undo contract would need its own focused test and decision. Existing BUG-188
discovery and BUG-180 leak-enabled follow-ups are unchanged. No new Vulkan run,
compile-time performance claim, commit/push or whole-engine completion verdict
is inferred from this slice.
