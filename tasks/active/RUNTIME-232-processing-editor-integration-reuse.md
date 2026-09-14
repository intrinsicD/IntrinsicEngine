---
id: RUNTIME-232
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive structural simplification; the scoped diff, independent review and relevant tests provide implementation evidence.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-232 — Simplify processing result wiring and panel workflows

## Goal
Remove repeated processing/editor integration mechanisms while preserving the
typed APIs, attachment lifetime, config validation and user workflows. The
operator explicitly requested this scope and iterative Claude review.

## Design and reuse decisions
1. Reuse `EditorGeometryProcessingResultsSnapshot` as the session's typed result
   aggregate. Replace parallel per-result pointer lists in contexts/bindings with
   one borrowed aggregate pointer; copy at the prepared-frame boundary. Reuse
   the existing attachment epoch and typed sinks, with one private sink factory
   for their identical lifetime guard. Keep explicit dismissal and the model's
   selected result projection. No result registry, erased payload or new module.
2. Reuse `ProcessingDraftState`, `DrawProcessingPointInput`,
   `DrawProcessingPropertyName` and `DrawProcessingExecution` in the remaining
   compatible point panels. Preserve method-specific input coupling, controls,
   readiness resolution, histogram display state and visualization recipes.

The flagged ceremony is repeated storage/projection and edit/validate/run code.
The retained boundaries carry attachment safety, typed results and config parity.
The blast radius is runtime editor composition, app panels and their tests;
layer ownership does not change. Separate result storage is warranted again only
if a real consumer needs independent retention/lifetime semantics. Separate panel
execution is warranted when its validated request flow differs.

## Engine integration
| Surface | Preserved behavior |
|---|---|
| Least-structured input | Existing typed canonical property domains and required topology. |
| Compatible entity sources | All existing mesh, graph and point-cloud sources; one/two-entity preflights. |
| RuntimeModule | Existing method services and JobService; epoch-guarded delivery. |
| Config/agent | Existing serializable sections and validated preview/apply path. |
| UI | Explicit algorithm controls with shared entity/input/output and execution mechanisms. |
| Publication | Existing property/history and owning construction operations; kernels unchanged. |
| End-to-end tests | Session lifecycle, model projection, processing panel interaction and CPU operation suites. |

## Acceptance criteria
- [x] One typed aggregate owns session results and crosses internal projections;
  prepared results remain independent copies and expired handles cannot access it.
- [x] All typed sinks and dismiss slots preserve independent results, attachment
  replacement/destruction safety and fresh frame visibility.
- [x] Compatible point panels reuse canonical controls and execution with their
  distinct config/readiness and display behavior preserved.
- [x] The combined production footprint decreases, counting shared code; no new
  production source file or module is introduced.
- [x] Pass focused and full CPU verification and update architecture/discovery documentation.
- [x] Review the fixed diff with Claude, verify its findings against source and
  address the missing rejection and empty-result test coverage.

## Verification
```bash
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure --no-tests=error -R '^(SandboxEditor|SandboxProcessingPanels|SandboxDescriptorAnalysisPanel|ParameterizationOperations)' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --check
```

## Current evidence

Implementation, Claude review and local verification complete; awaiting commit/retirement.
No commit or push was requested for this slice. Ten existing production files shrink from
16,608 to 15,893 physical lines (715 removed); no production file/module added.

- `IntrinsicTests` and `ExtrinsicSandbox` build with the configured Clang 23 ci preset.
- Focused editor/model/parameterization/panel tests: 271/271 pass.
- Full CPU selector: 4,524 pass, six capability skips, zero failures (113.41 s).
  Five skips require display access blocked inside the sandbox; all five pass
  when rerun with local display access. Combined: 4,529 pass and the expected
  unsanitized GLFW LeakSanitizer capability skip remains.
- Strict layering/task/test-layout checks, documentation links, skill validation,
  mirror freshness, generated inventory/brief and diff formatting pass.
- The new runtime test covers all 22 typed sinks, copied snapshot independence,
  replacement/dismissal, reattachment and callbacks/commands after destruction.
  The new UI test clicks the four newly shared Run controls and compares their
  outputs with direct CPU execution. Existing selection, backend, visualization
  and histogram-follow interaction tests pass.
- Fixed during verification: aggregate definition ordering, remaining test-seam
  pointer reset, UV-result status assertion and the old storage-name architecture
  assertion. No gate was weakened.

Exact scoped diff, before/after hashes/counts and logs are retained locally at
`build/analysis/processing-integration-2026-09-11/`. The user authorized sending
`integration.diff`; Claude reviewed exactly that diff with no extra source excerpts.
The completed transcript is `claude-review.txt`; `claude-review-disposition.md`
records each source check and correction. The first CLI attempt returned attempted
tool calls instead of a review; the text-only retry produced the review.

Claude's purported lifetime issue is not present: `AttachmentEpochIsActive` is a
free function using the retained epoch, not a session member call. The visualization
helper already chooses label/scalar/color recipes from the typed reference, and
existing Show interaction tests cover the resulting Appearance lanes. A complete
build and source search confirm the pointer-field migration; both aggregate
declarations are inside the same exported namespace.

The useful test gaps are closed: all 24 empty result slots are checked before
attachment, a published result disappears after detach, and the four shared
execution panels now receive invalid numeric edits and missing inputs through
real UI/config actions before a valid run. Rejection leaves accepted config,
job submissions and outputs unchanged. Only tests and review notes changed after
Claude's review; the production diff remains byte-identical. Both changed test
targets rebuilt and all 271 focused tests passed again (9.12 s). The previous full
CPU/display evidence remains applicable to the unchanged production source.
